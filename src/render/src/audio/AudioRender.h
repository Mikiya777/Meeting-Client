#ifndef AUDIO_RENDER_H
#define AUDIO_RENDER_H
/*
模块位置：
远端用户 AudioFrame
  -> AudioMixer 混音
  -> AudioRender 从 AudioMixer 拉取混音数据
  -> 扬声器播放

功能：负责音频播放
1. 管理播放设备
2. 从音频源拉取混音数据
3. 把音频数据填入播放缓冲区

与其它模块的关系：
*/
#include "render/include/RenderDefine.h"

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
namespace PLATFORM { class WasapiDevice; }
#endif

namespace RENDER {

class IAudioSource;
using PlaybackAudioCallback = std::function<void(const float *samples,
                                                 size_t sample_count,
                                                 int sample_rate,
                                                 int channels)>;

class AudioRender {
public:
  AudioRender();
  ~AudioRender();

  //持有设备资源、回调状态、当前扬声器 ID，禁止拷贝。
  AudioRender(const AudioRender &) = delete;
  AudioRender &operator=(const AudioRender &) = delete;

  /*
初始化平台播放后端
打开默认扬声器设备
设置播放格式
注册播放回调
启动播放设备
  */
  bool Initialize(int sample_rate, int channels);

  /*
停止播放设备
释放设备句柄
清空当前扬声器 ID
标记未初始化
  */
  void Uninitialize();

  //设置 AudioRender 从哪里拉取音频数据。本模块不持有IAudioSource，因此使用std::weak_ptr仅使用，避免循环引用。
  void SetAudioSource(std::weak_ptr<IAudioSource> audio_source);

  std::vector<AudioSpeaker> GetSpeakerDevices() const;

  //关闭旧设备、重新打开新设备。
  bool UpdateSpeaker(const std::string &device_id);

  std::string GetCurrentSpeakerId() const;

  void SetPlaybackAudioCallback(PlaybackAudioCallback callback);

  void FillPlaybackBuffer(float *output, uint32_t frame_count);

private:
#if defined(__APPLE__) || defined(_WIN32)
  bool OpenPlaybackDevice(const std::string &device_id);
  void ClosePlaybackDevice();
#endif

#ifdef __APPLE__
  static void AudioQueueOutputCallback(void *user_data,
                                       AudioQueueRef queue,
                                       AudioQueueBufferRef buffer);

  bool FillAndEnqueueBuffer(AudioQueueBufferRef buffer);
#endif

  //AudioRender 不应该拥有音频源的生命周期。这个模块不负责混音和存储用户的音频，也就是不持有，只使用。
  std::weak_ptr<IAudioSource> m_audio_source;
  PlaybackAudioCallback m_playback_callback;
  int m_sample_rate = 0;
  int m_channels = 0;
  std::string m_current_speaker_id;
  bool m_initialized = false;

  mutable std::mutex m_device_mutex;
  mutable std::mutex m_callback_mutex;

#ifdef __APPLE__
  AudioQueueRef m_audio_queue = nullptr;
  std::vector<AudioQueueBufferRef> m_audio_buffers;
  uint32_t m_buffer_frame_count = 0;
  uint32_t m_buffer_byte_size = 0;
#elif defined(_WIN32)
  std::unique_ptr<PLATFORM::WasapiDevice> m_playback_device;
#endif
};

} // namespace RENDER

#endif // AUDIO_RENDER_H
