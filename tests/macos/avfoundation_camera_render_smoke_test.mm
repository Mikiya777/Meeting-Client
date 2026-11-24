#include "render/include/IRenderEngine.h"

#include <QApplication>
#include <QMetaObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSurfaceFormat>

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kUserName = "avfoundation-camera-user";
constexpr int kRenderWidth = 1280;
constexpr int kRenderHeight = 720;
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;

int64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

double AverageYPlane(const std::shared_ptr<RENDER::I420Frame> &frame) {
  if (!frame || !frame->HasData()) {
    return 0.0;
  }

  const size_t y_size = frame->GetYPlaneSize();
  uint64_t sum = 0;
  for (size_t i = 0; i < y_size; ++i) {
    sum += frame->data[0][i];
  }

  return y_size == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(y_size);
}

std::shared_ptr<RENDER::I420Frame> ConvertNV12ToI420(CVPixelBufferRef pixel_buffer) {
  if (pixel_buffer == nullptr ||
      !CVPixelBufferIsPlanar(pixel_buffer) ||
      CVPixelBufferGetPlaneCount(pixel_buffer) < 2) {
    return nullptr;
  }

  const int width = static_cast<int>(CVPixelBufferGetWidth(pixel_buffer));
  const int height = static_cast<int>(CVPixelBufferGetHeight(pixel_buffer));
  if (width <= 0 || height <= 0 || width % 2 != 0 || height % 2 != 0) {
    return nullptr;
  }

  CVPixelBufferLockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);

  const auto *y_src = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  const auto *uv_src = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  const size_t y_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  const size_t uv_stride = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);

  if (y_src == nullptr || uv_src == nullptr) {
    CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
    return nullptr;
  }

  auto frame = std::make_shared<RENDER::I420Frame>();
  frame->width = width;
  frame->height = height;
  frame->timestamp_us = NowUs();

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  for (int row = 0; row < height; ++row) {
    std::memcpy(frame->data[0].get() + static_cast<size_t>(row) * width,
                y_src + static_cast<size_t>(row) * y_stride,
                static_cast<size_t>(width));
  }

  for (int row = 0; row < height / 2; ++row) {
    const auto *uv_row = uv_src + static_cast<size_t>(row) * uv_stride;
    auto *u_row = frame->data[1].get() + static_cast<size_t>(row) * (width / 2);
    auto *v_row = frame->data[2].get() + static_cast<size_t>(row) * (width / 2);
    for (int column = 0; column < width / 2; ++column) {
      u_row[column] = uv_row[column * 2];
      v_row[column] = uv_row[column * 2 + 1];
    }
  }

  CVPixelBufferUnlockBaseAddress(pixel_buffer, kCVPixelBufferLock_ReadOnly);
  return frame;
}

