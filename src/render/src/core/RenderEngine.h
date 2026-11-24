#ifndef RENDER_ENGINE_H
#define RENDER_ENGINE_H
/*
组件位置：
render/include/IRenderEngine.h
        |
        v
render/src/core/RenderEngine.h
        |
        v
render/src/core/RenderEngine.cpp

功能：
RenderEngine 把音频、视频、用户上下文组织起来。

视频链路：
user_name -> UserContext -> VideoRender -> OpenGL

音频链路：
AudioFrame -> AudioMixer -> AudioRender -> Speaker
*/

#include "render/include/IRenderEngine.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace RENDER {

class AudioMixer;
class AudioRender;
class UserContext;

class RenderEngine : public IRenderEngine {
public:
  RenderEngine();
  ~RenderEngine() override;

  bool Initialize(int sample_rate, int channels, int video_width, int video_height) override;
  void Uninitialize() override;

  bool AddUser(const std::string &user_name, bool is_local) override;
  bool RemoveUser(const std::string &user_name) override;

  bool PushVideoFrame(const std::string &user_name, const std::shared_ptr<I420Frame> &frame) override;
  bool PushAudioFrame(const std::string &user_name, const std::shared_ptr<AudioFrame> &frame) override;

  //RenderEngine通知Ui更新，然后UI调用RenderUser，RenderUser内部再使用OpenGl渲染。
  bool RenderUser(const std::string &user_name) override;
  bool UpdateUserVideoSize(const std::string &user_name, int width, int height) override;

  //音频设备管理
  std::vector<AudioSpeaker> GetSpeakerDevices() override;
  bool UpdateAudioSpeaker(const std::string &device_id) override;
  bool GetCurrentAudioSpeaker(std::string &device_id) override;
  void SetPlaybackAudioCallback(PlaybackAudioCallback callback) override;

private:
  std::shared_ptr<UserContext> GetUserContext(const std::string &user_name) const;

private:
  std::shared_ptr<AudioMixer> m_audio_mixer; //AudioRender 需要通过弱引用或共享引用拿到音频源
  std::unique_ptr<AudioRender> m_audio_render; //RenderEngine 独占管理它，不需要共享所有权

  std::unordered_map<std::string, std::shared_ptr<UserContext>> m_user_contexts;
  mutable std::mutex m_user_contexts_mutex; //保护 m_user_contexts 的互斥锁。

  int m_video_width = 0;
  int m_video_height = 0;
  bool m_initialized = false;
};

} // namespace RENDER

#endif // RENDER_ENGINE_H
