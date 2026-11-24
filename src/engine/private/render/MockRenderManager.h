/*
1. 初始化/释放渲染资源
2. 显示本地摄像头预览
3. 显示远端用户视频
4. 播放远端用户音频
*/

#ifndef MOCK_RENDER_MANAGER_H
#define MOCK_RENDER_MANAGER_H

#include "engine/private/render/IRenderManager.h"

#include <mutex>

namespace VCE {
class MockRenderManager final : public IRenderManager {
public:
  MockRenderManager() = default;
  ~MockRenderManager() override = default;

  Result Init() override;
  Result Uninit() override;

  Result AddUser(const std::string &user_id, bool is_local) override;
  Result RemoveUser(const std::string &user_id) override;

  Result RenderLocalVideoFrame(const VideoFrame &frame) override;
  Result RenderRemoteVideoFrame(const VideoFrame &frame) override;
  Result PlayRemoteAudioFrame(const AudioFrame &frame) override;

  Result RenderUser(const std::string &user_id) override;
  Result UpdateUserVideoSize(const std::string &user_id, int width, int height) override;
  void SetPlaybackAudioCallback(PlaybackAudioCallback callback) override;

  int GetLocalVideoFrameCount() const;
  int GetRemoteVideoFrameCount() const;
  int GetRemoteAudioFrameCount() const;

private:
  //内部状态。
  bool m_initialized = false;
  int m_local_video_frame_count = 0;
  int m_remote_video_frame_count = 0;
  int m_remote_audio_frame_count = 0;

  // Mock 可能被 Engine 的不同线程调用，因此内部状态需要锁保护。
  mutable std::mutex m_mutex;
};
} // namespace VCE

#endif //MOCK_RENDER_MANAGER_H