float ReadPCMSample(const uint8_t *data,
                    size_t sample_index,
                    const AudioStreamBasicDescription &format) {
  if (data == nullptr) {
    return 0.0f;
  }

  const bool is_float = (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
  const bool is_signed_integer = (format.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;

  if (is_float && format.mBitsPerChannel == 32) {
    return reinterpret_cast<const float *>(data)[sample_index];
  }

  if (is_signed_integer && format.mBitsPerChannel == 16) {
    return static_cast<float>(reinterpret_cast<const int16_t *>(data)[sample_index]) / 32768.0f;
  }

  if (is_signed_integer && format.mBitsPerChannel == 32) {
    return static_cast<float>(reinterpret_cast<const int32_t *>(data)[sample_index]) / 2147483648.0f;
  }

  return 0.0f;
}

std::vector<float> ResampleToRenderFormat(const std::vector<float> &input,
                                          int input_sample_rate,
                                          int input_channels,
                                          int input_frames) {
  if (input.empty() || input_sample_rate <= 0 || input_channels <= 0 || input_frames <= 0) {
    return {};
  }

  const int output_frames = std::max(
      1,
      static_cast<int>(std::llround(
          static_cast<double>(input_frames) * kSampleRate /
          static_cast<double>(input_sample_rate))));
  std::vector<float> output(static_cast<size_t>(output_frames) * kChannels);

  for (int frame = 0; frame < output_frames; ++frame) {
    const double source_position =
        output_frames == 1
            ? 0.0
            : static_cast<double>(frame) * static_cast<double>(input_frames - 1) /
                  static_cast<double>(output_frames - 1);
    const int left_index = static_cast<int>(source_position);
    const int right_index = std::min(left_index + 1, input_frames - 1);
    const float ratio = static_cast<float>(source_position - static_cast<double>(left_index));

    for (int channel = 0; channel < kChannels; ++channel) {
      const int input_channel = std::min(channel, input_channels - 1);
      const float left = input[static_cast<size_t>(left_index) * input_channels + input_channel];
      const float right = input[static_cast<size_t>(right_index) * input_channels + input_channel];
      output[static_cast<size_t>(frame) * kChannels + channel] =
          left + (right - left) * ratio;
    }
  }

  return output;
}

std::shared_ptr<RENDER::AudioFrame> ConvertAudioSampleBuffer(CMSampleBufferRef sample_buffer,
                                                            int *source_sample_rate,
                                                            int *source_channels,
                                                            int *source_frames,
                                                            double *peak_level) {
  if (sample_buffer == nullptr) {
    return nullptr;
  }

  CMFormatDescriptionRef format_description =
      CMSampleBufferGetFormatDescription(sample_buffer);
  const AudioStreamBasicDescription *format =
      CMAudioFormatDescriptionGetStreamBasicDescription(format_description);
  if (format == nullptr || format->mFormatID != kAudioFormatLinearPCM) {
    return nullptr;
  }

  const int input_sample_rate = static_cast<int>(std::llround(format->mSampleRate));
  const int input_channels = static_cast<int>(format->mChannelsPerFrame);
  const int input_frames = static_cast<int>(CMSampleBufferGetNumSamples(sample_buffer));
  if (input_sample_rate <= 0 || input_channels <= 0 || input_frames <= 0) {
    return nullptr;
  }

  size_t audio_buffer_list_size = 0;
  OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
      sample_buffer,
      &audio_buffer_list_size,
      nullptr,
      0,
      nullptr,
      nullptr,
      kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
      nullptr);
  if (status != noErr || audio_buffer_list_size == 0) {
    return nullptr;
  }

  std::vector<uint8_t> audio_buffer_list_storage(audio_buffer_list_size);
  auto *audio_buffer_list =
      reinterpret_cast<AudioBufferList *>(audio_buffer_list_storage.data());
  CMBlockBufferRef block_buffer = nullptr;
  status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
      sample_buffer,
      nullptr,
      audio_buffer_list,
      audio_buffer_list_size,
      nullptr,
      nullptr,
      kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment,
      &block_buffer);
  if (status != noErr) {
    if (block_buffer != nullptr) {
      CFRelease(block_buffer);
    }
    return nullptr;
  }

  const bool is_non_interleaved =
      (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
  std::vector<float> input_samples(static_cast<size_t>(input_frames) * input_channels);
  double peak = 0.0;

  for (int frame = 0; frame < input_frames; ++frame) {
    for (int channel = 0; channel < input_channels; ++channel) {
      float sample = 0.0f;
      if (is_non_interleaved) {
        const UInt32 buffer_index =
            std::min<UInt32>(static_cast<UInt32>(channel),
                             audio_buffer_list->mNumberBuffers - 1);
        sample = ReadPCMSample(
            static_cast<const uint8_t *>(audio_buffer_list->mBuffers[buffer_index].mData),
            static_cast<size_t>(frame),
            *format);
      } else if (audio_buffer_list->mNumberBuffers > 0) {
        sample = ReadPCMSample(
            static_cast<const uint8_t *>(audio_buffer_list->mBuffers[0].mData),
            static_cast<size_t>(frame) * input_channels + channel,
            *format);
      }

      sample = std::clamp(sample * 2.5f, -1.0f, 1.0f);
      input_samples[static_cast<size_t>(frame) * input_channels + channel] = sample;
      peak = std::max(peak, static_cast<double>(std::abs(sample)));
    }
  }

  if (block_buffer != nullptr) {
    CFRelease(block_buffer);
  }

  std::vector<float> output_samples =
      ResampleToRenderFormat(input_samples, input_sample_rate, input_channels, input_frames);
  if (output_samples.empty()) {
    return nullptr;
  }

  auto frame = std::make_shared<RENDER::AudioFrame>();
  frame->sample_rate = kSampleRate;
  frame->channels = kChannels;
  frame->samples_per_channel =
      static_cast<int>(output_samples.size() / static_cast<size_t>(kChannels));
  frame->timestamp_us = 0;
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memcpy(frame->data.get(), output_samples.data(), frame->DataSize());

  if (source_sample_rate) {
    *source_sample_rate = input_sample_rate;
  }
  if (source_channels) {
    *source_channels = input_channels;
  }
  if (source_frames) {
    *source_frames = input_frames;
  }
  if (peak_level) {
    *peak_level = peak;
  }

  return frame;
}

class AVFoundationCameraWidget;

} // namespace

