#include "engine/private/audio/AudioProcessingPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 2;
constexpr int kFrameSamples = 480;

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }
  std::cout << "[PASSED] " << message << '\n';
  return true;
}

VCE::AudioFrame MakeFrame(const std::vector<float> &mono) {
  VCE::AudioFrame frame;
  frame.sample_rate = kSampleRate;
  frame.channels = kChannels;
  frame.samples = static_cast<int>(mono.size());
  frame.data = std::shared_ptr<uint8_t[]>(new uint8_t[frame.DataSize()]);

  auto *samples = reinterpret_cast<float *>(frame.data.get());
  for (size_t i = 0; i < mono.size(); ++i) {
    samples[i * 2] = mono[i];
    samples[i * 2 + 1] = mono[i];
  }
  return frame;
}

std::vector<float> ReadMono(const VCE::AudioFrame &frame) {
  std::vector<float> mono(static_cast<size_t>(frame.samples));
  const auto *samples = reinterpret_cast<const float *>(frame.data.get());
  for (int i = 0; i < frame.samples; ++i) {
    mono[static_cast<size_t>(i)] = samples[static_cast<size_t>(i) * 2];
  }
  return mono;
}

double Energy(const std::vector<float> &samples) {
  double energy = 0.0;
  for (float sample : samples) {
    energy += static_cast<double>(sample) * static_cast<double>(sample);
  }
  return energy / static_cast<double>(std::max<size_t>(samples.size(), 1));
}

std::vector<float> MakeFarEndFrame(uint32_t *state, float *previous) {
  std::vector<float> frame(kFrameSamples);
  for (float &sample : frame) {
    *state = *state * 1664525u + 1013904223u;
    const float noise =
        (static_cast<float>((*state >> 8) & 0xffffu) / 32768.0f) - 1.0f;
    *previous = 0.82f * *previous + 0.18f * noise;
    sample = *previous * 0.45f;
  }
  return frame;
}

std::vector<float> MakeNearEndFrame(uint32_t *state,
                                    float *previous,
                                    int frame_index) {
  std::vector<float> frame(kFrameSamples);
  const float envelope =
      0.45f + 0.55f * std::abs(std::sin(static_cast<float>(frame_index) * 0.17f));
  for (float &sample : frame) {
    *state = *state * 1103515245u + 12345u;
    const float noise =
        (static_cast<float>((*state >> 8) & 0xffffu) / 32768.0f) - 1.0f;
    *previous = 0.68f * *previous + 0.32f * noise;
    sample = *previous * 0.32f * envelope;
  }
  return frame;
}

