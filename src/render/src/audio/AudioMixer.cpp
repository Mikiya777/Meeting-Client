#include "render/src/audio/AudioMixer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace RENDER {
namespace {

constexpr int kMaxBufferedAudioMilliseconds = 2000;
constexpr int kJitterBufferStartMilliseconds = 120;
constexpr int kMaxConcealmentMilliseconds = 60;
constexpr int64_t kAudioLateToleranceUs = 80 * 1000;
constexpr float kHighPassAlpha = 0.995f;
constexpr float kNoiseGateClosedRms = 0.003f;
constexpr float kNoiseGateOpenRms = 0.012f;
constexpr float kAgcTargetRms = 0.08f;
constexpr float kAgcMinGain = 0.7f;
constexpr float kAgcMaxGain = 2.2f;
constexpr float kAgcSmoothing = 0.08f;
constexpr float kPlcDecay = 0.90f;

int64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

int64_t FramesToDurationUs(size_t frame_count, int sample_rate) {
  if (sample_rate <= 0) {
    return 0;
  }

  return static_cast<int64_t>(frame_count) * 1000 * 1000 /
         static_cast<int64_t>(sample_rate);
}

} // namespace

AudioMixer::AudioMixer() = default;

AudioMixer::~AudioMixer() {
  Uninitialize();
}

bool AudioMixer::Initialize(int sample_rate, int channels) {
  if (sample_rate <= 0 || channels <= 0) {
    return false;
  }

  m_sample_rate = sample_rate;
  m_channels = channels;
  m_initialized = true;
  return true;
}

void AudioMixer::Uninitialize() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_user_buffers.clear();
  m_initialized = false;
}

bool AudioMixer::PushAudioFrame(const std::string &user_name, const std::shared_ptr<AudioFrame> &frame) {
  if (user_name.empty() || !frame || !frame->HasData()) {
    return false;
  }

  auto samples = ConvertFrameToFloat(frame);
  if (samples.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return false;
  }

  if (frame->sample_rate != m_sample_rate || frame->channels != m_channels) {
    return false;
  }

  auto &buffer = m_user_buffers[user_name];
  ApplyInputProcessing(buffer, samples);

  AudioSegment segment;
  // Remote RTP timestamps are not aligned to the speaker callback clock yet.
  // Play queued audio in arrival order through a small jitter buffer.
  segment.start_timestamp_us = 0;
  segment.samples.insert(segment.samples.end(), samples.begin(), samples.end());

  buffer.segments.push_back(std::move(segment));
  TrimUserBuffer(buffer);
  return true;
}

void AudioMixer::RemoveUser(const std::string &user_name) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_user_buffers.erase(user_name);
}

uint32_t AudioMixer::PopAudio(float *output, uint32_t frame_count) {
  if (!output || frame_count == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || m_channels <= 0) {
    return 0;
  }

  const size_t sample_count =
      static_cast<size_t>(frame_count) * static_cast<size_t>(m_channels);
  std::memset(output, 0, sample_count * sizeof(float));

  size_t active_source_count = 0;
  const int64_t output_start_timestamp_us = NowUs();

  for (auto &[user_name, buffer] : m_user_buffers) {
    if (MixUserBuffer(buffer, output, frame_count, output_start_timestamp_us) > 0) {
      ++active_source_count;
    }
  }

  ApplyOutputGainAndLimit(output, sample_count, active_source_count);
  return frame_count;
}

std::vector<float> AudioMixer::ConvertFrameToFloat(const std::shared_ptr<AudioFrame> &frame) {
  if (!frame || !frame->HasData()) {
    return {};
  }

  const size_t sample_count = static_cast<size_t>(frame->samples_per_channel) *
                              static_cast<size_t>(frame->channels);
  std::vector<float> samples(sample_count);
  std::memcpy(samples.data(), frame->data.get(), sample_count * sizeof(float));
  return samples;
}

