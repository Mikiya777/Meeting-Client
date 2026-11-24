#ifndef I_RENDER_ENGINE_H
#define I_RENDER_ENGINE_H
/*
组件位置：
外部业务层 / Engine 层
  -> IRenderEngine.h
  -> RenderEngine.h/.cpp
  -> video / audio 内部实现
功能：
1. 创建渲染引擎
2. 初始化 / 反初始化
3. 用户管理
4. 推送音视频帧
5. 触发视频渲染
6. 更新视频窗口尺寸
7. 扬声器设备管理
*/
#include "render/include/RenderDefine.h"

#include <cstddef>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace RENDER {

class RENDER_ENGINE_API IRenderEngine {
public:
  using PlaybackAudioCallback = std::function<void(const float *samples,
                                                   size_t sample_count,
                                                   int sample_rate,
                                                   int channels)>;

  virtual ~IRenderEngine() = default;

  static std::unique_ptr<IRenderEngine> CreateRenderEngine();

  virtual bool Initialize(int sample_rate, int channels, int video_width, int video_height) = 0;
  virtual void Uninitialize() = 0;

  virtual bool AddUser(const std::string &user_name, bool is_local) = 0;
  virtual bool RemoveUser(const std::string &user_name) = 0;

  virtual bool PushVideoFrame(const std::string &user_name, const std::shared_ptr<I420Frame> &frame) = 0;
  virtual bool PushAudioFrame(const std::string &user_name, const std::shared_ptr<AudioFrame> &frame) = 0;

  virtual bool RenderUser(const std::string &user_name) = 0;
  virtual bool UpdateUserVideoSize(const std::string &user_name, int width, int height) = 0;

  virtual std::vector<AudioSpeaker> GetSpeakerDevices() = 0;
  virtual bool UpdateAudioSpeaker(const std::string &device_id) = 0;
  virtual bool GetCurrentAudioSpeaker(std::string &device_id) = 0;

  virtual void SetPlaybackAudioCallback(PlaybackAudioCallback callback) = 0;
};

} // namespace RENDER

#endif // I_RENDER_ENGINE_H
