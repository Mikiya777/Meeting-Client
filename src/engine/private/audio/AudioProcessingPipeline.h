#ifndef AUDIO_PROCESSING_PIPELINE_H
#define AUDIO_PROCESSING_PIPELINE_H

#include "engine/public/VceTypes.h"

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

struct SpeexEchoState_;
struct SpeexPreprocessState_;

namespace VCE {

class AudioProcessingPipeline {
public:
  AudioProcessingPipeline();
  ~AudioProcessingPipeline();

  AudioProcessingPipeline(const AudioProcessingPipeline &) = delete;
  AudioProcessingPipeline &operator=(const AudioProcessingPipeline &) = delete;

  bool Initialize(int sample_rate, int channels);
  void Uninitialize();
  void SetCaptureActive(bool active);

  void FeedPlaybackReference(const float *samples,
                             size_t sample_count,
                             int sample_rate,
                             int channels);
  // A null result leaves the caller to forward the original frame.
  std::shared_ptr<AudioFrame> ProcessCaptureFrame(const AudioFrame &frame);

private:
  struct DenoiserDeleter {
    void operator()(SpeexPreprocessState_ *state) const noexcept;
  };

  void ResetLocked();
  size_t FrameSampleCount() const;
  void DownmixToMono(const float *input,
                     size_t frame_count,
                     int channels,
                     std::vector<float> *output) const;
  void MonoToInterleaved(const std::vector<float> &mono,
                         int channels,
                         std::vector<float> *output) const;
  std::vector<float> ConvertFrameToFloat(const AudioFrame &frame) const;
  std::shared_ptr<AudioFrame> CreateFrameFromFloat(const AudioFrame &source,
                                                   const std::vector<float> &samples) const;
  void ProcessTenMillisecondsLocked(const std::vector<float> &capture,
                                    std::vector<float> *processed);
  void ApplyPostProcessingLocked(std::vector<float> *samples, bool far_end_active);

private:
  mutable std::mutex m_mutex;
  bool m_initialized = false;
  int m_sample_rate = 0;
  int m_channels = 0;
  int m_frame_size = 0;
  int m_filter_length = 0;
  SpeexEchoState_ *m_echo_state = nullptr;
  std::unique_ptr<SpeexPreprocessState_, DenoiserDeleter> m_denoiser;
  bool m_capture_active = false;
  std::deque<float> m_playback_reference;
  std::deque<float> m_capture_input;
  std::deque<float> m_processed_output;
  float m_high_pass_previous_input = 0.0f;
  float m_high_pass_previous_output = 0.0f;
  float m_agc_gain = 1.0f;
  float m_residual_gain = 1.0f;
  int m_playback_hold_frames = 0;
};

} // namespace VCE

#endif
