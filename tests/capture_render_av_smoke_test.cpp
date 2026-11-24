#include "capture/include/CaptureFrame.h"
#include "capture/include/ICaptureEngine.h"
#include "render/include/IRenderEngine.h"

#include <QApplication>
#include <QMetaObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kUserName = "macbook-device-user";
constexpr const char *kCameraSourceName = "macbook-camera";
constexpr const char *kMicSourceName = "macbook-mic";
constexpr int kVideoWidth = 1280;
constexpr int kVideoHeight = 720;
constexpr int kVideoFps = 30;
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr double kPi = 3.14159265358979323846;

struct DeviceInfo {
  std::string id;
  std::string name;
};

bool IsDefaultDevice(const DeviceInfo &device) {
  return device.id == "default" || device.name == "Default" ||
         device.name == "default";
}

DeviceInfo SelectConcreteDevice(const std::vector<DeviceInfo> &devices) {
  for (const auto &device : devices) {
    if (!IsDefaultDevice(device)) {
      return device;
    }
  }

  return devices.empty() ? DeviceInfo{} : devices.front();
}

int64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

std::shared_ptr<RENDER::I420Frame> ToRenderVideoFrame(
    const std::shared_ptr<CAPTURE::I420Frame> &frame) {
  if (!frame || frame->Validate() != CAPTURE::CaptureResult::kSuccess) {
    return nullptr;
  }

  auto output = std::make_shared<RENDER::I420Frame>();
  output->data[0] = frame->data[0];
  output->data[1] = frame->data[1];
  output->data[2] = frame->data[2];
  output->width = frame->width;
  output->height = frame->height;
  output->timestamp_us = frame->timestamp_us > 0 ? frame->timestamp_us : NowUs();
  return output;
}

double AverageYPlane(const std::shared_ptr<CAPTURE::I420Frame> &frame) {
  if (!frame || frame->Validate() != CAPTURE::CaptureResult::kSuccess) {
    return 0.0;
  }

  const size_t y_size = frame->GetYPlaneSize();
  if (y_size == 0) {
    return 0.0;
  }

  uint64_t sum = 0;
  for (size_t i = 0; i < y_size; ++i) {
    sum += frame->data[0][i];
  }

  return static_cast<double>(sum) / static_cast<double>(y_size);
}

std::shared_ptr<RENDER::AudioFrame> ToRenderAudioFrame(
    const std::shared_ptr<CAPTURE::AudioFrame> &frame) {
  if (!frame || frame->Validate() != CAPTURE::CaptureResult::kSuccess) {
    return nullptr;
  }

  auto output = std::make_shared<RENDER::AudioFrame>();
  output->data = frame->data;
  output->sample_rate = frame->sample_rate;
  output->channels = frame->channels;
  output->samples_per_channel = frame->samples;
  // Smoke test uses immediate playback. OBS audio timestamps may be in a
  // different clock domain from RenderEngine's steady_clock sync clock.
  output->timestamp_us = 0;

  auto *samples = reinterpret_cast<float *>(output->data.get());
  const size_t sample_count =
      static_cast<size_t>(output->samples_per_channel) *
      static_cast<size_t>(output->channels);
  for (size_t i = 0; i < sample_count; ++i) {
    samples[i] *= 3.0f;
  }

  return output;
}

std::shared_ptr<RENDER::AudioFrame> MakeToneFrame(double &phase) {
  constexpr int kToneSamplesPerChannel = 480;
  constexpr double kToneHz = 440.0;

  auto frame = std::make_shared<RENDER::AudioFrame>();
  frame->sample_rate = kSampleRate;
  frame->channels = kChannels;
  frame->samples_per_channel = kToneSamplesPerChannel;
  frame->timestamp_us = 0;
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);

  auto *samples = reinterpret_cast<float *>(frame->data.get());
  const double step = 2.0 * kPi * kToneHz / static_cast<double>(kSampleRate);
  for (int i = 0; i < kToneSamplesPerChannel; ++i) {
    const float value = static_cast<float>(0.08 * std::sin(phase));
    for (int channel = 0; channel < kChannels; ++channel) {
      samples[i * kChannels + channel] = value;
    }

    phase += step;
    if (phase >= 2.0 * kPi) {
      phase -= 2.0 * kPi;
    }
  }

  return frame;
}

