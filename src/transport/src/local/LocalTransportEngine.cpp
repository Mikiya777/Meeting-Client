#include "transport/src/local/LocalTransportEngine.h"

namespace TRANSPORT {
namespace {

bool IsValidPublishInfo(const PublishInfo &info) {
  return info.video_width > 0 &&
         info.video_height > 0 &&
         info.video_fps > 0 &&
         info.audio_sample_rate > 0 &&
         info.audio_channels > 0;
}

} // namespace

LocalTransportEngine::~LocalTransportEngine() {
  Uninit();
}

bool LocalTransportEngine::Initialize(const PublishInfo &publish_info) {
  if (!IsValidPublishInfo(publish_info)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return true;
  }

  m_publish_info = publish_info;
  m_video_publishing = false;
  m_audio_publishing = false;
  m_subscribed_users.clear();
  m_pushed_video_frame_count = 0;
  m_pushed_audio_frame_count = 0;
  m_last_pushed_video_timestamp_us = 0;
  m_last_pushed_audio_timestamp_us = 0;
  m_state = ConnectionState::kDisconnected;
  m_initialized = true;
  return true;
}

void LocalTransportEngine::Uninit() {
  ConnectionStateCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return;
    }

    m_initialized = false;
    m_video_publishing = false;
    m_audio_publishing = false;
    m_subscribed_users.clear();
    m_state = ConnectionState::kClosed;
    callback = m_connection_state_callback;
    m_video_callback = nullptr;
    m_audio_callback = nullptr;
    m_connection_state_callback = nullptr;
  }

  if (callback) {
    callback(ConnectionState::kClosed);
  }
}

bool LocalTransportEngine::IsInitialized() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_initialized;
}

void LocalTransportEngine::SetTargetRoomInfo(const TargetRoomInfo &info) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_room_info = info;
}

TargetRoomInfo LocalTransportEngine::GetTargetRoomInfo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_room_info;
}

bool LocalTransportEngine::StartPublishVideo() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_video_publishing = true;
  }

  SetState(ConnectionState::kConnected);
  return true;
}

bool LocalTransportEngine::StopPublishVideo() {
  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_video_publishing = false;
    should_disconnect = ShouldDisconnectLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool LocalTransportEngine::StartPublishAudio() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_audio_publishing = true;
  }

  SetState(ConnectionState::kConnected);
  return true;
}

bool LocalTransportEngine::StopPublishAudio() {
  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_audio_publishing = false;
    should_disconnect = ShouldDisconnectLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool LocalTransportEngine::PushVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_video_publishing || !frame || !frame->IsValid()) {
    return false;
  }

  ++m_pushed_video_frame_count;
  m_last_pushed_video_timestamp_us = frame->timestamp_us;
  return true;
}

bool LocalTransportEngine::PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_audio_publishing || !frame || !frame->IsValid()) {
    return false;
  }

  ++m_pushed_audio_frame_count;
  m_last_pushed_audio_timestamp_us = frame->timestamp_us;
  return true;
}

bool LocalTransportEngine::IsPublishingVideo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_video_publishing;
}

bool LocalTransportEngine::IsPublishingAudio() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_audio_publishing;
}

bool LocalTransportEngine::SubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_subscribed_users.insert(user_id);
  }

  SetState(ConnectionState::kConnected);
  return true;
}

bool LocalTransportEngine::UnsubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return false;
  }

  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_subscribed_users.erase(user_id);
    should_disconnect = ShouldDisconnectLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool LocalTransportEngine::IsUserSubscribedAV(const std::string &user_id) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_subscribed_users.find(user_id) != m_subscribed_users.end();
}

std::vector<std::string> LocalTransportEngine::GetSubscribedUsers() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return {m_subscribed_users.begin(), m_subscribed_users.end()};
}

void LocalTransportEngine::RegisterVideoCallback(VideoDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_video_callback = std::move(callback);
}

void LocalTransportEngine::RegisterAudioCallback(AudioDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_audio_callback = std::move(callback);
}

void LocalTransportEngine::RegisterConnectionStateCallback(
    ConnectionStateCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_connection_state_callback = std::move(callback);
}

int LocalTransportEngine::GetPushedVideoFrameCount() const {
  return m_pushed_video_frame_count.load();
}

int LocalTransportEngine::GetPushedAudioFrameCount() const {
  return m_pushed_audio_frame_count.load();
}

int64_t LocalTransportEngine::GetLastPushedVideoTimestampUs() const {
  return m_last_pushed_video_timestamp_us.load();
}

int64_t LocalTransportEngine::GetLastPushedAudioTimestampUs() const {
  return m_last_pushed_audio_timestamp_us.load();
}

void LocalTransportEngine::InjectRemoteVideoFrame(
    const std::string &user_id,
    const std::shared_ptr<I420Frame> &frame) {
  VideoDataCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || user_id.empty() || !frame || !frame->IsValid()) {
      return;
    }

    callback = m_video_callback;
  }

  if (callback) {
    callback(user_id, frame);
  }
}

void LocalTransportEngine::InjectRemoteAudioFrame(
    const std::string &user_id,
    const std::shared_ptr<AudioFrame> &frame) {
  AudioDataCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || user_id.empty() || !frame || !frame->IsValid()) {
      return;
    }

    callback = m_audio_callback;
  }

  if (callback) {
    callback(user_id, frame);
  }
}

void LocalTransportEngine::SetState(ConnectionState state) {
  ConnectionStateCallback callback;
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized && state != ConnectionState::kClosed) {
      return;
    }

    changed = m_state != state;
    m_state = state;
    callback = m_connection_state_callback;
  }

  if (changed && callback) {
    callback(state);
  }
}

bool LocalTransportEngine::ShouldDisconnectLocked() const {
  return !m_video_publishing &&
         !m_audio_publishing &&
         m_subscribed_users.empty();
}

} // namespace TRANSPORT
