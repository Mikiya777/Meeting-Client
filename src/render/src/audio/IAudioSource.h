#ifndef I_AUDIO_SOURCE_H
#define I_AUDIO_SOURCE_H
/*
模块位置
AudioMixer
  -> 实现 IAudioSource

AudioRender
  -> 持有 IAudioSource
  -> 从 IAudioSource 拉取音频数据

功能：定义音频播放端如何从音频源里取数据，Pull 模型
AudioRender 需要播放时，主动向 IAudioSource 要一段音频
把“音频生产/混音”和“音频播放/设备回调”分开。

与其它模块关系：
AudioMixer：
  负责混音，实现 PopAudio

AudioRender：
  负责播放，需要音频时调用 PopAudio

IAudioSource：
  规定播放端如何从音频源拉取一段 float PCM 数据
*/
#include <cstdint>

namespace RENDER {

class IAudioSource {
public:
  virtual ~IAudioSource() = default;

  //核心函数
  /*
  Pop的意思是：
  AudioMixer 里有混好的音频缓存
  AudioRender 调 PopAudio
  AudioMixer 把一段音频写到 output
  这段数据就被消费掉

  output是输出缓冲区，函数将音频数据写入这里，frame_count负责记录实际填充了多少帧音频。
  */
  virtual uint32_t PopAudio(float *output, uint32_t frame_count) = 0;
};

} // namespace RENDER

#endif // I_AUDIO_SOURCE_H