bool CheckNoiseSuppression() {
  VCE::AudioProcessingPipeline pipeline;
  if (!pipeline.Initialize(kSampleRate, kChannels)) {
    return false;
  }
  pipeline.SetCaptureActive(true);

  // The speaker callback still supplies PCM when no one is speaking remotely.
  const std::vector<float> silent_playback(kFrameSamples * kChannels);
  const auto feed_silence = [&] {
    pipeline.FeedPlaybackReference(silent_playback.data(), silent_playback.size(),
                                   kSampleRate, kChannels);
  };

  uint32_t noise_state = 0x712c9ba3u;
  double noise_before = 0.0;
  double noise_after = 0.0;
  double mixed_noise_before = 0.0;
  double voice_energy = 0.0;
  double output_energy = 0.0;
  double voiced_output_energy = 0.0;
  constexpr double kTau = 6.283185307179586;
  constexpr std::array<double, 3> kFrequencies{200.0, 600.0, 1200.0};
  constexpr std::array<float, 3> kAmplitudes{0.14f, 0.07f, 0.04f};
  std::array<std::array<double, kFrameSamples>, 6> voice_basis{};
  std::vector<float> voice(kFrameSamples);
  for (int i = 0; i < kFrameSamples; ++i) {
    for (size_t h = 0; h < kFrequencies.size(); ++h) {
      const double phase = kTau * kFrequencies[h] * i / kSampleRate;
      voice_basis[2 * h][i] = std::sin(phase);
      voice_basis[2 * h + 1][i] = std::cos(phase);
      voice[i] += static_cast<float>(kAmplitudes[h] * voice_basis[2 * h][i]);
    }
  }
  for (int block = 0; block < 430; ++block) {
    const bool has_voice = block >= 300;
    std::vector<float> noise(kFrameSamples);
    std::vector<float> input(kFrameSamples);
    for (int i = 0; i < kFrameSamples; ++i) {
      noise_state = noise_state * 1664525u + 1013904223u;
      noise[i] = 0.05f * (static_cast<float>(noise_state >> 16) / 32768.0f - 1.0f);
      input[i] = noise[i] + (has_voice ? voice[i] : 0.0f);
    }

    feed_silence();
    const auto processed = pipeline.ProcessCaptureFrame(MakeFrame(input));
    if (!processed || !processed->HasData() || processed->samples != kFrameSamples) {
      return false;
    }
    const auto output = ReadMono(*processed);
    if (block >= 200 && block < 300) {
      noise_before += Energy(noise);
      noise_after += Energy(output);
    } else if (block >= 320) {
      mixed_noise_before += Energy(noise) * kFrameSamples;
      voice_energy += Energy(voice) * kFrameSamples;
      output_energy += Energy(output) * kFrameSamples;
      // Measure the voiced harmonics independently of filter phase and delay.
      for (const auto &basis : voice_basis) {
        double projection = 0.0;
        for (int i = 0; i < kFrameSamples; ++i) {
          projection += basis[i] * output[i];
        }
        voiced_output_energy += projection * projection / (kFrameSamples / 2);
      }
    }
  }

  if (noise_before == 0.0 || voice_energy == 0.0 || mixed_noise_before == 0.0) {
    return false;
  }
  const double voice_gain = std::sqrt(voiced_output_energy / voice_energy);
  const double residual_energy = output_energy - voiced_output_energy;
  std::cout << "noise_only_energy_ratio=" << noise_after / noise_before
            << ", voiced_component_gain=" << voice_gain
            << ", mixed_residual_energy_ratio=" << residual_energy / mixed_noise_before
            << '\n';
  bool passed = Check(noise_after < noise_before * 0.35,
                      "stationary background noise is reduced without a far-end signal");
  passed &= Check(voice_gain > 0.50,
                  "denoising preserves the voiced component");
  passed &= Check(residual_energy < mixed_noise_before * 0.80,
                  "background is attenuated while voice is present");

  pipeline.SetCaptureActive(false);
  pipeline.SetCaptureActive(true);
  const auto silence = MakeFrame(std::vector<float>(kFrameSamples));
  for (int block = 0; block < 4; ++block) {
    feed_silence();
    const auto processed = pipeline.ProcessCaptureFrame(silence);
    if (!processed || !processed->HasData() || Energy(ReadMono(*processed)) > 1e-8) {
      return Check(false, "reopening the microphone clears prior PCM history");
    }
  }
  Check(true, "reopening the microphone clears prior PCM history");
  return passed;
}


