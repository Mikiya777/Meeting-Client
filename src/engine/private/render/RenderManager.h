#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

#include "engine/private/render/IRenderManager.h"
#include "render/include/IRenderEngine.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace VCE {

class RenderManager final : public IRenderManager {
public:
  RenderManager();
  ~RenderManager() override;

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

private:
  Result PushVideoFrame(const VideoFrame &frame, bool is_local);
  std::shared_ptr<RENDER::I420Frame> CreateRenderVideoFrame(const VideoFrame &frame,
                                                            bool is_local) const;
  std::shared_ptr<RENDER::AudioFrame> CreateRenderAudioFrame(const AudioFrame &frame) const;

private:
  mutable std::mutex m_mutex;
  std::unique_ptr<RENDER::IRenderEngine> m_render_engine;
  std::unordered_set<std::string> m_users;
  std::unordered_map<std::string, VideoFrame> m_pending_remote_video_frames;
  bool m_initialized = false;
};

} // namespace VCE

#endif // RENDER_MANAGER_H
