#ifndef AUDIO_MIXER_H
#define AUDIO_MIXER_H
/*
模块位置：
远端用户 AudioFrame
  -> AudioMixer
  -> 多路音频混成一路
  -> AudioRender
  -> 扬声器播放

功能：负责多路音频混音
多个用户的音频帧怎么缓存
多个用户的音频怎么混成一路
播放端需要音频时怎么取
用户离开时怎么清理他的音频缓存
多线程访问音频缓存怎么保护

与其它模块的关系：
AudioMixer：
  负责多路音频缓存和混音

AudioRender：
  负责把混好的音频送到扬声器

IAudioSource：
  连接 AudioMixer 和 AudioRender 的拉取接口
*/
#include "render/src/audio/IAudioSource.h"
#include "render/include/RenderDefine.h"

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace RENDER {

class AudioMixer : public IAudioSource {
public:
  AudioMixer();
  ~AudioMixer() override;

  //持有：std::mutex、用户音频缓存、混音状态，禁止拷贝。
  AudioMixer(const AudioMixer &) = delete;
  AudioMixer &operator=(const AudioMixer &) = delete;

  //所有参与混音的数据格式要一致，所以 AudioMixer 需要知道目标采样率和通道数。
  bool Initialize(int sample_rate, int channels);

  //清空所有用户音频缓存，重置初始化状态
  void Uninitialize();

  //进入该用户自己的音频缓存。返回 false 表示输入无效、未初始化或格式不匹配。
  bool PushAudioFrame(const std::string &user_name, const std::shared_ptr<AudioFrame> &frame);

  //删除某个用户的音频缓存，比如某个用户离开了会议。
  void RemoveUser(const std::string &user_name);

  //从混音器里取出一段混好的音频，output是缓冲区用于写入，frame_count用于计数
  uint32_t PopAudio(float *output, uint32_t frame_count) override;

private:
  struct AudioSegment {
    int64_t start_timestamp_us = 0;
    std::deque<float> samples;
  };

  struct UserAudioBuffer {
    std::deque<AudioSegment> segments;
    bool playback_started = false;
    size_t concealed_sample_count = 0;
    float agc_gain = 1.0f;
    std::vector<float> high_pass_previous_input;
    std::vector<float> high_pass_previous_output;
    std::deque<float> last_played_samples;
  };

  //AudioFrame 中的原始数据转换成 float 数组。它不依赖某个具体 AudioMixer 对象的成员变量，只是一个工具函数，声明为static。
  static std::vector<float> ConvertFrameToFloat(const std::shared_ptr<AudioFrame> &frame);

  //归一化，防止多路音频叠加后超过正常范围。samples是需要归一化的音频采样数据，sample_count是采样点数量
  /*
  case：
  Alice 音量 0.8
  Bob 音量 0.7
  叠加后可能变成 1.5

  音频一般希望保持在：[-1.0, 1.0]

  这函数就是用于把最终结果控制在期待区间内
  */
  static void ApplyOutputGainAndLimit(float *samples,
                                      size_t sample_count,
                                      size_t active_source_count);

  size_t MaxBufferedSampleCount() const;
  size_t JitterStartSampleCount() const;
  size_t MaxConcealedSampleCount() const;
  size_t LastPlayedSampleCount() const;
  void TrimUserBuffer(UserAudioBuffer &buffer) const;
  size_t TotalBufferedSampleCount(const UserAudioBuffer &buffer) const;
  void DropSamplesFromSegment(AudioSegment &segment, size_t sample_count) const;
  void ApplyInputProcessing(UserAudioBuffer &buffer, std::vector<float> &samples) const;
  void RememberPlayedSamples(UserAudioBuffer &buffer, float sample) const;
  size_t ConcealMissingAudio(UserAudioBuffer &buffer,
                             float *output,
                             size_t output_offset,
                             size_t requested_sample_count) const;
  size_t MixUserBuffer(UserAudioBuffer &buffer,
                       float *output,
                       uint32_t frame_count,
                       int64_t output_start_timestamp_us);

private:
  int m_sample_rate{0};
  int m_channels{0};
  bool m_initialized{false};

  //string表示用户名称，UserAudioBuffer表示对应用户的音频数据
  std::unordered_map<std::string, UserAudioBuffer> m_user_buffers;
  std::mutex m_mutex;
};

} // namespace RENDER
#endif // AUDIO_MIXER_H
