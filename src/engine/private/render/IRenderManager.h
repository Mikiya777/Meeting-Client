#ifndef I_RENDER_MANAGER_H
#define I_RENDER_MANAGER_H

#include "engine/public/VceTypes.h"

#include <cstddef>
#include <functional>
#include <string>

/*
    IRenderManager功能：假装自己渲染成功，并记录 Engine 调用了我几次
    1.对象本身的生命周期。
    2.视频渲染。
    3.音频播放。
*/

namespace VCE {
class IRenderManager {
public:
  using PlaybackAudioCallback = std::function<void(const float *samples,
                                                   size_t sample_count,
                                                   int sample_rate,
                                                   int channels)>;

  virtual ~IRenderManager() = default;

  virtual Result Init() = 0;
  virtual Result Uninit() = 0;

  virtual Result AddUser(const std::string &user_id, bool is_local) = 0;
  virtual Result RemoveUser(const std::string &user_id) = 0;

  // Render 只处理本地视频预览、远端视频显示和远端音频播放。
  // 本地音频只发送到远端，不在本地播放，避免回声。
  virtual Result RenderLocalVideoFrame(const VideoFrame &frame) = 0;
  virtual Result RenderRemoteVideoFrame(const VideoFrame &frame) = 0;
  virtual Result PlayRemoteAudioFrame(const AudioFrame &frame) = 0;

  virtual Result RenderUser(const std::string &user_id) = 0;
  virtual Result UpdateUserVideoSize(const std::string &user_id, int width, int height) = 0;
  virtual void SetPlaybackAudioCallback(PlaybackAudioCallback callback) = 0;
};
} // namespace VCE

#endif // I_RENDER_MANAGER_H