void AudioMixer::ApplyOutputGainAndLimit(float *samples,
                                         size_t sample_count,
                                         size_t active_source_count) {
  if (!samples || sample_count == 0 || active_source_count == 0) {
    return;
  }

  if (active_source_count > 1) {
    const float source_gain =
        1.0f / std::sqrt(static_cast<float>(active_source_count));
    for (size_t i = 0; i < sample_count; ++i) {
      samples[i] *= source_gain;
    }
  }

  float max_abs = 0.0f;
  for (size_t i = 0; i < sample_count; ++i) {
    max_abs = std::max(max_abs, std::abs(samples[i]));
  }

  if (max_abs <= 1.0f) {
    return;
  }

  const float scale = 1.0f / max_abs;
  for (size_t i = 0; i < sample_count; ++i) {
    samples[i] *= scale;
  }
}

size_t AudioMixer::MaxBufferedSampleCount() const {
  if (m_sample_rate <= 0 || m_channels <= 0) {
    return 0;
  }

  return static_cast<size_t>(m_sample_rate) *
         static_cast<size_t>(m_channels) *
         static_cast<size_t>(kMaxBufferedAudioMilliseconds) / 1000;
}

size_t AudioMixer::JitterStartSampleCount() const {
  if (m_sample_rate <= 0 || m_channels <= 0) {
    return 0;
  }

  return static_cast<size_t>(m_sample_rate) *
         static_cast<size_t>(m_channels) *
         static_cast<size_t>(kJitterBufferStartMilliseconds) / 1000;
}

size_t AudioMixer::MaxConcealedSampleCount() const {
  if (m_sample_rate <= 0 || m_channels <= 0) {
    return 0;
  }

  return static_cast<size_t>(m_sample_rate) *
         static_cast<size_t>(m_channels) *
         static_cast<size_t>(kMaxConcealmentMilliseconds) / 1000;
}

size_t AudioMixer::LastPlayedSampleCount() const {
  if (m_sample_rate <= 0 || m_channels <= 0) {
    return 0;
  }

  return static_cast<size_t>(m_sample_rate) *
         static_cast<size_t>(m_channels) / 50;
}

void AudioMixer::TrimUserBuffer(UserAudioBuffer &buffer) const {
  const size_t max_sample_count = MaxBufferedSampleCount();
  if (max_sample_count == 0) {
    buffer.segments.clear();
    return;
  }

  size_t buffered_samples = TotalBufferedSampleCount(buffer);
  while (buffered_samples > max_sample_count) {
    auto &segment = buffer.segments.front();
    const size_t excess = buffered_samples - max_sample_count;
    if (segment.samples.size() > excess) {
      DropSamplesFromSegment(segment, excess);
      break;
    }

    buffered_samples -= segment.samples.size();
    buffer.segments.pop_front();
  }
}

size_t AudioMixer::TotalBufferedSampleCount(const UserAudioBuffer &buffer) const {
  size_t sample_count = 0;
  for (const auto &segment : buffer.segments) {
    sample_count += segment.samples.size();
  }
  return sample_count;
}

void AudioMixer::DropSamplesFromSegment(AudioSegment &segment, size_t sample_count) const {
  const size_t actual_sample_count = std::min(sample_count, segment.samples.size());
  for (size_t i = 0; i < actual_sample_count; ++i) {
    segment.samples.pop_front();
  }

  if (segment.start_timestamp_us > 0 && m_channels > 0) {
    const size_t dropped_frame_count =
        actual_sample_count / static_cast<size_t>(m_channels);
    segment.start_timestamp_us += FramesToDurationUs(dropped_frame_count, m_sample_rate);
  }
}