@interface CameraSampleDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate,
                                           AVCaptureAudioDataOutputSampleBufferDelegate>
- (instancetype)initWithWidget:(AVFoundationCameraWidget *)widget;
@end

namespace {

class AVFoundationCameraWidget final : public QOpenGLWidget {
public:
  explicit AVFoundationCameraWidget(QWidget *parent = nullptr)
      : QOpenGLWidget(parent) {
    setWindowTitle(QStringLiteral("AVFoundation Camera/Mic -> Render Smoke Test"));
    resize(960, 540);
  }

  ~AVFoundationCameraWidget() override {
    StopCapture();

    makeCurrent();
    if (m_render_engine) {
      m_render_engine->RemoveUser(kUserName);
      m_render_engine->Uninitialize();
      m_render_engine.reset();
    }
    doneCurrent();
  }

  void OnVideoSampleBuffer(CMSampleBufferRef sample_buffer) {
    CVImageBufferRef image_buffer = CMSampleBufferGetImageBuffer(sample_buffer);
    auto frame = ConvertNV12ToI420(image_buffer);
    if (!frame || !m_render_engine) {
      return;
    }

    if (m_render_engine->PushVideoFrame(kUserName, frame)) {
      m_last_width = frame->width;
      m_last_height = frame->height;
      m_last_avg_y = AverageYPlane(frame);
      ++m_video_count;
      QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
    }
  }

  void OnAudioSampleBuffer(CMSampleBufferRef sample_buffer) {
    if (!m_render_engine) {
      return;
    }

    int source_sample_rate = 0;
    int source_channels = 0;
    int source_frames = 0;
    double peak_level = 0.0;
    auto frame = ConvertAudioSampleBuffer(
        sample_buffer, &source_sample_rate, &source_channels, &source_frames, &peak_level);
    if (!frame) {
      return;
    }

    if (m_render_engine->PushAudioFrame(kUserName, frame)) {
      ++m_audio_count;
      m_last_audio_sample_rate = source_sample_rate;
      m_last_audio_channels = source_channels;
      m_last_audio_frames = source_frames;
      m_last_audio_peak = peak_level;
      QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
    }
  }

protected:
  void initializeGL() override {
    m_render_engine = RENDER::IRenderEngine::CreateRenderEngine();
    if (!m_render_engine ||
        !m_render_engine->Initialize(kSampleRate, kChannels, kRenderWidth, kRenderHeight) ||
        !m_render_engine->AddUser(kUserName, true)) {
      SetStatus("RenderEngine initialize failed");
      return;
    }

    StartCapture();
  }

  void resizeGL(int width, int height) override {
    if (m_render_engine) {
      m_render_engine->UpdateUserVideoSize(kUserName, width, height);
    }
  }

