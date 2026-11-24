#include "capture/src/windows/QtCaptureRuntime.h"

#include "platform/windows/audio/WasapiDevice.h"

#include <QCamera>
#include <QDebug>
#include <QImage>
#include <QVideoFrame>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QAbstractVideoSurface>
#include <QCameraInfo>
#include <QVideoSurfaceFormat>
#else
#include <QCameraDevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoSink>
#endif

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <utility>

namespace CAPTURE {
namespace {

using PLATFORM::WasapiDevice;
using Clock = std::chrono::steady_clock;

int64_t CaptureTimeUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      Clock::now().time_since_epoch()).count();
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using CameraInput = QCameraInfo;

std::string CameraId(const CameraInput &device) {
  return device.deviceName().toUtf8().toHex().toStdString();
}

class CameraSurface final : public QAbstractVideoSurface {
public:
  using FrameCallback = std::function<void(const QVideoFrame &, bool)>;

  explicit CameraSurface(FrameCallback callback) : callback_(std::move(callback)) {}

  QList<QVideoFrame::PixelFormat>
  supportedPixelFormats(QAbstractVideoBuffer::HandleType handle_type) const override {
    if (handle_type == QAbstractVideoBuffer::NoHandle) {
      return {QVideoFrame::Format_RGB32};
    }
    return {};
  }

  bool present(const QVideoFrame &frame) override {
    // DirectShow presents under its frame mutex. Bound the queue and convert
    // the frame after control returns to the event loop.
    if (frame_pending_) {
      return true;
    }
    const bool bottom_up = surfaceFormat().scanLineDirection() == QVideoSurfaceFormat::BottomToTop;
    frame_pending_ = QMetaObject::invokeMethod(this, [this, frame, bottom_up] {
      frame_pending_ = false;
      callback_(frame, bottom_up);
    }, Qt::QueuedConnection);
    return frame_pending_;
  }

private:
  FrameCallback callback_;
  bool frame_pending_ = false;
};

auto AvailableCameras() { return QCameraInfo::availableCameras(); }
CameraInput DefaultCamera() { return QCameraInfo::defaultCamera(); }
#else
using CameraInput = QCameraDevice;

std::string CameraId(const CameraInput &device) {
  return device.id().toHex().toStdString();
}

auto AvailableCameras() { return QMediaDevices::videoInputs(); }
CameraInput DefaultCamera() { return QMediaDevices::defaultVideoInput(); }
#endif

CameraInput FindCamera(const std::string &id) {
  if (id.empty() || id == "default") {
    return DefaultCamera();
  }
  for (const auto &device : AvailableCameras()) {
    if (CameraId(device) == id) {
      return device;
    }
  }
  return CameraInput{};
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QCameraFormat SelectFormat(const QCameraDevice &device, const QSize &size, int fps) {
  QCameraFormat best;
  double best_score = std::numeric_limits<double>::infinity();
  for (const auto &format : device.videoFormats()) {
    const auto resolution = format.resolution();
    const double size_difference = std::abs(resolution.width() - size.width()) +
                                   std::abs(resolution.height() - size.height());
    const double fps_shortfall = std::max(0.0f, fps - format.maxFrameRate());
    const double score = size_difference + 100.0 * fps_shortfall;
    if (score < best_score) {
      best_score = score;
      best = format;
    }
  }
  return best;
}
#endif

std::shared_ptr<I420Frame> ConvertImage(QImage image, const QSize &size, int64_t time_us) {
  if (image.isNull()) {
    return {};
  }

  if (image.size() != size) {
    image = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    image = image.copy((image.width() - size.width()) / 2,
                       (image.height() - size.height()) / 2,
                       size.width(), size.height());
  }
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }

  cv::Mat input(size.height(), size.width(), CV_8UC4,
                image.bits(), image.bytesPerLine());
  cv::Mat i420;
  cv::cvtColor(input, i420, cv::COLOR_RGBA2YUV_I420);

  auto frame = std::make_shared<I420Frame>();
  frame->width = size.width();
  frame->height = size.height();
  frame->timestamp_us = time_us;
  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0].reset(new uint8_t[y_size]);
  frame->data[1].reset(new uint8_t[uv_size]);
  frame->data[2].reset(new uint8_t[uv_size]);
  const auto *planes = i420.ptr<uint8_t>();
  std::memcpy(frame->data[0].get(), planes, y_size);
  std::memcpy(frame->data[1].get(), planes + y_size, uv_size);
  std::memcpy(frame->data[2].get(), planes + y_size + uv_size, uv_size);
  return frame;
}

} // namespace