bool CheckBatchedPlaybackReference() {
  constexpr int kBatchFrames = 4;
  constexpr int kNearVoiceStartFrame = 400;
  constexpr float kNearVoiceAmplitude = 0.060f;
  VCE::AudioProcessingPipeline pipeline;
  if (!pipeline.Initialize(kSampleRate, kChannels)) {
    return Check(false, "batched playback test initializes");
  }
  pipeline.SetCaptureActive(true);

  uint32_t random_state = 0x13572468u;
  float previous = 0.0f;
  std::vector<std::vector<float>> history;
  double input_energy = 0.0;
  double output_energy = 0.0;
  double startup_input_energy = 0.0;
  double startup_output_energy = 0.0;
  double near_voice_energy = 0.0;
  int near_voice_frames = 0;
  std::array<double, kFrameSamples> voice_sine{};
  std::array<double, kFrameSamples> voice_cosine{};
  for (int i = 0; i < kFrameSamples; ++i) {
    const double phase = 2.0 * 3.141592653589793 * 300.0 * i / kSampleRate;
    voice_sine[i] = std::sin(phase);
    voice_cosine[i] = std::cos(phase);
  }
  for (int frame_index = 0; frame_index < 500; ++frame_index) {
    if (frame_index % kBatchFrames == 0) {
      std::vector<float> batch;
      for (int block = 0; block < kBatchFrames; ++block) {
        const auto frame = MakeFarEndFrame(&random_state, &previous);
        batch.insert(batch.end(), frame.begin(), frame.end());
        history.push_back(frame);
      }
      const auto reference = MakeFrame(batch);
      pipeline.FeedPlaybackReference(reinterpret_cast<const float *>(reference.data.get()),
                                     batch.size() * kChannels, kSampleRate, kChannels);
    }

    std::vector<float> microphone(kFrameSamples, 0.0f);
    if (frame_index >= 13) {
      const auto &direct = history[static_cast<size_t>(frame_index - 6)];
      const auto &reflection = history[static_cast<size_t>(frame_index - 13)];
      const float gain = frame_index % 83 < 40 ? 0.55f : 0.35f;
      for (size_t i = 0; i < microphone.size(); ++i) {
        microphone[i] = std::clamp(direct[i] * gain, -0.065f, 0.065f) + reflection[i] * 0.2f;
      }
    }
    if (frame_index >= kNearVoiceStartFrame) {
      for (int i = 0; i < kFrameSamples; ++i) {
        microphone[i] += kNearVoiceAmplitude * voice_sine[i];
      }
    }
    const auto processed = pipeline.ProcessCaptureFrame(MakeFrame(microphone));
    if (!processed || !processed->HasData()) {
      return Check(false, "capture continues with batched playback");
    }
    if (frame_index >= 15 && frame_index < 90) {
      startup_input_energy += Energy(microphone);
      startup_output_energy += Energy(ReadMono(*processed));
    } else if (frame_index >= 300 && frame_index < kNearVoiceStartFrame) {
      input_energy += Energy(microphone);
      output_energy += Energy(ReadMono(*processed));
    } else if (frame_index >= kNearVoiceStartFrame + 16) {
      const auto output = ReadMono(*processed);
      double sine_projection = 0.0;
      double cosine_projection = 0.0;
      for (int i = 0; i < kFrameSamples; ++i) {
        sine_projection += output[i] * voice_sine[i];
        cosine_projection += output[i] * voice_cosine[i];
      }
      near_voice_energy += 2.0 * (sine_projection * sine_projection +
                                 cosine_projection * cosine_projection) /
                           (kFrameSamples * kFrameSamples);
      ++near_voice_frames;
    }
  }
  const double near_voice_input_energy =
      0.5 * kNearVoiceAmplitude * kNearVoiceAmplitude * near_voice_frames;
  const double near_voice_gain = std::sqrt(near_voice_energy / near_voice_input_energy);
  std::cout << "batched_playback_echo_energy_ratio="
            << output_energy / std::max(input_energy, 1e-12)
            << ", startup_echo_energy_ratio="
            << startup_output_energy / std::max(startup_input_energy, 1e-12)
            << ", near_voice_gain=" << near_voice_gain << '\n';
  bool passed = Check(input_energy > 0.0 && output_energy < input_energy * 0.0005,
                      "quiet residual echo is attenuated with batched playback");
  passed &= Check(startup_input_energy > 0.0 && startup_output_energy < startup_input_energy * 0.20,
                  "echo is attenuated before the adaptive filter has settled");
  passed &= Check(near_voice_gain > 0.80,
                  "clear near-end signal survives simultaneous far-end playback");
  return passed;
}

} // namespace

