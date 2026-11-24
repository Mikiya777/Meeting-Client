#include "render/include/IRenderEngine.h"
#include "render/src/video/UserContext.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

std::shared_ptr<RENDER::I420Frame> MakeVideoFrame(int width, int height) {
  auto frame = std::make_shared<RENDER::I420Frame>();
  frame->width = width;
  frame->height = height;
  if (width <= 0 || height <= 0) {
    return frame;
  }

  const size_t y_size = frame->GetYPlaneSize();
  const size_t uv_size = frame->GetUVPlaneSize();
  frame->data[0] = std::shared_ptr<uint8_t[]>(new uint8_t[y_size]);
  frame->data[1] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  frame->data[2] = std::shared_ptr<uint8_t[]>(new uint8_t[uv_size]);
  return frame;
}

std::shared_ptr<RENDER::AudioFrame> MakeAudioFrame(int sample_rate,
                                                   int channels,
                                                   int samples_per_channel) {
  auto frame = std::make_shared<RENDER::AudioFrame>();
  frame->sample_rate = sample_rate;
  frame->channels = channels;
  frame->samples_per_channel = samples_per_channel;
  if (sample_rate <= 0 || channels <= 0 || samples_per_channel <= 0) {
    return frame;
  }

  std::vector<float> samples(static_cast<size_t>(channels) *
                             static_cast<size_t>(samples_per_channel),
                             0.1f);
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memcpy(frame->data.get(), samples.data(), frame->DataSize());
  return frame;
}

} // namespace

int main() {
  bool passed = true;

  passed &= Check(MakeVideoFrame(640, 360)->HasData(),
                  "I420Frame accepts valid even dimensions");
  passed &= Check(!MakeVideoFrame(641, 360)->HasData(),
                  "I420Frame rejects odd width");
  passed &= Check(!MakeVideoFrame(640, 361)->HasData(),
                  "I420Frame rejects odd height");
  passed &= Check(!MakeVideoFrame(0, 360)->HasData(),
                  "I420Frame rejects zero width");

  passed &= Check(MakeAudioFrame(48000, 2, 480)->HasData(),
                  "AudioFrame accepts valid float PCM metadata");
  passed &= Check(!MakeAudioFrame(0, 2, 480)->HasData(),
                  "AudioFrame rejects zero sample rate");
  passed &= Check(!MakeAudioFrame(48000, 0, 480)->HasData(),
                  "AudioFrame rejects zero channels");
  passed &= Check(!MakeAudioFrame(48000, 2, 0)->HasData(),
                  "AudioFrame rejects zero samples");

  RENDER::UserContext user("video-buffer-test", false);
  auto first = MakeVideoFrame(640, 360);
  first->timestamp_us = 100;
  std::weak_ptr<RENDER::I420Frame> previous = first;
  user.PushFrame(first);
  first.reset();
  passed &= Check(!previous.expired(), "video buffer retains its pending frame");

  auto latest = MakeVideoFrame(640, 360);
  latest->timestamp_us = 200;
  user.PushFrame(latest);
  passed &= Check(previous.expired(), "newer video frame releases the previous candidate");

  auto late = MakeVideoFrame(640, 360);
  late->timestamp_us = 150;
  std::weak_ptr<RENDER::I420Frame> discarded = late;
  user.PushFrame(late);
  late.reset();
  passed &= Check(discarded.expired(), "out-of-order older video frame is not retained");

  previous = latest;
  latest.reset();
  auto same_timestamp = MakeVideoFrame(640, 360);
  same_timestamp->timestamp_us = 200;
  user.PushFrame(same_timestamp);
  passed &= Check(previous.expired(), "latest arrival replaces a pending frame with the same timestamp");
  previous = same_timestamp;
  same_timestamp.reset();
  user.Uninitialize();
  passed &= Check(previous.expired(), "video buffer releases the pending frame on shutdown");

  auto engine = RENDER::IRenderEngine::CreateRenderEngine();
  passed &= Check(engine != nullptr, "RenderEngine factory returns an instance");
  passed &= Check(!engine->Initialize(0, 2, 640, 360),
                  "RenderEngine rejects invalid sample rate before touching devices");
  passed &= Check(!engine->Initialize(48000, 0, 640, 360),
                  "RenderEngine rejects invalid channel count before touching devices");
  passed &= Check(!engine->Initialize(48000, 2, 0, 360),
                  "RenderEngine rejects invalid video width before touching devices");
  passed &= Check(!engine->Initialize(48000, 2, 641, 360),
                  "RenderEngine rejects odd video width before touching devices");
  passed &= Check(!engine->PushAudioFrame("user", MakeAudioFrame(48000, 2, 480)),
                  "RenderEngine rejects audio push before initialization");
  passed &= Check(!engine->PushVideoFrame("user", MakeVideoFrame(640, 360)),
                  "RenderEngine rejects video push before initialization/user registration");
  passed &= Check(!engine->RenderUser("user"),
                  "RenderEngine rejects render before user registration");
  passed &= Check(!engine->UpdateUserVideoSize("user", 640, 360),
                  "RenderEngine rejects resize before user registration");

  std::string speaker_id;
  passed &= Check(!engine->GetCurrentAudioSpeaker(speaker_id),
                  "RenderEngine reports no current speaker before initialization");

  return passed ? 0 : 1;
}