struct QtCaptureRuntime::State {
  struct Camera {
    const std::string id;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    CameraSurface surface;
    QCamera device;

    Camera(const CameraInput &input, CameraSurface::FrameCallback callback)
        : id(CameraId(input)), surface(std::move(callback)), device(input) {}
#else
    QCamera device;
    QVideoSink sink;
    QMediaCaptureSession session;

    explicit Camera(const CameraInput &input) : id(CameraId(input)), device(input) {}
#endif
  };

  std::unique_ptr<Camera> camera;
  std::unique_ptr<WasapiDevice> microphone;
  std::string camera_name;
  std::string mic_name;
  std::string mic_id;
  VideoDataCallback video_callback;
  std::shared_ptr<const AudioDataCallback> audio_callback;
  QSize size;
  int fps = 0;
  int channels = 0;
  int sample_rate = 0;
  int64_t next_video_time_us = 0;

  void DeliverFrame(const QVideoFrame &video, [[maybe_unused]] bool bottom_up = false) {
    const auto now = CaptureTimeUs();
    if (!video_callback || !video.isValid() || now < next_video_time_us) {
      return;
    }
    const int64_t period = 1000000 / fps;
    next_video_time_us = std::max(next_video_time_us + period, now + period / 2);
    try {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
      auto image = video.image();
      if (bottom_up) {
        image = image.mirrored(false, true);
      }
#else
      auto image = video.toImage();
#endif
      if (auto frame = ConvertImage(std::move(image), size, now)) {
        video_callback(frame);
      }
    } catch (const std::exception &error) {
      qWarning("Camera: frame delivery failed: %s", error.what());
    }
  }

  bool OpenCamera(const CameraInput &input) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto replacement = std::make_unique<Camera>(input,
        [this](const QVideoFrame &video, bool bottom_up) { DeliverFrame(video, bottom_up); });
    replacement->device.setViewfinder(&replacement->surface);
    QObject::connect(&replacement->device, &QCamera::errorOccurred,
                     &replacement->surface, [device = &replacement->device](QCamera::Error error) {
      if (error != QCamera::NoError) {
        qWarning("Camera: %s", qPrintable(device->errorString()));
      }
    });
#else
    auto replacement = std::make_unique<Camera>(input);
    const auto format = SelectFormat(input, size, fps);
    if (!format.isNull()) {
      replacement->device.setCameraFormat(format);
    }
    replacement->session.setCamera(&replacement->device);
    replacement->session.setVideoSink(&replacement->sink);
    QObject::connect(&replacement->device, &QCamera::errorOccurred,
                     &replacement->sink, [](QCamera::Error, const QString &message) {
      qWarning("Camera: %s", qPrintable(message));
    });
    QObject::connect(&replacement->sink, &QVideoSink::videoFrameChanged,
                     &replacement->sink, [this](const QVideoFrame &video) {
      DeliverFrame(video);
    }, Qt::QueuedConnection);
#endif

    auto previous = std::move(camera);
    if (previous) {
      previous->device.stop();
    }
    next_video_time_us = 0;
    replacement->device.start();
    if (replacement->device.error() != QCamera::NoError) {
      camera = std::move(previous);
      if (camera) {
        camera->device.start();
      }
      return false;
    }
    camera = std::move(replacement);
    return true;
  }

  bool OpenMic(const std::string &id) {
    return microphone->Start(id, sample_rate, channels,
                             [this](float *, const float *samples, uint32_t frames) {
      auto callback = std::atomic_load(&audio_callback);
      if (!callback || !samples || frames == 0) {
        return;
      }
      auto frame = std::make_shared<AudioFrame>();
      frame->samples = static_cast<int>(frames);
      frame->sample_rate = sample_rate;
      frame->channels = channels;
      frame->timestamp_us = CaptureTimeUs() -
          static_cast<int64_t>(frames) * 1000000 / sample_rate;
      const auto size = frame->GetDataSize();
      frame->data.reset(new uint8_t[size]);
      std::memcpy(frame->data.get(), samples, size);
      (*callback)(frame);
    });
  }
};

QtCaptureRuntime::QtCaptureRuntime() = default;
QtCaptureRuntime::~QtCaptureRuntime() {
  UnInit();
}

