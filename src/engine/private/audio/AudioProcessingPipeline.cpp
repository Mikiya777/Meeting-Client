#include "engine/private/audio/AudioProcessingPipeline.h"

#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace VCE {
namespace {

constexpr int kProcessingFrameMs = 10;
constexpr int kEchoTailMs = 240;
constexpr size_t kPlaybackLeadFrames = 2;
constexpr size_t kMaxPlaybackReferenceFrames = 8;
constexpr float kFloatToInt16 = 32767.0f;
constexpr float kInt16ToFloat = 1.0f / 32768.0f;
constexpr float kHighPassAlpha = 0.995f;
constexpr int kNoiseSuppressionDb = -20;
constexpr int kFarEndEchoSuppressionDb = -55;
constexpr float kAgcTargetRms = 0.075f;
constexpr float kAgcMinimumInputRms = 0.008f;
constexpr float kAgcMinimumGain = 0.85f;
constexpr float kAgcMaximumGain = 2.0f;
constexpr float kAgcSmoothing = 0.04f;
constexpr float kFarEndActivityRms = 0.008f;
constexpr int kPlaybackHoldFrames = 12;
constexpr float kQuietResidualRms = 0.015f;
constexpr float kClearSpeechRms = 0.045f;
constexpr float kQuietResidualGain = 0.12f;
constexpr float kResidualAttenuationSmoothing = 0.55f;
constexpr float kResidualRecoverySmoothing = 0.8f;

spx_int16_t FloatToInt16(float value) {
  const float clamped = std::clamp(value, -1.0f, 1.0f);
  return static_cast<spx_int16_t>(std::lrintf(clamped * kFloatToInt16));
}

float Int16ToFloat(spx_int16_t value) {
  return static_cast<float>(value) * kInt16ToFloat;
}

} // namespace

void AudioProcessingPipeline::DenoiserDeleter::operator()(
    SpeexPreprocessState_ *state) const noexcept {
  speex_preprocess_state_destroy(state);
}

AudioProcessingPipeline::AudioProcessingPipeline() = default;

AudioProcessingPipeline::~AudioProcessingPipeline() {
  Uninitialize();
}

bool AudioProcessingPipeline::Initialize(int sample_rate, int channels) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized &&
      m_sample_rate == sample_rate &&
      m_channels == channels) {
    return true;
  }

  ResetLocked();
  if (sample_rate <= 0 || channels <= 0) {
    return false;
  }

  m_frame_size = sample_rate * kProcessingFrameMs / 1000;
  m_filter_length = sample_rate * kEchoTailMs / 1000;
  if (m_frame_size <= 0 || m_filter_length <= m_frame_size) {
    ResetLocked();
    return false;
  }

  m_echo_state = speex_echo_state_init(m_frame_size, m_filter_length);
  if (!m_echo_state) {
    ResetLocked();
    return false;
  }

  speex_echo_ctl(m_echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate);

  m_sample_rate = sample_rate;
  m_channels = channels;
  m_initialized = true;
  return true;
}

void AudioProcessingPipeline::Uninitialize() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetLocked();
}

void AudioProcessingPipeline::SetCaptureActive(bool active) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || m_capture_active == active) {
    return;
  }

  m_capture_active = active;
  m_playback_reference.clear();
  m_capture_input.clear();
  m_processed_output.clear();
  m_high_pass_previous_input = 0.0f;
  m_high_pass_previous_output = 0.0f;
  m_agc_gain = 1.0f;
  m_residual_gain = 1.0f;
  m_playback_hold_frames = 0;
  if (m_echo_state) {
    speex_echo_state_reset(m_echo_state);
  }

  // The denoiser keeps a noise estimate and overlapping PCM between calls.
  m_denoiser.reset();
  if (active) {
    m_playback_reference.assign(FrameSampleCount() * kPlaybackLeadFrames, 0.0f);
    m_denoiser.reset(speex_preprocess_state_init(m_frame_size, m_sample_rate));
    if (m_denoiser) {
      int denoise = 1;
      int suppression_db = kNoiseSuppressionDb;
      if (speex_preprocess_ctl(m_denoiser.get(), SPEEX_PREPROCESS_SET_DENOISE,
                               &denoise) != 0 ||
          speex_preprocess_ctl(m_denoiser.get(), SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
                               &suppression_db) != 0) {
        m_denoiser.reset();
      } else {
        speex_preprocess_ctl(m_denoiser.get(), SPEEX_PREPROCESS_SET_ECHO_STATE,
                             m_echo_state);
        int echo_suppression_db = kFarEndEchoSuppressionDb;
        speex_preprocess_ctl(m_denoiser.get(), SPEEX_PREPROCESS_SET_ECHO_SUPPRESS,
                             &echo_suppression_db);
      }
    }
    m_processed_output.insert(m_processed_output.end(),
                              FrameSampleCount(),
                              0.0f);
  }
}