void AudioMixer::ApplyInputProcessing(UserAudioBuffer &buffer, std::vector<float> &samples) const {
  if (samples.empty() || m_channels <= 0) {
    return;
  }

  const size_t channels = static_cast<size_t>(m_channels);
  if (buffer.high_pass_previous_input.size() != channels) {
    buffer.high_pass_previous_input.assign(channels, 0.0f);
    buffer.high_pass_previous_output.assign(channels, 0.0f);
  }

  double energy = 0.0;
  for (size_t i = 0; i < samples.size(); ++i) {
    const size_t channel = i % channels;
    const float input = samples[i];
    const float filtered =
        input - buffer.high_pass_previous_input[channel] +
        kHighPassAlpha * buffer.high_pass_previous_output[channel];
    buffer.high_pass_previous_input[channel] = input;
    buffer.high_pass_previous_output[channel] = filtered;
    samples[i] = filtered;
    energy += static_cast<double>(filtered) * static_cast<double>(filtered);
  }

  const float rms = static_cast<float>(std::sqrt(energy /
      static_cast<double>(samples.size())));

  float gate_gain = 1.0f;
  if (rms <= kNoiseGateClosedRms) {
    gate_gain = 0.08f;
  } else if (rms < kNoiseGateOpenRms) {
    const float t =
        (rms - kNoiseGateClosedRms) /
        (kNoiseGateOpenRms - kNoiseGateClosedRms);
    gate_gain = 0.08f + 0.92f * t;
  }

  float desired_gain = 1.0f;
  if (rms >= kNoiseGateOpenRms) {
    desired_gain = std::clamp(kAgcTargetRms / rms, kAgcMinGain, kAgcMaxGain);
  }
  buffer.agc_gain =
      buffer.agc_gain * (1.0f - kAgcSmoothing) + desired_gain * kAgcSmoothing;

  const float total_gain = gate_gain * buffer.agc_gain;
  for (float &sample : samples) {
    sample = std::clamp(sample * total_gain, -1.0f, 1.0f);
  }
}

void AudioMixer::RememberPlayedSamples(UserAudioBuffer &buffer, float sample) const {
  const size_t max_history = LastPlayedSampleCount();
  if (max_history == 0) {
    buffer.last_played_samples.clear();
    return;
  }

  buffer.last_played_samples.push_back(sample);
  while (buffer.last_played_samples.size() > max_history) {
    buffer.last_played_samples.pop_front();
  }
}

size_t AudioMixer::ConcealMissingAudio(UserAudioBuffer &buffer,
                                       float *output,
                                       size_t output_offset,
                                       size_t requested_sample_count) const {
  if (!output || requested_sample_count == 0 || buffer.last_played_samples.empty()) {
    buffer.playback_started = false;
    buffer.concealed_sample_count = 0;
    return 0;
  }

  const size_t max_concealed = MaxConcealedSampleCount();
  if (max_concealed == 0 || buffer.concealed_sample_count >= max_concealed) {
    buffer.playback_started = false;
    buffer.concealed_sample_count = 0;
    return 0;
  }

  const size_t concealed_sample_count =
      std::min(requested_sample_count, max_concealed - buffer.concealed_sample_count);
  const size_t history_size = buffer.last_played_samples.size();
  for (size_t i = 0; i < concealed_sample_count; ++i) {
    const float sample =
        buffer.last_played_samples[i % history_size] * kPlcDecay;
    output[output_offset + i] += sample;
    RememberPlayedSamples(buffer, sample);
  }

  buffer.concealed_sample_count += concealed_sample_count;
  if (buffer.concealed_sample_count >= max_concealed) {
    buffer.playback_started = false;
    buffer.concealed_sample_count = 0;
  }
  return concealed_sample_count;
}

