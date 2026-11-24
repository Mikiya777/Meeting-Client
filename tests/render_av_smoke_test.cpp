#include "render/include/IRenderEngine.h"

#include <QApplication>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr const char *kUserName = "render-smoke-user";
constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kAudioFrameSamples = 480;
constexpr double kToneHz = 440.0;
constexpr double kPi = 3.14159265358979323846;

int64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

uint8_t ClampByte(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return static_cast<uint8_t>(value);
}

std::shared_ptr<RENDER::I420Frame> MakeVideoFrame(int frame_index) {
  auto frame = std::make_shared<RENDER::I420Frame>();
  frame->width = kVideoWidth;
  frame->height = kVideoHeight;
  frame->timestamp_us = NowUs();

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);

  for (int row = 0; row < kVideoHeight; ++row) {
    for (int column = 0; column < kVideoWidth; ++column) {
      const int value = 96 +
                        static_cast<int>(48.0 * std::sin((column + frame_index * 6) * 0.025)) +
                        static_cast<int>(24.0 * std::cos((row + frame_index * 4) * 0.035));
      frame->data[0][static_cast<size_t>(row) * kVideoWidth + column] =
          ClampByte(value);
    }
  }

  const uint8_t u = ClampByte(80 + static_cast<int>(50.0 * std::sin(frame_index * 0.08)));
  const uint8_t v = ClampByte(150 + static_cast<int>(50.0 * std::cos(frame_index * 0.07)));
  std::memset(frame->data[1].get(), u, uv_size);
  std::memset(frame->data[2].get(), v, uv_size);
  return frame;
}

std::shared_ptr<RENDER::AudioFrame> MakeAudioFrame(double &phase, int64_t timestamp_us) {
  auto frame = std::make_shared<RENDER::AudioFrame>();
  frame->sample_rate = kSampleRate;
  frame->channels = kChannels;
  frame->samples_per_channel = kAudioFrameSamples;
  frame->timestamp_us = timestamp_us;

  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  auto *samples = reinterpret_cast<float *>(frame->data.get());
  const double phase_step = 2.0 * kPi * kToneHz / static_cast<double>(kSampleRate);

  for (int i = 0; i < kAudioFrameSamples; ++i) {
    const float sample = static_cast<float>(0.18 * std::sin(phase));
    for (int channel = 0; channel < kChannels; ++channel) {
      samples[i * kChannels + channel] = sample;
    }

    phase += phase_step;
    if (phase >= 2.0 * kPi) {
      phase -= 2.0 * kPi;
    }
  }

  return frame;
}

class RenderSmokeWidget final : public QOpenGLWidget {
public:
  explicit RenderSmokeWidget(QWidget *parent = nullptr)
      : QOpenGLWidget(parent) {
    setWindowTitle(QStringLiteral("Render AV Smoke Test"));
    resize(960, 540);

    connect(&m_video_timer, &QTimer::timeout, this, [this]() {
      if (!m_engine) {
        return;
      }

      m_engine->PushVideoFrame(kUserName, MakeVideoFrame(m_frame_index++));
      update();
    });

    connect(&m_audio_timer, &QTimer::timeout, this, [this]() {
      if (!m_engine) {
        return;
      }

      const int64_t now_us = NowUs();
      for (int i = 0; i < 4; ++i) {
        const int64_t timestamp_us =
            now_us + i * kAudioFrameSamples * 1000 * 1000 / kSampleRate;
        m_engine->PushAudioFrame(kUserName, MakeAudioFrame(m_audio_phase, timestamp_us));
      }
    });
  }

  ~RenderSmokeWidget() override {
    makeCurrent();
    if (m_engine) {
      m_engine->RemoveUser(kUserName);
      m_engine->Uninitialize();
      m_engine.reset();
    }
    doneCurrent();
  }

protected:
  void initializeGL() override {
    m_engine = RENDER::IRenderEngine::CreateRenderEngine();
    if (!m_engine ||
        !m_engine->Initialize(kSampleRate, kChannels, kVideoWidth, kVideoHeight) ||
        !m_engine->AddUser(kUserName, true)) {
      return;
    }

    m_video_timer.start(33);
    m_audio_timer.start(20);
  }

  void resizeGL(int width, int height) override {
    if (m_engine) {
      m_engine->UpdateUserVideoSize(kUserName, width, height);
    }
  }

  void paintGL() override {
    if (m_engine) {
      m_engine->RenderUser(kUserName);
    }
  }

private:
  std::unique_ptr<RENDER::IRenderEngine> m_engine;
  QTimer m_video_timer;
  QTimer m_audio_timer;
  int m_frame_index = 0;
  double m_audio_phase = 0.0;
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

  RenderSmokeWidget widget;
  widget.show();

  return app.exec();
}