std::string CaptureResultName(CAPTURE::CaptureResult result) {
  switch (result) {
    case CAPTURE::CaptureResult::kSuccess:
      return "kSuccess";
    case CAPTURE::CaptureResult::kAlreadyInitialized:
      return "kAlreadyInitialized";
    case CAPTURE::CaptureResult::kNotInitialized:
      return "kNotInitialized";
    case CAPTURE::CaptureResult::kInvalidArgument:
      return "kInvalidArgument";
    case CAPTURE::CaptureResult::kInvalidData:
      return "kInvalidData";
    case CAPTURE::CaptureResult::kInvalidSize:
      return "kInvalidSize";
    case CAPTURE::CaptureResult::kInvalidFormat:
      return "kInvalidFormat";
    case CAPTURE::CaptureResult::kInternalError:
      return "kInternalError";
    case CAPTURE::CaptureResult::kNotFound:
      return "kNotFound";
  }
  return "unknown";
}

class CaptureRenderSmokeWidget final : public QOpenGLWidget {
public:
  explicit CaptureRenderSmokeWidget(QWidget *parent = nullptr)
      : QOpenGLWidget(parent) {
    setWindowTitle(QStringLiteral("MacBook Camera/Mic -> Render Smoke Test"));
    resize(960, 540);

    connect(&m_status_timer, &QTimer::timeout, this, [this]() {
      update();
    });

    connect(&m_tone_timer, &QTimer::timeout, this, [this]() {
      if (!m_render_engine) {
        return;
      }

      m_render_engine->PushAudioFrame(kUserName, MakeToneFrame(m_tone_phase));
    });
  }

