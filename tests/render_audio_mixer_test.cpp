#include "render/src/audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kStartupFrames = kSampleRate * 120 / 1000;

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

std::shared_ptr<RENDER::AudioFrame> MakeAudioFrame(int sample_rate,
                                                   int channels,
                                                   const std::vector<float> &samples) {
  auto frame = std::make_shared<RENDER::AudioFrame>();
  frame->sample_rate = sample_rate;
  frame->channels = channels;
  frame->samples_per_channel = static_cast<int>(samples.size() / channels);
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->DataSize()]);
  std::memcpy(frame->data.get(), samples.data(), frame->DataSize());
  return frame;
}

bool AlmostEqual(float lhs, float rhs) {
  return std::abs(lhs - rhs) < 0.0001f;
}

std::shared_ptr<RENDER::AudioFrame> MakeToneFrame(int frame_count, float amplitude) {
  std::vector<float> samples(static_cast<size_t>(frame_count) * kChannels);
  for (int i = 0; i < frame_count; ++i) {
    const float sample = i % 2 == 0 ? amplitude : -amplitude;
    samples[i * kChannels] = sample;
    samples[i * kChannels + 1] = -sample;
  }
  return MakeAudioFrame(kSampleRate, kChannels, samples);
}

bool CheckTrimAcrossSegments() {
  constexpr int kRetainedFrames = kSampleRate * 2;
  constexpr int kFirstFrames = 40000;
  constexpr int kSecondFrames = 40000;
  constexpr int kThirdFrames = 72000;
  constexpr int kEvictedFrames =
      kFirstFrames + kSecondFrames + kThirdFrames - kRetainedFrames;

  RENDER::AudioMixer trimmed;
  RENDER::AudioMixer reference;
  const auto first = MakeToneFrame(kFirstFrames, 0.05f);
  const auto second = MakeToneFrame(kSecondFrames, 0.20f);
  const auto third = MakeToneFrame(kThirdFrames, 0.35f);

  if (!trimmed.Initialize(kSampleRate, kChannels) ||
      !reference.Initialize(kSampleRate, kChannels) ||
      !trimmed.PushAudioFrame("speaker", first) ||
      !trimmed.PushAudioFrame("speaker", second) ||
      !trimmed.PushAudioFrame("speaker", third) ||
      !reference.PushAudioFrame("speaker", first) ||
      !reference.PushAudioFrame("speaker", second)) {
    return false;
  }

  // Read the same prefix that should be evicted before queuing the third
  // segment, so the reference never exceeds the buffer limit.
  std::vector<float> prefix(kEvictedFrames * kChannels);
  if (reference.PopAudio(prefix.data(), kEvictedFrames) != kEvictedFrames ||
      !reference.PushAudioFrame("speaker", third)) {
    return false;
  }

  std::vector<float> actual(kRetainedFrames * kChannels);
  std::vector<float> expected(kRetainedFrames * kChannels);
  return trimmed.PopAudio(actual.data(), kRetainedFrames) == kRetainedFrames &&
         reference.PopAudio(expected.data(), kRetainedFrames) == kRetainedFrames &&
         std::abs(expected.front()) > 0.01f &&
         std::equal(actual.begin(), actual.end(), expected.begin(), AlmostEqual);
}

} // namespace

int main() {
  bool passed = true;

  RENDER::AudioMixer mixer;
  passed &= Check(!mixer.PushAudioFrame("before-init",
                                        MakeAudioFrame(48000, 2, {0.1f, 0.1f})),
                  "AudioMixer rejects frames before initialization");
  passed &= Check(mixer.Initialize(48000, 2), "AudioMixer initializes");
  passed &= Check(!mixer.PushAudioFrame("", MakeAudioFrame(48000, 2, {0.1f, 0.1f})),
                  "AudioMixer rejects empty user name");
  passed &= Check(!mixer.PushAudioFrame("null-frame", nullptr),
                  "AudioMixer rejects null frames");
  passed &= Check(!mixer.PushAudioFrame("bad-rate",
                                        MakeAudioFrame(44100, 2, {0.1f, 0.1f})),
                  "AudioMixer rejects mismatched sample rate");
  passed &= Check(!mixer.PushAudioFrame("bad-channels",
                                        MakeAudioFrame(48000, 1, {0.1f, 0.1f})),
                  "AudioMixer rejects mismatched channel count");

  passed &= Check(mixer.PushAudioFrame("startup", MakeToneFrame(kStartupFrames - 2, 0.2f)),
                  "AudioMixer buffers incoming samples before playback");
  float startup[4] = {};
  passed &= Check(mixer.PopAudio(startup, 2) == 2 &&
                      std::all_of(std::begin(startup), std::end(startup),
                                  [](float sample) { return sample == 0.0f; }),
                  "output stays silent below the jitter buffer threshold");
  passed &= Check(mixer.PushAudioFrame("startup", MakeToneFrame(2, 0.2f)) &&
                      mixer.PopAudio(startup, 2) == 2 && startup[0] > 0.1f,
                  "playback begins at the jitter buffer threshold");
  mixer.RemoveUser("startup");

  const auto tone = MakeToneFrame(kStartupFrames, 0.25f);
  float single[4] = {};
  passed &= Check(mixer.PushAudioFrame("alice", tone) &&
                      mixer.PopAudio(single, 2) == 2 && single[0] > 0.1f,
                  "a buffered source produces audible output");
  mixer.RemoveUser("alice");

  float mixed[4] = {};
  passed &= Check(mixer.PushAudioFrame("alice", tone) &&
                      mixer.PushAudioFrame("bob", tone) &&
                      mixer.PopAudio(mixed, 2) == 2 &&
                      AlmostEqual(mixed[0], single[0] * std::sqrt(2.0f)),
                  "multiple sources are mixed with active-source gain");
  mixer.RemoveUser("alice");
  mixer.RemoveUser("bob");

  const auto loud = MakeToneFrame(kStartupFrames, 2.0f);
  float limited[2] = {};
  passed &= Check(mixer.PushAudioFrame("loud-a", loud) &&
                      mixer.PushAudioFrame("loud-b", loud) &&
                      mixer.PopAudio(limited, 1) == 1 &&
                      AlmostEqual(limited[0], 1.0f) &&
                      AlmostEqual(limited[1], -1.0f),
                  "loud mixed output is peak-limited");

  passed &= Check(CheckTrimAcrossSegments(),
                  "overflow keeps the newest two seconds across whole and partial segments");

  mixer.Uninitialize();
  return passed ? 0 : 1;
}
