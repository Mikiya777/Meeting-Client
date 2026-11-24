#include "transport/src/srs/SrsTransportEngine.h"

#include "transport/src/rtc/RtcPullTransport.h"
#include "transport/src/rtc/RtcPushTransport.h"
#include "transport/src/srs/SrsTransportCommon.h"

namespace TRANSPORT {

SrsTransportEngine::SrsTransportEngine() = default;

SrsTransportEngine::~SrsTransportEngine() {
  Uninit();
}

bool SrsTransportEngine::Initialize(const PublishInfo &publish_info) {
  if (!IsValidPublishInfo(publish_info)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return true;
  }

  m_publish_info = publish_info;
  m_state = ConnectionState::kDisconnected;
  m_video_publishing = false;
  m_audio_publishing = false;
  m_publish_endpoint.clear();
  m_publish_resource_url.clear();
  m_real_publisher.reset();
  m_real_subscribers.clear();
  m_pull_sessions.clear();
  m_pushed_video_frame_count = 0;
  m_pushed_audio_frame_count = 0;
  m_last_pushed_video_timestamp_us = 0;
  m_last_pushed_audio_timestamp_us = 0;
  m_initialized = true;
  return true;
}

void SrsTransportEngine::Uninit() {
  ConnectionStateCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return;
    }

    m_initialized = false;
    m_video_publishing = false;
    m_audio_publishing = false;
    ClosePublishSessionLocked();
    m_real_subscribers.clear();
    m_pull_sessions.clear();
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

bool SrsTransportEngine::IsInitialized() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_initialized;
}

void SrsTransportEngine::SetTargetRoomInfo(const TargetRoomInfo &info) {
  std::lock_guard<std::mutex> lock(m_mutex);
  ClosePublishSessionLocked();
  m_real_subscribers.clear();
  m_room_info = info;
  m_publish_endpoint.clear();
  m_publish_resource_url.clear();
  m_pull_sessions.clear();
}

TargetRoomInfo SrsTransportEngine::GetTargetRoomInfo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_room_info;
}

bool SrsTransportEngine::StartPublishVideo() {
  ConnectionStateCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !HasValidRoomInfoLocked() ||
        !EnsurePublishSessionLocked()) {
      return false;
    }

    m_video_publishing = true;
    callback = SetStateLocked(ConnectionState::kConnected);
  }

  if (callback) {
    callback(ConnectionState::kConnected);
  }
  return true;
}

bool SrsTransportEngine::StopPublishVideo() {
  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_video_publishing = false;
    if (!m_audio_publishing) {
      ClosePublishSessionLocked();
    }
    should_disconnect = !HasAnyActiveSessionLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool SrsTransportEngine::StartPublishAudio() {
  ConnectionStateCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !HasValidRoomInfoLocked() ||
        !EnsurePublishSessionLocked()) {
      return false;
    }

    m_audio_publishing = true;
    callback = SetStateLocked(ConnectionState::kConnected);
  }

  if (callback) {
    callback(ConnectionState::kConnected);
  }
  return true;
}

bool SrsTransportEngine::StopPublishAudio() {
  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    m_audio_publishing = false;
    if (!m_video_publishing) {
      ClosePublishSessionLocked();
    }
    should_disconnect = !HasAnyActiveSessionLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool SrsTransportEngine::PushVideoFrame(
    const std::shared_ptr<I420Frame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_video_publishing || !frame || !frame->IsValid()) {
    return false;
  }
  if (RealWebRtcEnabledLocked() &&
      (!m_real_publisher || !m_real_publisher->PushVideoFrame(frame))) {
    return false;
  }

  ++m_pushed_video_frame_count;
  m_last_pushed_video_timestamp_us = frame->timestamp_us;
  return true;
}

bool SrsTransportEngine::PushAudioFrame(
    const std::shared_ptr<AudioFrame> &frame) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || !m_audio_publishing || !frame || !frame->IsValid()) {
    return false;
  }
  if (RealWebRtcEnabledLocked() &&
      (!m_real_publisher || !m_real_publisher->PushAudioFrame(frame))) {
    return false;
  }

  ++m_pushed_audio_frame_count;
  m_last_pushed_audio_timestamp_us = frame->timestamp_us;
  return true;
}

bool SrsTransportEngine::IsPublishingVideo() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_video_publishing;
}

bool SrsTransportEngine::IsPublishingAudio() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_audio_publishing;
}

bool SrsTransportEngine::SubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return false;
  }

  ConnectionStateCallback callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized || !HasValidRoomInfoLocked() ||
        m_room_info.pull_server_url.empty()) {
      return false;
    }

    PullSession session;
    session.user_id = user_id;
    session.endpoint = BuildPullEndpointLocked(user_id);
    session.resource_url = session.endpoint;
    if (session.endpoint.empty()) {
      return false;
    }

    if (RealWebRtcEnabledLocked()) {
#if TRANSPORT_ENABLE_REAL_SRS
      auto subscriber = std::make_unique<RtcPullTransport>();
      if (!subscriber->Open(m_publish_info,
                            m_room_info,
                            user_id,
                            session.endpoint,
                            m_video_callback,
                            m_audio_callback)) {
        return false;
      }
      session.resource_url = subscriber->ResourceUrl().empty()
                                 ? session.endpoint
                                 : subscriber->ResourceUrl();
      m_real_subscribers[user_id] = std::move(subscriber);
#else
      return false;
#endif
    }

    m_pull_sessions[user_id] = std::move(session);
    callback = SetStateLocked(ConnectionState::kConnected);
  }

  if (callback) {
    callback(ConnectionState::kConnected);
  }
  return true;
}

