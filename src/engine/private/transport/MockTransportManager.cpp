#include "engine/private/transport/MockTransportManager.h"

namespace VCE {
//接口方法部分
Result MockTransportManager::Init(const TransportPublishConfig &config) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }

  if (config.video_width <= 0 || config.video_height <= 0 ||
      config.video_fps <= 0 || config.audio_sample_rate <= 0 ||
      config.audio_channels <= 0) {
    return Result::kRet_InvalidArgument;
  }

  m_publish_config = config;
  m_initialized = true;
  m_camera_video_publishing = false;
  m_audio_publishing = false;
  m_pushed_video_frame_count = 0;
  m_pushed_audio_frame_count = 0;
  return Result::kRet_Success;
}
Result MockTransportManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_Success;
  }

  m_initialized = false;
  m_camera_video_publishing = false;
  m_audio_publishing = false;
  m_subscribed_users.clear();
  m_callback.reset();

  return Result::kRet_Success;
}

Result MockTransportManager::SetRoomInfo(const TransportRoomInfo &info) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (info.local_user_id.empty() || info.room_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  m_room_info = info;
  return Result::kRet_Success;
}

TransportRoomInfo MockTransportManager::GetRoomInfo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_room_info;
}

Result MockTransportManager::StartPublishCameraVideo() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_camera_video_publishing = true;
  return Result::kRet_Success;
}
Result MockTransportManager::StopPublishCameraVideo() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_camera_video_publishing = false;
  return Result::kRet_Success;
}
Result MockTransportManager::StartPublishAudio() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_audio_publishing = true;
  return Result::kRet_Success;
}
Result MockTransportManager::StopPublishAudio() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_audio_publishing = false;
  return Result::kRet_Success;
}
Result MockTransportManager::PushVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (!m_camera_video_publishing) {
    return Result::kRet_InvalidState;
  }

  //frame 非法
  if (!frame || frame->user_id.empty() || !frame->HasData()) {
    return Result::kRet_InvalidArgument;
  }

  ++m_pushed_video_frame_count;

  return Result::kRet_Success;
}
Result MockTransportManager::PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (!m_audio_publishing) {
    return Result::kRet_InvalidState;
  }

  if (!frame || frame->user_id.empty() || !frame->HasData()) {
    return Result::kRet_InvalidArgument;
  }

  ++m_pushed_audio_frame_count;

  return Result::kRet_Success;
}

Result MockTransportManager::SubscribeUserAV(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  m_subscribed_users.insert(user_id);
  return Result::kRet_Success;
}

Result MockTransportManager::UnsubscribeUserAV(const std::string &user_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  m_subscribed_users.erase(user_id);
  return Result::kRet_Success;
}

Result MockTransportManager::SubscribeUsersAV(const std::vector<std::string> &user_ids) {
  Result result = Result::kRet_Success;
  for (const auto &user_id : user_ids) {
    const auto current = SubscribeUserAV(user_id);
    if (current != Result::kRet_Success &&
        result == Result::kRet_Success) {
      result = current;
    }
  }

  return result;
}

Result MockTransportManager::UnsubscribeUsersAV(const std::vector<std::string> &user_ids) {
  Result result = Result::kRet_Success;
  for (const auto &user_id : user_ids) {
    const auto current = UnsubscribeUserAV(user_id);
    if (current != Result::kRet_Success &&
        result == Result::kRet_Success) {
      result = current;
    }
  }

  return result;
}

//测试方法部分
bool MockTransportManager::IsPublishingCameraVideo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_camera_video_publishing;
}
bool MockTransportManager::IsPublishingAudio() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_audio_publishing;
}
bool MockTransportManager::IsUserSubscribedAV(const std::string &user_id) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_subscribed_users.find(user_id) != m_subscribed_users.end();
}
std::vector<std::string> MockTransportManager::GetSubscribedUsers() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return {m_subscribed_users.begin(), m_subscribed_users.end()};
}
int MockTransportManager::GetPushedVideoFrameCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_pushed_video_frame_count;
}
int MockTransportManager::GetPushedAudioFrameCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_pushed_audio_frame_count;
}

// 专用方法部分
void MockTransportManager::SetTransportDataCallback(const std::shared_ptr<ITransportDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  //使用weak_ptr 去保存 engine的 shared_ptr callback （注册）
  m_callback = callback;
}
void MockTransportManager::SimulateRemoteVideoFrame(
    const std::string &user_id,
    const std::shared_ptr<I420Frame> &frame) {
  std::shared_ptr<ITransportDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }
  if (callback) {
    callback->OnTransportVideoFrame(user_id, frame);
  }
}
void MockTransportManager::SimulateRemoteAudioFrame(
    const std::string &user_id,
    const std::shared_ptr<AudioFrame> &frame) {
  std::shared_ptr<ITransportDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }
  if (callback) {
    callback->OnTransportAudioFrame(user_id, frame);
  }
}
void MockTransportManager::SimulateConnectionStateChanged(TransportState state) {
  std::shared_ptr<ITransportDataCallback> callback;
  // callback 会进入 Engine，Engine 后面可能又调用 Render/Transport。如果拿着 Transport 的锁调用外部对象，以后容易死锁。
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }
  if (callback) {
    callback->OnTransportConnectionStateChanged(state);
  }
}

} // namespace VCE