  ~CaptureRenderSmokeWidget() override {
    m_status_timer.stop();
    m_tone_timer.stop();

    if (m_capture_engine) {
      m_capture_engine->UnInit();
      m_capture_engine.reset();
    }

    makeCurrent();
    if (m_render_engine) {
      m_render_engine->RemoveUser(kUserName);
      m_render_engine->Uninitialize();
      m_render_engine.reset();
    }
    doneCurrent();
  }

protected:
  void initializeGL() override {
    m_render_engine = RENDER::IRenderEngine::CreateRenderEngine();
    if (!m_render_engine ||
        !m_render_engine->Initialize(kSampleRate, kChannels, kVideoWidth, kVideoHeight) ||
        !m_render_engine->AddUser(kUserName, true)) {
      SetStatus("RenderEngine initialize failed");
      return;
    }

    m_capture_engine = CAPTURE::ICaptureEngine::Create();
    if (!m_capture_engine) {
      SetStatus("CaptureEngine create failed");
      return;
    }

    m_capture_engine->RegisterVideoCallback(
        [this](const std::shared_ptr<CAPTURE::I420Frame> &frame) {
          auto render_frame = ToRenderVideoFrame(frame);
          if (!render_frame || !m_render_engine) {
            return;
          }

          if (m_render_engine->PushVideoFrame(kUserName, render_frame)) {
            ++m_video_count;
            m_last_video_width = render_frame->width;
            m_last_video_height = render_frame->height;
            m_last_video_y_average = AverageYPlane(frame);
            QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
          }
        });

    m_capture_engine->RegisterAudioCallback(
        [this](const std::shared_ptr<CAPTURE::AudioFrame> &frame) {
          auto render_frame = ToRenderAudioFrame(frame);
          if (!render_frame || !m_render_engine) {
            return;
          }

          if (m_render_engine->PushAudioFrame(kUserName, render_frame)) {
            ++m_audio_count;
          }
        });

    const auto init_result =
        m_capture_engine->Init(kSampleRate, kChannels, kVideoWidth, kVideoHeight, kVideoFps);
    if (init_result != CAPTURE::CaptureResult::kSuccess) {
      SetStatus("CaptureEngine init failed: " + CaptureResultName(init_result));
      return;
    }

    std::vector<DeviceInfo> camera_devices;
    const auto enum_camera_result =
        m_capture_engine->EnumCameraDevices(
            [&](const char *device_id, const char *device_name) {
              camera_devices.push_back(DeviceInfo{
                  device_id ? device_id : "",
                  device_name ? device_name : ""});
              return true;
            });
    if (enum_camera_result != CAPTURE::CaptureResult::kSuccess) {
      SetStatus("Enum camera failed: " + CaptureResultName(enum_camera_result));
      return;
    }

    std::vector<DeviceInfo> mic_devices;
    const auto enum_mic_result =
        m_capture_engine->EnumMicDevices(
            [&](const char *device_id, const char *device_name) {
              mic_devices.push_back(DeviceInfo{
                  device_id ? device_id : "",
                  device_name ? device_name : ""});
              return true;
            });
    if (enum_mic_result != CAPTURE::CaptureResult::kSuccess) {
      SetStatus("Enum mic failed: " + CaptureResultName(enum_mic_result));
      return;
    }

    const DeviceInfo camera_device = SelectConcreteDevice(camera_devices);
    const DeviceInfo mic_device = SelectConcreteDevice(mic_devices);

    auto camera = camera_device.id.empty()
                      ? m_capture_engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera,
                                                       kCameraSourceName)
                      : m_capture_engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera,
                                                       kCameraSourceName,
                                                       camera_device.id);
    if (!camera) {
      SetStatus("Create camera source failed");
      return;
    }

    auto mic = mic_device.id.empty()
                   ? m_capture_engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone,
                                                    kMicSourceName)
                   : m_capture_engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone,
                                                    kMicSourceName,
                                                    mic_device.id);
    if (!mic) {
      SetStatus("Create mic source failed");
      return;
    }

    std::ostringstream status;
    status << "Capturing camera="
           << (camera_device.name.empty() ? "default" : camera_device.name)
           << " (" << (camera_device.id.empty() ? "no-id" : camera_device.id) << ")"
           << ", mic=" << (mic_device.name.empty() ? "default" : mic_device.name)
           << " (" << (mic_device.id.empty() ? "no-id" : mic_device.id) << ")";
    SetStatus(status.str());
    m_status_timer.start(250);
    m_tone_timer.start(10);
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
    m_last_render_result = rendered;

    QPainter painter(this);
    if (!rendered) {
      painter.fillRect(rect(), QColor(16, 18, 24));
    }

    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(QRect(12, 12, 720, 122), QColor(0, 0, 0, 170));
    painter.setPen(Qt::white);
    painter.drawText(QRect(24, 22, 650, 24), QString::fromStdString(Status()));
    painter.drawText(QRect(24, 50, 650, 24),
                     QStringLiteral("video frames: %1    audio frames: %2")
                         .arg(m_video_count.load())
                         .arg(m_audio_count.load()));
    painter.drawText(QRect(24, 76, 650, 20),
                     QStringLiteral("rendered: %1    frame: %2x%3    avg Y: %4")
                         .arg(m_last_render_result ? QStringLiteral("yes") : QStringLiteral("no"))
                         .arg(m_last_video_width.load())
                         .arg(m_last_video_height.load())
                         .arg(m_last_video_y_average.load(), 0, 'f', 1));
    painter.drawText(QRect(24, 100, 650, 20),
                     m_last_video_y_average.load() <= 20.0 && m_video_count.load() > 0
                         ? QStringLiteral("Captured video is near black. Try concrete camera device/permissions.")
                         : QStringLiteral("Mic is amplified; a quiet 440Hz tone is also mixed in."));
  }

private:
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
  std::unique_ptr<CAPTURE::ICaptureEngine> m_capture_engine;
  QTimer m_status_timer;
  QTimer m_tone_timer;
  std::atomic<int> m_video_count{0};
  std::atomic<int> m_audio_count{0};
  std::atomic<int> m_last_video_width{0};
  std::atomic<int> m_last_video_height{0};
  std::atomic<double> m_last_video_y_average{0.0};
  bool m_last_render_result = false;
  double m_tone_phase = 0.0;
  mutable std::mutex m_status_mutex;
  std::string m_status = "Initializing capture/render test...";
};

} // namespace

int main(int argc, char **argv) {
  QSurfaceFormat format;
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);

  CaptureRenderSmokeWidget widget;
  widget.show();

  return app.exec();
}