bool SrsTransportEngine::UnsubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return false;
  }

  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return false;
    }

    const auto subscriber = m_real_subscribers.find(user_id);
    if (subscriber != m_real_subscribers.end()) {
      subscriber->second->Close();
      m_real_subscribers.erase(subscriber);
    }
    m_pull_sessions.erase(user_id);
    should_disconnect = !HasAnyActiveSessionLocked();
  }

  if (should_disconnect) {
    SetState(ConnectionState::kDisconnected);
  }
  return true;
}

bool SrsTransportEngine::IsUserSubscribedAV(
    const std::string &user_id) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_pull_sessions.find(user_id) != m_pull_sessions.end();
}

std::vector<std::string> SrsTransportEngine::GetSubscribedUsers() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::vector<std::string> users;
  users.reserve(m_pull_sessions.size());
  for (const auto &entry : m_pull_sessions) {
    users.push_back(entry.first);
  }
  return users;
}

void SrsTransportEngine::RegisterVideoCallback(VideoDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_video_callback = std::move(callback);
}

void SrsTransportEngine::RegisterAudioCallback(AudioDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_audio_callback = std::move(callback);
}

void SrsTransportEngine::RegisterConnectionStateCallback(
    ConnectionStateCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_connection_state_callback = std::move(callback);
}

std::string SrsTransportEngine::GetLocalStreamName() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return MakeStreamName(m_room_info.room_id, m_room_info.local_user_id);
}

std::string SrsTransportEngine::GetPublishEndpoint() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return BuildPublishEndpointLocked();
}

std::string SrsTransportEngine::GetPullEndpoint(
    const std::string &user_id) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return BuildPullEndpointLocked(user_id);
}

int SrsTransportEngine::GetPushedVideoFrameCount() const {
  return m_pushed_video_frame_count.load();
}

int SrsTransportEngine::GetPushedAudioFrameCount() const {
  return m_pushed_audio_frame_count.load();
}

int64_t SrsTransportEngine::GetLastPushedVideoTimestampUs() const {
  return m_last_pushed_video_timestamp_us.load();
}

int64_t SrsTransportEngine::GetLastPushedAudioTimestampUs() const {
  return m_last_pushed_audio_timestamp_us.load();
}

bool SrsTransportEngine::HasValidRoomInfoLocked() const {
  return !m_room_info.local_user_id.empty() &&
         !m_room_info.room_id.empty();
}

bool SrsTransportEngine::HasAnyActiveSessionLocked() const {
  return m_video_publishing ||
         m_audio_publishing ||
         !m_pull_sessions.empty();
}

bool SrsTransportEngine::EnsurePublishSessionLocked() {
  if (!m_publish_resource_url.empty()) {
    return true;
  }

  m_publish_endpoint = BuildPublishEndpointLocked();
  if (m_publish_endpoint.empty()) {
    return false;
  }

  m_state = ConnectionState::kConnecting;
  if (RealWebRtcEnabledLocked()) {
#if TRANSPORT_ENABLE_REAL_SRS
    if (!m_real_publisher) {
      m_real_publisher = std::make_unique<RtcPushTransport>();
    }
    if (!m_real_publisher->Open(m_publish_info, m_room_info,
                                m_publish_endpoint)) {
      m_real_publisher.reset();
      m_state = ConnectionState::kFailed;
      return false;
    }
    m_publish_resource_url = m_real_publisher->ResourceUrl().empty()
                                 ? m_publish_endpoint
                                 : m_real_publisher->ResourceUrl();
#else
    return false;
#endif
  } else {
    m_publish_resource_url = m_publish_endpoint;
  }
  return true;
}

void SrsTransportEngine::ClosePublishSessionLocked() {
  if (m_real_publisher) {
    m_real_publisher->Close();
    m_real_publisher.reset();
  }
  m_publish_endpoint.clear();
  m_publish_resource_url.clear();
}

bool SrsTransportEngine::RealWebRtcEnabledLocked() const {
#if TRANSPORT_ENABLE_REAL_SRS
  return EnvFlagEnabled("MEETING_SRS_ENABLE_REAL_WEBRTC");
#else
  return false;
#endif
}

std::string SrsTransportEngine::BuildPublishEndpointLocked() const {
  return BuildSrsEndpoint(m_room_info.push_server_url,
                          "whip",
                          m_room_info.srs_app,
                          m_room_info.room_id,
                          m_room_info.local_user_id,
                          m_room_info.srs_secret);
}

std::string SrsTransportEngine::BuildPullEndpointLocked(
    const std::string &user_id) const {
  return BuildSrsEndpoint(m_room_info.pull_server_url,
                          "whep",
                          m_room_info.srs_app,
                          m_room_info.room_id,
                          user_id,
                          "");
}

ConnectionStateCallback SrsTransportEngine::SetStateLocked(
    ConnectionState state) {
  const bool changed = m_state != state;
  m_state = state;
  return changed ? m_connection_state_callback : nullptr;
}

void SrsTransportEngine::SetState(ConnectionState state) {
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

} // namespace TRANSPORT