  void paintGL() override {
    bool rendered = false;
    if (m_render_engine) {
      rendered = m_render_engine->RenderUser(kUserName);
    }

    QPainter painter(this);
    if (!rendered) {
      painter.fillRect(rect(), QColor(16, 18, 24));
    }

    painter.fillRect(QRect(12, 12, 780, 124), QColor(0, 0, 0, 170));
    painter.setPen(Qt::white);
    painter.drawText(QRect(24, 22, 740, 24), QString::fromStdString(Status()));
    painter.drawText(QRect(24, 50, 740, 24),
                     QStringLiteral("video frames: %1    audio frames: %2    rendered: %3")
                         .arg(m_video_count.load())
                         .arg(m_audio_count.load())
                         .arg(rendered ? QStringLiteral("yes") : QStringLiteral("no")));
    painter.drawText(QRect(24, 76, 740, 20),
                     QStringLiteral("frame: %1x%2    avg Y: %3")
                         .arg(m_last_width.load())
                         .arg(m_last_height.load())
                         .arg(m_last_avg_y.load(), 0, 'f', 1));
    painter.drawText(QRect(24, 100, 740, 20),
                     QStringLiteral("mic source: %1 Hz / %2 ch / %3 frames    peak: %4")
                         .arg(m_last_audio_sample_rate.load())
                         .arg(m_last_audio_channels.load())
                         .arg(m_last_audio_frames.load())
                         .arg(m_last_audio_peak.load(), 0, 'f', 3));
  }

private:
  void StartCapture() {
    AVAuthorizationStatus video_status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    AVAuthorizationStatus audio_status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (video_status == AVAuthorizationStatusDenied ||
        video_status == AVAuthorizationStatusRestricted) {
      SetStatus("Camera permission denied/restricted");
      return;
    }
    if (audio_status == AVAuthorizationStatusDenied ||
        audio_status == AVAuthorizationStatusRestricted) {
      SetStatus("Microphone permission denied/restricted");
      return;
    }

    AVCaptureDevice *video_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (video_device == nil) {
      SetStatus("No AVFoundation camera device");
      return;
    }
    AVCaptureDevice *audio_device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    if (audio_device == nil) {
      SetStatus("No AVFoundation microphone device");
      return;
    }

    NSError *error = nil;
    AVCaptureDeviceInput *video_input =
        [AVCaptureDeviceInput deviceInputWithDevice:video_device error:&error];
    if (video_input == nil) {
      SetStatus("Create camera input failed");
      return;
    }
    AVCaptureDeviceInput *audio_input =
        [AVCaptureDeviceInput deviceInputWithDevice:audio_device error:&error];
    if (audio_input == nil) {
      SetStatus("Create microphone input failed");
      return;
    }

    m_session = [[AVCaptureSession alloc] init];
    if ([m_session canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
      m_session.sessionPreset = AVCaptureSessionPreset1280x720;
    }

    if (![m_session canAddInput:video_input]) {
      SetStatus("Cannot add camera input");
      return;
    }
    [m_session addInput:video_input];

    if (![m_session canAddInput:audio_input]) {
      SetStatus("Cannot add microphone input");
      return;
    }
    [m_session addInput:audio_input];

    AVCaptureVideoDataOutput *video_output = [[AVCaptureVideoDataOutput alloc] init];
    video_output.videoSettings = @{
      (id)kCVPixelBufferPixelFormatTypeKey :
          @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
    };
    video_output.alwaysDiscardsLateVideoFrames = YES;

    AVCaptureAudioDataOutput *audio_output = [[AVCaptureAudioDataOutput alloc] init];

    m_delegate = [[CameraSampleDelegate alloc] initWithWidget:this];
    m_capture_queue = dispatch_queue_create("meeting.avfoundation.av", DISPATCH_QUEUE_SERIAL);
    [video_output setSampleBufferDelegate:m_delegate queue:m_capture_queue];
    [audio_output setSampleBufferDelegate:m_delegate queue:m_capture_queue];

    if (![m_session canAddOutput:video_output]) {
      SetStatus("Cannot add camera output");
      return;
    }
    [m_session addOutput:video_output];

    if (![m_session canAddOutput:audio_output]) {
      SetStatus("Cannot add microphone output");
      return;
    }
    [m_session addOutput:audio_output];

    SetStatus("Capturing AVFoundation camera/mic: " +
              std::string([[video_device localizedName] UTF8String]) + " / " +
              std::string([[audio_device localizedName] UTF8String]));
    [m_session startRunning];
  }

  void StopCapture() {
    if (m_session) {
      [m_session stopRunning];
      m_session = nil;
    }
    m_delegate = nil;
    m_capture_queue = nil;
  }

  void SetStatus(std::string status) {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_status = std::move(status);
  }

  std::string Status() const {
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_status;
  }

private:
  std::unique_ptr<RENDER::IRenderEngine> m_render_engine;
  AVCaptureSession *m_session = nil;
  CameraSampleDelegate *m_delegate = nil;
  dispatch_queue_t m_capture_queue = nil;
  std::atomic<int> m_video_count{0};
  std::atomic<int> m_audio_count{0};
  std::atomic<int> m_last_width{0};
  std::atomic<int> m_last_height{0};
  std::atomic<double> m_last_avg_y{0.0};
  std::atomic<int> m_last_audio_sample_rate{0};
  std::atomic<int> m_last_audio_channels{0};
  std::atomic<int> m_last_audio_frames{0};
  std::atomic<double> m_last_audio_peak{0.0};
  mutable std::mutex m_status_mutex;
  std::string m_status = "Initializing AVFoundation camera/mic test...";
};

} // namespace

@implementation CameraSampleDelegate {
  AVFoundationCameraWidget *_widget;
}

- (instancetype)initWithWidget:(AVFoundationCameraWidget *)widget {
  self = [super init];
  if (self) {
    _widget = widget;
  }
  return self;
}

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection *)connection {
  (void)output;
  (void)connection;
  if (_widget != nullptr) {
    if ([output isKindOfClass:[AVCaptureVideoDataOutput class]]) {
      _widget->OnVideoSampleBuffer(sampleBuffer);
    } else if ([output isKindOfClass:[AVCaptureAudioDataOutput class]]) {
      _widget->OnAudioSampleBuffer(sampleBuffer);
    }
  }
}

@end

int main(int argc, char **argv) {
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);

  AVFoundationCameraWidget widget;
  widget.show();

  return app.exec();
}