void AudioProcessingPipeline::FeedPlaybackReference(const float *samples,
                                                    size_t sample_count,
                                                    int sample_rate,
                                                    int channels) {
  if (!samples || sample_count == 0 || sample_rate <= 0 || channels <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_capture_active ||
      sample_rate != m_sample_rate ||
      channels != m_channels ||
      sample_count % static_cast<size_t>(channels) != 0) {
    return;
  }

  std::vector<float> mono;
  DownmixToMono(samples, sample_count / static_cast<size_t>(channels), channels, &mono);

  // Match each capture block with a reference block even when playback arrives in bursts.
  m_playback_reference.insert(m_playback_reference.end(), mono.begin(), mono.end());
  const size_t block_size = FrameSampleCount();
  const size_t max_samples = block_size * kMaxPlaybackReferenceFrames;
  if (m_playback_reference.size() > max_samples) {
    const size_t excess = m_playback_reference.size() - max_samples;
    const size_t drop = ((excess + block_size - 1) / block_size) * block_size;
    m_playback_reference.erase(m_playback_reference.begin(),
                               m_playback_reference.begin() + drop);
  }
}

std::shared_ptr<AudioFrame> AudioProcessingPipeline::ProcessCaptureFrame(
    const AudioFrame &frame) {
  if (!frame.HasData()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_capture_active ||
      frame.sample_rate != m_sample_rate ||
      frame.channels != m_channels) {
    return nullptr;
  }

  const std::vector<float> input = ConvertFrameToFloat(frame);
  if (input.empty()) {
    return nullptr;
  }

  std::vector<float> capture_mono;
  DownmixToMono(input.data(), frame.samples, frame.channels, &capture_mono);

  m_capture_input.insert(m_capture_input.end(),
                         capture_mono.begin(),
                         capture_mono.end());
  const size_t block_size = FrameSampleCount();
  while (m_capture_input.size() >= block_size) {
    std::vector<float> block;
    block.reserve(block_size);
    for (size_t i = 0; i < block_size; ++i) {
      block.push_back(m_capture_input.front());
      m_capture_input.pop_front();
    }

    std::vector<float> processed_block;
    ProcessTenMillisecondsLocked(block, &processed_block);
    m_processed_output.insert(m_processed_output.end(),
                              processed_block.begin(),
                              processed_block.end());
  }

  std::vector<float> processed_mono(capture_mono.size(), 0.0f);
  const size_t available =
      std::min(processed_mono.size(), m_processed_output.size());
  for (size_t i = 0; i < available; ++i) {
    processed_mono[i] = m_processed_output.front();
    m_processed_output.pop_front();
  }

  std::vector<float> output;
  MonoToInterleaved(processed_mono, frame.channels, &output);
  return CreateFrameFromFloat(frame, output);
}

void AudioProcessingPipeline::ResetLocked() {
  m_denoiser.reset();
  if (m_echo_state) {
    speex_echo_state_destroy(m_echo_state);
    m_echo_state = nullptr;
  }

  m_sample_rate = 0;
  m_channels = 0;
  m_frame_size = 0;
  m_filter_length = 0;
  m_capture_active = false;
  m_playback_reference.clear();
  m_capture_input.clear();
  m_processed_output.clear();
  m_high_pass_previous_input = 0.0f;
  m_high_pass_previous_output = 0.0f;
  m_agc_gain = 1.0f;
  m_residual_gain = 1.0f;
  m_playback_hold_frames = 0;
  m_initialized = false;
}

size_t AudioProcessingPipeline::FrameSampleCount() const {
  return static_cast<size_t>(std::max(m_frame_size, 0));
}

void AudioProcessingPipeline::DownmixToMono(const float *input,
                                            size_t frame_count,
                                            int channels,
                                            std::vector<float> *output) const {
  if (!input || !output || frame_count == 0 || channels <= 0) {
    return;
  }

  output->reserve(output->size() + frame_count);
  const float scale = 1.0f / static_cast<float>(channels);
  for (size_t frame = 0; frame < frame_count; ++frame) {
    float mixed = 0.0f;
    for (int channel = 0; channel < channels; ++channel) {
      mixed += input[frame * static_cast<size_t>(channels) +
                     static_cast<size_t>(channel)];
    }
    output->push_back(std::clamp(mixed * scale, -1.0f, 1.0f));
  }
}

void AudioProcessingPipeline::MonoToInterleaved(const std::vector<float> &mono,
                                                int channels,
                                                std::vector<float> *output) const {
  if (!output || channels <= 0) {
    return;
  }

  output->resize(mono.size() * static_cast<size_t>(channels));
  for (size_t frame = 0; frame < mono.size(); ++frame) {
    for (int channel = 0; channel < channels; ++channel) {
      (*output)[frame * static_cast<size_t>(channels) +
                static_cast<size_t>(channel)] = mono[frame];
    }
  }
}