int main() {
  bool passed = true;
  VCE::AudioProcessingPipeline pipeline;
  const auto passthrough_input = MakeFrame(std::vector<float>(kFrameSamples, 0.1f));
  passed &= Check(!pipeline.ProcessCaptureFrame(passthrough_input),
                  "uninitialized pipeline leaves the original frame to the caller");

  passed &= Check(pipeline.Initialize(kSampleRate, kChannels),
                  "audio processing pipeline initializes");
  passed &= Check(!pipeline.ProcessCaptureFrame(passthrough_input),
                  "inactive pipeline leaves the original frame to the caller");

  pipeline.SetCaptureActive(true);
  auto other_rate = passthrough_input;
  other_rate.sample_rate = 44100;
  passed &= Check(!pipeline.ProcessCaptureFrame(other_rate),
                  "unsupported sample rate bypasses processing");
  auto other_channels = passthrough_input;
  other_channels.channels = 1;
  passed &= Check(!pipeline.ProcessCaptureFrame(other_channels),
                  "unsupported channel count bypasses processing");

  uint32_t random_state = 0x13572468u;
  uint32_t near_end_random_state = 0x24681357u;
  float previous = 0.0f;
  float near_end_previous = 0.0f;
  std::vector<std::vector<float>> playback_history;
  double input_echo_energy = 0.0;
  double output_echo_energy = 0.0;
  double double_talk_output_energy = 0.0;
  double near_end_energy = 0.0;

  for (int frame_index = 0; frame_index < 500; ++frame_index) {
    const auto playback = MakeFarEndFrame(&random_state, &previous);
    const auto playback_frame = MakeFrame(playback);
    pipeline.FeedPlaybackReference(
        reinterpret_cast<const float *>(playback_frame.data.get()),
        static_cast<size_t>(playback_frame.samples * playback_frame.channels),
        playback_frame.sample_rate,
        playback_frame.channels);
    playback_history.push_back(playback);

    std::vector<float> microphone(kFrameSamples, 0.0f);
    if (frame_index >= 2) {
      const auto &echo = playback_history[static_cast<size_t>(frame_index - 2)];
      for (size_t i = 0; i < microphone.size(); ++i) {
        microphone[i] = echo[i] * 0.55f;
      }
    }

    const bool double_talk = frame_index >= 400;
    std::vector<float> near_end(kFrameSamples, 0.0f);
    if (double_talk) {
      near_end = MakeNearEndFrame(&near_end_random_state,
                                 &near_end_previous,
                                 frame_index);
      for (size_t i = 0; i < microphone.size(); ++i) {
        microphone[i] += near_end[i];
      }
    }

    const auto input_frame = MakeFrame(microphone);
    const auto processed = pipeline.ProcessCaptureFrame(input_frame);
    if (!processed || !processed->HasData()) {
      passed &= Check(false, "capture frames are processed");
      break;
    }

    if (frame_index >= 300 && frame_index < 400) {
      input_echo_energy += Energy(microphone);
      output_echo_energy += Energy(ReadMono(*processed));
    } else if (double_talk) {
      near_end_energy += Energy(near_end);
      double_talk_output_energy += Energy(ReadMono(*processed));
    }
  }

  passed &= Check(input_echo_energy > 0.0 && near_end_energy > 0.0,
                  "synthetic echo and double-talk signals are present");
  std::cout << "echo_energy_ratio="
            << output_echo_energy / std::max(input_echo_energy, 1e-12)
            << ", double_talk_energy_ratio="
            << double_talk_output_energy / std::max(near_end_energy, 1e-12)
            << '\n';
  passed &= Check(output_echo_energy < input_echo_energy * 0.45,
                  "far-end acoustic echo is substantially reduced");
  passed &= Check(double_talk_output_energy > near_end_energy * 0.12,
                  "near-end speech remains present during double talk");

  pipeline.SetCaptureActive(false);
  passed &= Check(!pipeline.ProcessCaptureFrame(passthrough_input),
                  "stopping capture restores bypass");
  pipeline.Uninitialize();
  passed &= CheckNoiseSuppression();
  passed &= CheckBatchedPlaybackReference();
  return passed ? 0 : 1;
}