size_t AudioMixer::MixUserBuffer(UserAudioBuffer &buffer,
                                 float *output,
                                 uint32_t frame_count,
                                 int64_t output_start_timestamp_us) {
  if (!output || frame_count == 0 || m_channels <= 0 || m_sample_rate <= 0) {
    return 0;
  }

  const int64_t output_duration_us = FramesToDurationUs(frame_count, m_sample_rate);
  const int64_t output_end_timestamp_us = output_start_timestamp_us + output_duration_us;
  const size_t requested_sample_count =
      static_cast<size_t>(frame_count) * static_cast<size_t>(m_channels);

  if (!buffer.playback_started) {
    if (TotalBufferedSampleCount(buffer) < JitterStartSampleCount()) {
      return 0;
    }
    buffer.playback_started = true;
    buffer.concealed_sample_count = 0;
  }

  size_t mixed_sample_count = 0;

  while (!buffer.segments.empty()) {
    auto &segment = buffer.segments.front();
    if (segment.samples.empty()) {
      buffer.segments.pop_front();
      continue;
    }

    if (segment.start_timestamp_us <= 0) {
      const size_t remaining_sample_count =
          requested_sample_count - mixed_sample_count;
      const size_t available_sample_count =
          std::min(remaining_sample_count, segment.samples.size());

      for (size_t i = 0; i < available_sample_count; ++i) {
        const float sample = segment.samples.front();
        output[mixed_sample_count + i] += sample;
        RememberPlayedSamples(buffer, sample);
        segment.samples.pop_front();
      }

      mixed_sample_count += available_sample_count;
      if (available_sample_count > 0) {
        buffer.concealed_sample_count = 0;
      }
      if (segment.samples.empty()) {
        buffer.segments.pop_front();
      }
      if (mixed_sample_count >= requested_sample_count) {
        return mixed_sample_count;
      }
      continue;
    }

    const size_t segment_frame_count =
        segment.samples.size() / static_cast<size_t>(m_channels);
    const int64_t segment_duration_us =
        FramesToDurationUs(segment_frame_count, m_sample_rate);
    const int64_t segment_end_timestamp_us =
        segment.start_timestamp_us + segment_duration_us;

    if (segment_end_timestamp_us <= output_start_timestamp_us - kAudioLateToleranceUs) {
      buffer.segments.pop_front();
      continue;
    }

    if (segment.start_timestamp_us >= output_end_timestamp_us) {
      break;
    }

    const int64_t mix_start_timestamp_us =
        std::max(segment.start_timestamp_us, output_start_timestamp_us);
    const int64_t mix_end_timestamp_us =
        std::min(segment_end_timestamp_us, output_end_timestamp_us);

    if (mix_end_timestamp_us <= mix_start_timestamp_us) {
      break;
    }

    const size_t segment_start_frame =
        static_cast<size_t>(
            std::max<int64_t>(0, mix_start_timestamp_us - segment.start_timestamp_us) *
            m_sample_rate / (1000 * 1000));
    const size_t output_start_frame =
        static_cast<size_t>(
            std::max<int64_t>(0, mix_start_timestamp_us - output_start_timestamp_us) *
            m_sample_rate / (1000 * 1000));
    const size_t mix_frame_count =
        std::min(segment_frame_count - segment_start_frame,
                 static_cast<size_t>(frame_count) - output_start_frame);

    const size_t segment_start_sample =
        segment_start_frame * static_cast<size_t>(m_channels);
    const size_t output_start_sample =
        output_start_frame * static_cast<size_t>(m_channels);
    const size_t mix_sample_count =
        mix_frame_count * static_cast<size_t>(m_channels);

    auto sample_it = segment.samples.begin() + static_cast<long>(segment_start_sample);
    for (size_t i = 0; i < mix_sample_count; ++i) {
      const float sample = *(sample_it + static_cast<long>(i));
      output[output_start_sample + i] += sample;
      RememberPlayedSamples(buffer, sample);
    }
    mixed_sample_count += mix_sample_count;
    if (mix_sample_count > 0) {
      buffer.concealed_sample_count = 0;
    }

    const size_t consumed_frame_count =
        static_cast<size_t>(
            std::max<int64_t>(0, output_end_timestamp_us - segment.start_timestamp_us) *
            m_sample_rate / (1000 * 1000));
    const size_t consumed_sample_count =
        std::min(consumed_frame_count * static_cast<size_t>(m_channels),
                 segment.samples.size());

    DropSamplesFromSegment(segment, consumed_sample_count);
    if (segment.samples.empty()) {
      buffer.segments.pop_front();
      continue;
    }

    if (segment.start_timestamp_us >= output_end_timestamp_us) {
      break;
    }
  }

  if (mixed_sample_count < requested_sample_count) {
    mixed_sample_count += ConcealMissingAudio(buffer,
                                             output,
                                             mixed_sample_count,
                                             requested_sample_count -
                                                 mixed_sample_count);
  }

  return mixed_sample_count;
}

} // namespace RENDER