std::vector<float> AudioProcessingPipeline::ConvertFrameToFloat(
    const AudioFrame &frame) const {
  if (!frame.HasData()) {
    return {};
  }

  const size_t sample_count =
      static_cast<size_t>(frame.samples) * static_cast<size_t>(frame.channels);
  std::vector<float> samples(sample_count);
  std::memcpy(samples.data(), frame.data.get(), sample_count * sizeof(float));
  return samples;
}

std::shared_ptr<AudioFrame> AudioProcessingPipeline::CreateFrameFromFloat(
    const AudioFrame &source,
    const std::vector<float> &samples) const {
  auto output = std::make_shared<AudioFrame>();
  output->sample_rate = source.sample_rate;
  output->channels = source.channels;
  output->samples = source.samples;
  output->timestamp_us = source.timestamp_us;
  output->user_id = source.user_id;

  output->data = std::shared_ptr<uint8_t[]>(new uint8_t[output->DataSize()]);
  const size_t bytes =
      std::min(output->DataSize(), samples.size() * sizeof(float));
  std::memcpy(output->data.get(), samples.data(), bytes);
  if (bytes < output->DataSize()) {
    std::memset(output->data.get() + bytes, 0, output->DataSize() - bytes);
  }
  return output;
}

void AudioProcessingPipeline::ProcessTenMillisecondsLocked(
    const std::vector<float> &capture,
    std::vector<float> *processed) {
  if (!processed || !m_echo_state || capture.size() != FrameSampleCount()) {
    return;
  }

  std::vector<spx_int16_t> rec(capture.size());
  std::vector<spx_int16_t> playback(capture.size(), 0);
  std::vector<spx_int16_t> out(capture.size());

  for (size_t i = 0; i < capture.size(); ++i) {
    rec[i] = FloatToInt16(capture[i]);
  }

  double playback_energy = 0.0;
  if (m_playback_reference.size() >= playback.size()) {
    for (auto &sample : playback) {
      const float reference = m_playback_reference.front();
      playback_energy += static_cast<double>(reference) * reference;
      sample = FloatToInt16(reference);
      m_playback_reference.pop_front();
    }
  }

  // Reflections can remain audible briefly after the far end stops speaking.
  if (playback_energy >= playback.size() * kFarEndActivityRms * kFarEndActivityRms) {
    m_playback_hold_frames = kPlaybackHoldFrames;
  } else if (m_playback_hold_frames > 0) {
    --m_playback_hold_frames;
  }

  speex_echo_cancellation(m_echo_state, rec.data(), playback.data(), out.data());
  if (m_denoiser) {
    // Suppress background noise and residual echo in the microphone PCM.
    speex_preprocess_run(m_denoiser.get(), out.data());
  }

  std::vector<float> block;
  block.reserve(out.size());
  for (spx_int16_t sample : out) {
    block.push_back(Int16ToFloat(sample));
  }
  ApplyPostProcessingLocked(&block, m_playback_hold_frames > 0);
  processed->insert(processed->end(), block.begin(), block.end());
}

void AudioProcessingPipeline::ApplyPostProcessingLocked(
    std::vector<float> *samples, bool far_end_active) {
  if (!samples || samples->empty()) {
    return;
  }

  double energy = 0.0;
  for (float &sample : *samples) {
    const float high_passed =
        sample - m_high_pass_previous_input +
        kHighPassAlpha * m_high_pass_previous_output;
    m_high_pass_previous_input = sample;
    m_high_pass_previous_output = high_passed;
    sample = high_passed;
    energy += static_cast<double>(sample) * static_cast<double>(sample);
  }

  const float rms = static_cast<float>(
      std::sqrt(energy / static_cast<double>(samples->size())));
  float target_gain = 1.0f;
  if (rms >= kAgcMinimumInputRms) {
    target_gain = std::clamp(kAgcTargetRms / rms,
                             kAgcMinimumGain,
                             kAgcMaximumGain);
  }
  if (far_end_active && rms < kClearSpeechRms) {
    target_gain = std::min(target_gain, 1.0f);
  }
  m_agc_gain += (target_gain - m_agc_gain) * kAgcSmoothing;

  // An unconverged echo filter can leave recognizable speech in the residual.
  float residual_target = 1.0f;
  if (far_end_active) {
    const float speech_level = std::clamp(
        (rms - kQuietResidualRms) / (kClearSpeechRms - kQuietResidualRms), 0.0f, 1.0f);
    residual_target = kQuietResidualGain + (1.0f - kQuietResidualGain) * speech_level;
  }
  const float smoothing = residual_target > m_residual_gain
      ? kResidualRecoverySmoothing : kResidualAttenuationSmoothing;
  m_residual_gain += (residual_target - m_residual_gain) * smoothing;

  for (float &sample : *samples) {
    sample = std::clamp(sample * m_agc_gain * m_residual_gain, -1.0f, 1.0f);
  }
}

} // namespace VCE