CaptureResult QtCaptureRuntime::Init(int sample_rate, int channels,
                                      int width, int height, int fps) {
  if (sample_rate <= 0 || channels <= 0 || width <= 0 || height <= 0 ||
      width % 2 != 0 || height % 2 != 0 || fps <= 0) {
    return CaptureResult::kInvalidArgument;
  }
  auto state = std::make_unique<State>();
  state->size = QSize(width, height);
  state->fps = fps;
  state->channels = channels;
  state->sample_rate = sample_rate;
  state->microphone = std::make_unique<WasapiDevice>(WasapiDevice::Direction::Capture);
  m_state = std::move(state);
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::UnInit() {
  if (m_state) {
    m_state->microphone->Stop();
    m_state->camera.reset();
    m_state.reset();
  }
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::AddCameraSource(const std::string &name,
                                                 const std::string &id) {
  if (m_state->camera || name.empty()) {
    return CaptureResult::kInvalidArgument;
  }
  const auto input = FindCamera(id);
  if (input.isNull()) {
    return CaptureResult::kNotFound;
  }
  if (!m_state->OpenCamera(input)) {
    return CaptureResult::kInternalError;
  }
  m_state->camera_name = name;
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::AddMicSource(const std::string &name,
                                              const std::string &id) {
  if (!m_state->mic_name.empty() || name.empty()) {
    return CaptureResult::kInvalidArgument;
  }
  if (!m_state->OpenMic(id)) {
    return CaptureResult::kInternalError;
  }
  m_state->mic_name = name;
  m_state->mic_id = id;
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::RemoveSource(const std::string &name) {
  if (m_state->camera && name == m_state->camera_name) {
    m_state->camera->device.stop();
    m_state->camera.reset();
    m_state->camera_name.clear();
    return CaptureResult::kSuccess;
  }
  if (!m_state->mic_name.empty() && name == m_state->mic_name) {
    m_state->microphone->Stop();
    m_state->mic_name.clear();
    m_state->mic_id.clear();
    return CaptureResult::kSuccess;
  }
  return CaptureResult::kNotFound;
}

CaptureResult QtCaptureRuntime::UpdateCameraSourceDevice(const std::string &name,
                                                          const std::string &id) {
  if (!m_state->camera || m_state->camera_name != name) {
    return CaptureResult::kNotFound;
  }
  const auto input = FindCamera(id);
  if (input.isNull()) {
    return CaptureResult::kNotFound;
  }
  if (CameraId(input) == m_state->camera->id) {
    return CaptureResult::kSuccess;
  }
  return m_state->OpenCamera(input) ? CaptureResult::kSuccess : CaptureResult::kInternalError;
}

CaptureResult QtCaptureRuntime::UpdateMicSourceDevice(const std::string &name,
                                                       const std::string &id) {
  if (m_state->mic_name.empty() || m_state->mic_name != name) {
    return CaptureResult::kNotFound;
  }
  m_state->microphone->Stop();
  if (!m_state->OpenMic(id)) {
    m_state->OpenMic(m_state->mic_id);
    return CaptureResult::kInternalError;
  }
  m_state->mic_id = id;
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::EnumCameraDevices(CaptureDeviceEnumCallback callback) {
  auto devices = AvailableCameras();
  const auto preferred = CameraId(DefaultCamera());
  const auto position = std::find_if(devices.begin(), devices.end(), [&](const auto &device) {
    return CameraId(device) == preferred;
  });
  if (position != devices.end()) {
    std::rotate(devices.begin(), position, position + 1);
  }
  for (const auto &device : devices) {
    const auto id = CameraId(device);
    const auto name = device.description().toUtf8();
    if (!callback(id.c_str(), name.constData())) {
      break;
    }
  }
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::EnumMicDevices(CaptureDeviceEnumCallback callback) {
  auto devices = m_state->microphone->Devices();
  const auto position = std::find_if(devices.begin(), devices.end(), [](const auto &device) {
    return device.is_default;
  });
  if (position != devices.end()) {
    std::rotate(devices.begin(), position, position + 1);
  }
  for (const auto &device : devices) {
    if (!callback(device.id.c_str(), device.name.c_str())) {
      break;
    }
  }
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::RegisterVideoCallback(VideoDataCallback callback) {
  m_state->video_callback = std::move(callback);
  return CaptureResult::kSuccess;
}

CaptureResult QtCaptureRuntime::RegisterAudioCallback(AudioDataCallback callback) {
  std::shared_ptr<const AudioDataCallback> registered;
  if (callback) {
    registered = std::make_shared<const AudioDataCallback>(std::move(callback));
  }
  std::atomic_store(&m_state->audio_callback, std::move(registered));
  return CaptureResult::kSuccess;
}

} // namespace CAPTURE
