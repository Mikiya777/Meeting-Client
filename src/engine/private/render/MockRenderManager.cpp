#include "engine/private/render/MockRenderManager.h"

/*
1. 我现在能不能工作？ -> m_initialized
2. 我被调用了几次？ -> 三个 count
*/

namespace VCE {
//RenderManager 准备好了，可以开始接收视频/音频帧了。
Result MockRenderManager::Init() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }
  m_initialized = true;
  m_local_video_frame_count = 0;
  m_remote_video_frame_count = 0;
  m_remote_audio_frame_count = 0;
  return Result::kRet_Success;
}

Result MockRenderManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_Success;
  }
  m_initialized = false;
  return Result::kRet_Success;
}

Result MockRenderManager::AddUser(const std::string &user_id, bool /*is_local*/) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  return user_id.empty() ? Result::kRet_InvalidArgument : Result::kRet_Success;
}

Result MockRenderManager::RemoveUser(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  return user_id.empty() ? Result::kRet_InvalidArgument : Result::kRet_Success;
}

//Engine 把本地摄像头画面交给 RenderManager 做预览。
Result MockRenderManager::RenderLocalVideoFrame(const VideoFrame &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (frame.user_id.empty() || frame.width <= 0 || frame.height <= 0) {
    return Result::kRet_InvalidArgument;
  }

  m_local_video_frame_count++;
  return Result::kRet_Success;
}

//Engine 把远端用户的视频画面交给 RenderManager 显示。
Result MockRenderManager::RenderRemoteVideoFrame(const VideoFrame &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (frame.user_id.empty() || frame.width <= 0 || frame.height <= 0) {
    return Result::kRet_InvalidArgument;
  }

  m_remote_video_frame_count++;
  return Result::kRet_Success;
}

//Engine 把远端用户的音频交给 RenderManager 播放。
Result MockRenderManager::PlayRemoteAudioFrame(const AudioFrame &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (frame.user_id.empty() || frame.sample_rate <= 0 ||
      frame.channels <= 0 || frame.samples <= 0) {
    return Result::kRet_InvalidArgument;
  }

  m_remote_audio_frame_count++;

  return Result::kRet_Success;
}

Result MockRenderManager::RenderUser(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  return user_id.empty() ? Result::kRet_InvalidArgument : Result::kRet_Success;
}

Result MockRenderManager::UpdateUserVideoSize(const std::string &user_id,
                                              int width,
                                              int height) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (user_id.empty() || width <= 0 || height <= 0) {
    return Result::kRet_InvalidArgument;
  }
  return Result::kRet_Success;
}

void MockRenderManager::SetPlaybackAudioCallback(PlaybackAudioCallback /*callback*/) {}

int MockRenderManager::GetLocalVideoFrameCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_local_video_frame_count;
}

int MockRenderManager::GetRemoteVideoFrameCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_remote_video_frame_count;
}

int MockRenderManager::GetRemoteAudioFrameCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_remote_audio_frame_count;
}
} // namespace VCE
