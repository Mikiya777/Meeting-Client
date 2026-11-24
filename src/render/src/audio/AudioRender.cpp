#include "render/src/audio/AudioRender.h"

#include "render/src/audio/IAudioSource.h"

#include <cstring>
#include <utility>

#ifdef _WIN32
#include "platform/windows/audio/WasapiDevice.h"
#endif

namespace RENDER {

AudioRender::AudioRender() = default;

AudioRender::~AudioRender() {
  Uninitialize();
}

bool AudioRender::Initialize(int sample_rate, int channels) {
  std::lock_guard<std::mutex> lock(m_device_mutex);
  if (m_initialized) {
    return true;
  }

  if (sample_rate <= 0 || channels <= 0) {
    return false;
  }

  m_sample_rate = sample_rate;
  m_channels = channels;

#if defined(__APPLE__) || defined(_WIN32)
  if (!OpenPlaybackDevice("default")) {
    m_sample_rate = 0;
    m_channels = 0;
    return false;
  }
#else
  m_current_speaker_id = "default";
#endif

  if (m_current_speaker_id.empty()) {
    m_current_speaker_id = "default";
  }

  m_initialized = true;
  return true;
}

void AudioRender::Uninitialize() {
  {
    std::lock_guard<std::mutex> callback_lock(m_callback_mutex);
    m_playback_callback = nullptr;
  }

  std::lock_guard<std::mutex> lock(m_device_mutex);

#if defined(__APPLE__) || defined(_WIN32)
  ClosePlaybackDevice();
#endif

  m_initialized = false;
  m_current_speaker_id.clear();
  m_sample_rate = 0;
  m_channels = 0;
}

void AudioRender::SetAudioSource(std::weak_ptr<IAudioSource> audio_source) {
  m_audio_source = std::move(audio_source);
}

#if !defined(__APPLE__) && !defined(_WIN32)
std::vector<AudioSpeaker> AudioRender::GetSpeakerDevices() const {
  return {AudioSpeaker{"default", "Default Speaker", true}};
}
#endif

bool AudioRender::UpdateSpeaker(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_device_mutex);
  if (!m_initialized) {
    return false;
  }

  const std::string target_device_id = device_id.empty() ? "default" : device_id;
#if defined(__APPLE__) || defined(_WIN32)
  const std::string previous_device_id = m_current_speaker_id;
  ClosePlaybackDevice();
  if (OpenPlaybackDevice(target_device_id)) {
    return true;
  }

  const std::string fallback_device_id = previous_device_id.empty() ? "default" : previous_device_id;
  if (!OpenPlaybackDevice(fallback_device_id)) {
    m_current_speaker_id = fallback_device_id;
  }
  return false;
#else
  m_current_speaker_id = target_device_id;
  return true;
#endif
}

std::string AudioRender::GetCurrentSpeakerId() const {
  std::lock_guard<std::mutex> lock(m_device_mutex);
  return m_current_speaker_id;
}

void AudioRender::SetPlaybackAudioCallback(PlaybackAudioCallback callback) {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  m_playback_callback = std::move(callback);
}

void AudioRender::FillPlaybackBuffer(float *output, uint32_t frame_count) {
  if (!output || frame_count == 0 || m_channels <= 0) {
    return;
  }

  const size_t sample_count =
      static_cast<size_t>(frame_count) * static_cast<size_t>(m_channels);
  std::memset(output, 0, sample_count * sizeof(float));

  if (auto source = m_audio_source.lock()) {
    const uint32_t filled_frames = source->PopAudio(output, frame_count);
    if (filled_frames < frame_count) {
      const size_t filled_samples =
          static_cast<size_t>(filled_frames) * static_cast<size_t>(m_channels);
      std::memset(output + filled_samples,
                  0,
                  (sample_count - filled_samples) * sizeof(float));
    }
  }

  PlaybackAudioCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    callback = m_playback_callback;
  }
  if (callback) {
    callback(output, sample_count, m_sample_rate, m_channels);
  }
}

} // namespace RENDER
