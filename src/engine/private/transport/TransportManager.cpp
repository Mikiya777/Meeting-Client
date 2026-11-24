#include "engine/private/transport/TransportManager.h"

#include <utility>

namespace VCE {
namespace {

constexpr size_t kMaxQueuedFrameCount = 30;

bool IsValidPublishConfig(const TransportPublishConfig &config) {
  return config.video_width > 0 &&
         config.video_height > 0 &&
         config.video_fps > 0 &&
         config.audio_sample_rate > 0 &&
         config.audio_channels > 0;
}

bool IsValidRoomInfo(const TransportRoomInfo &info) {
  return !info.local_user_id.empty() && !info.room_id.empty();
}

} // namespace

TransportManager::~TransportManager() {
  Uninit();
}

Result TransportManager::Init(const TransportPublishConfig &config) {
  if (!IsValidPublishConfig(config)) {
    return Result::kRet_InvalidArgument;
  }

  auto transport_engine = TRANSPORT::ITransportEngine::Create();
  if (!transport_engine) {
    return Result::kRet_InternalError;
  }

  TRANSPORT::PublishInfo publish_info;
  publish_info.video_width = config.video_width;
  publish_info.video_height = config.video_height;
  publish_info.video_fps = config.video_fps;
  publish_info.audio_sample_rate = config.audio_sample_rate;
  publish_info.audio_channels = config.audio_channels;

  if (!transport_engine->Initialize(publish_info)) {
    return Result::kRet_InternalError;
  }

  transport_engine->RegisterVideoCallback(
      [this](const std::string &user_id,
             const std::shared_ptr<TRANSPORT::I420Frame> &frame) {
        OnEngineVideoFrame(user_id, frame);
      });
  transport_engine->RegisterAudioCallback(
      [this](const std::string &user_id,
             const std::shared_ptr<TRANSPORT::AudioFrame> &frame) {
        OnEngineAudioFrame(user_id, frame);
      });
  transport_engine->RegisterConnectionStateCallback(
      [this](TRANSPORT::ConnectionState state) {
        OnEngineConnectionStateChanged(state);
      });

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (m_initialized) {
      transport_engine->Uninit();
      return Result::kRet_AlreadyInitialized;
    }

    m_transport_engine = std::move(transport_engine);
    m_publish_config = config;
    m_camera_video_publishing = false;
    m_audio_publishing = false;
    m_subscribed_users.clear();
    m_sent_video_frame_count = 0;
    m_sent_audio_frame_count = 0;
    m_dropped_video_frame_count = 0;
    m_dropped_audio_frame_count = 0;
    m_last_sent_video_timestamp_us = 0;
    m_last_sent_audio_timestamp_us = 0;
    m_initialized = true;
    m_state = TransportState::kDisconnected;
  }

  StartConsumerThreads();
  return Result::kRet_Success;
}

Result TransportManager::Uninit() {
  bool should_notify_closed = false;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_Success;
    }

    m_initialized = false;
    m_camera_video_publishing = false;
    m_audio_publishing = false;
    m_subscribed_users.clear();
    should_notify_closed = m_state != TransportState::kClosed;
    m_state = TransportState::kClosed;
  }

  StopConsumerThreads();

  if (m_transport_engine) {
    m_transport_engine->Uninit();
    m_transport_engine.reset();
  }

  if (should_notify_closed) {
    if (auto callback = GetCallback()) {
      callback->OnTransportConnectionStateChanged(TransportState::kClosed);
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_callback_mutex);
    m_callback.reset();
  }

  return Result::kRet_Success;
}

Result TransportManager::SetRoomInfo(const TransportRoomInfo &info) {
  if (!IsValidRoomInfo(info)) {
    return Result::kRet_InvalidArgument;
  }

  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }

    m_room_info = info;
    engine = m_transport_engine.get();
  }

  if (engine) {
    TRANSPORT::TargetRoomInfo target_info;
    target_info.local_user_id = info.local_user_id;
    target_info.room_id = info.room_id;
    target_info.push_server_url = info.push_server_url;
    target_info.pull_server_url = info.pull_server_url;
    target_info.srs_app = info.srs_app;
    target_info.srs_secret = info.srs_secret;
    engine->SetTargetRoomInfo(target_info);
  }
  return Result::kRet_Success;
}

TransportRoomInfo TransportManager::GetRoomInfo() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_room_info;
}

Result TransportManager::StartPublishCameraVideo() {
  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->StartPublishVideo()) {
    return Result::kRet_InternalError;
  }

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_camera_video_publishing = engine->IsPublishingVideo();
  }

  return Result::kRet_Success;
}

Result TransportManager::StopPublishCameraVideo() {
  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  std::queue<std::shared_ptr<I420Frame>> discarded_frames;
  if (!engine || !engine->StopPublishVideo()) {
    return Result::kRet_InternalError;
  }

  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_camera_video_publishing = engine->IsPublishingVideo();
    if (!m_camera_video_publishing) {
      std::lock_guard<std::mutex> queue_lock(m_video_mutex);
      m_video_queue.swap(discarded_frames);
    }
    should_disconnect = !m_camera_video_publishing && !m_audio_publishing &&
                        m_subscribed_users.empty();
  }

  if (should_disconnect) {
    SetState(TransportState::kDisconnected);
  }
  return Result::kRet_Success;
}

Result TransportManager::StartPublishAudio() {
  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->StartPublishAudio()) {
    return Result::kRet_InternalError;
  }

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_audio_publishing = engine->IsPublishingAudio();
  }

  return Result::kRet_Success;
}

Result TransportManager::StopPublishAudio() {
  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  std::queue<std::shared_ptr<AudioFrame>> discarded_frames;
  if (!engine || !engine->StopPublishAudio()) {
    return Result::kRet_InternalError;
  }

  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_audio_publishing = engine->IsPublishingAudio();
    if (!m_audio_publishing) {
      std::lock_guard<std::mutex> queue_lock(m_audio_mutex);
      m_audio_queue.swap(discarded_frames);
    }
    should_disconnect = !m_audio_publishing && !m_camera_video_publishing &&
                        m_subscribed_users.empty();
  }

  if (should_disconnect) {
    SetState(TransportState::kDisconnected);
  }
  return Result::kRet_Success;
}

Result TransportManager::PushVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  // Keep the state check and enqueue ordered with stop and queue clearing.
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (!m_camera_video_publishing) {
    return Result::kRet_InvalidState;
  }

  if (!frame || frame->user_id.empty() || !frame->HasData()) {
    return Result::kRet_InvalidArgument;
  }

  return EnqueueVideoFrame(frame);
}

Result TransportManager::PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  // Keep the state check and enqueue ordered with stop and queue clearing.
  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (!m_audio_publishing) {
    return Result::kRet_InvalidState;
  }

  if (!frame || frame->user_id.empty() || !frame->HasData()) {
    return Result::kRet_InvalidArgument;
  }

  return EnqueueAudioFrame(frame);
}

Result TransportManager::SubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->SubscribeUserAV(user_id)) {
    return Result::kRet_InternalError;
  }

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_subscribed_users.insert(user_id);
  }

  return Result::kRet_Success;
}

Result TransportManager::UnsubscribeUserAV(const std::string &user_id) {
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->UnsubscribeUserAV(user_id)) {
    return Result::kRet_InternalError;
  }

  bool should_disconnect = false;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_subscribed_users.erase(user_id);
    should_disconnect =
        m_subscribed_users.empty() &&
        !m_camera_video_publishing &&
        !m_audio_publishing;
  }

  if (should_disconnect) {
    SetState(TransportState::kDisconnected);
  }
  return Result::kRet_Success;
}

Result TransportManager::SubscribeUsersAV(const std::vector<std::string> &user_ids) {
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

Result TransportManager::UnsubscribeUsersAV(const std::vector<std::string> &user_ids) {
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

bool TransportManager::IsPublishingCameraVideo() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_camera_video_publishing;
}

bool TransportManager::IsPublishingAudio() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_audio_publishing;
}

bool TransportManager::IsUserSubscribedAV(const std::string &user_id) const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_subscribed_users.find(user_id) != m_subscribed_users.end();
}

std::vector<std::string> TransportManager::GetSubscribedUsers() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return {m_subscribed_users.begin(), m_subscribed_users.end()};
}

void TransportManager::SetTransportDataCallback(
    const std::shared_ptr<ITransportDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  m_callback = callback;
}

size_t TransportManager::GetQueuedVideoFrameCount() const {
  std::lock_guard<std::mutex> lock(m_video_mutex);
  return m_video_queue.size();
}

size_t TransportManager::GetQueuedAudioFrameCount() const {
  std::lock_guard<std::mutex> lock(m_audio_mutex);
  return m_audio_queue.size();
}

int TransportManager::GetSentVideoFrameCount() const {
  return m_sent_video_frame_count.load();
}

int TransportManager::GetSentAudioFrameCount() const {
  return m_sent_audio_frame_count.load();
}

int TransportManager::GetDroppedVideoFrameCount() const {
  return m_dropped_video_frame_count.load();
}

int TransportManager::GetDroppedAudioFrameCount() const {
  return m_dropped_audio_frame_count.load();
}

int64_t TransportManager::GetLastSentVideoTimestampUs() const {
  return m_last_sent_video_timestamp_us.load();
}

int64_t TransportManager::GetLastSentAudioTimestampUs() const {
  return m_last_sent_audio_timestamp_us.load();
}

TransportState TransportManager::GetTransportState() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_state;
}

void TransportManager::InjectRemoteVideoFrame(
    const std::string &user_id,
    const std::shared_ptr<I420Frame> &frame) {
  if (user_id.empty() || !frame || !frame->HasData()) {
    return;
  }

  if (auto callback = GetCallback()) {
    callback->OnTransportVideoFrame(user_id, frame);
  }
}

void TransportManager::InjectRemoteAudioFrame(
    const std::string &user_id,
    const std::shared_ptr<AudioFrame> &frame) {
  if (user_id.empty() || !frame || !frame->HasData()) {
    return;
  }

  if (auto callback = GetCallback()) {
    callback->OnTransportAudioFrame(user_id, frame);
  }
}

void TransportManager::StartConsumerThreads() {
  {
    std::scoped_lock lock(m_video_mutex, m_audio_mutex);
    if (m_running) {
      return;
    }
    m_running = true;
  }

  m_video_consumer_thread = std::thread(&TransportManager::VideoConsumerThread, this);
  m_audio_consumer_thread = std::thread(&TransportManager::AudioConsumerThread, this);
}

void TransportManager::StopConsumerThreads() {
  {
    std::scoped_lock lock(m_video_mutex, m_audio_mutex);
    if (!m_running) {
      return;
    }
    m_running = false;
  }

  m_video_condition.notify_all();
  m_audio_condition.notify_all();

  if (m_video_consumer_thread.joinable()) {
    m_video_consumer_thread.join();
  }

  if (m_audio_consumer_thread.joinable()) {
    m_audio_consumer_thread.join();
  }

  {
    std::lock_guard<std::mutex> lock(m_video_mutex);
    while (!m_video_queue.empty()) {
      m_video_queue.pop();
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_audio_mutex);
    while (!m_audio_queue.empty()) {
      m_audio_queue.pop();
    }
  }
}

void TransportManager::VideoConsumerThread() {
  while (true) {
    std::shared_ptr<I420Frame> frame;
    {
      std::unique_lock<std::mutex> lock(m_video_mutex);
      m_video_condition.wait(lock, [this]() {
        return !m_running || !m_video_queue.empty();
      });

      if (!m_running) {
        return;
      }

      frame = std::move(m_video_queue.front());
      m_video_queue.pop();
    }

    SendVideoFrame(frame);
  }
}

void TransportManager::AudioConsumerThread() {
  while (true) {
    std::shared_ptr<AudioFrame> frame;
    {
      std::unique_lock<std::mutex> lock(m_audio_mutex);
      m_audio_condition.wait(lock, [this]() {
        return !m_running || !m_audio_queue.empty();
      });

      if (!m_running) {
        return;
      }

      frame = std::move(m_audio_queue.front());
      m_audio_queue.pop();
    }

    SendAudioFrame(frame);
  }
}

Result TransportManager::EnqueueVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  {
    std::lock_guard<std::mutex> lock(m_video_mutex);
    while (m_video_queue.size() >= kMaxQueuedFrameCount) {
      m_video_queue.pop();
      ++m_dropped_video_frame_count;
    }
    m_video_queue.push(frame);
  }

  m_video_condition.notify_one();
  return Result::kRet_Success;
}

Result TransportManager::EnqueueAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  {
    std::lock_guard<std::mutex> lock(m_audio_mutex);
    while (m_audio_queue.size() >= kMaxQueuedFrameCount) {
      m_audio_queue.pop();
      ++m_dropped_audio_frame_count;
    }
    m_audio_queue.push(frame);
  }

  m_audio_condition.notify_one();
  return Result::kRet_Success;
}

void TransportManager::SendVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  if (!frame || !frame->HasData()) {
    return;
  }

  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized || !m_camera_video_publishing) {
      return;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->PushVideoFrame(ConvertToTransportVideoFrame(frame))) {
    return;
  }

  m_last_sent_video_timestamp_us = frame->timestamp_us;
  ++m_sent_video_frame_count;
}

void TransportManager::SendAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  if (!frame || !frame->HasData()) {
    return;
  }

  TRANSPORT::ITransportEngine *engine = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized || !m_audio_publishing) {
      return;
    }
    engine = m_transport_engine.get();
  }

  if (!engine || !engine->PushAudioFrame(ConvertToTransportAudioFrame(frame))) {
    return;
  }

  m_last_sent_audio_timestamp_us = frame->timestamp_us;
  ++m_sent_audio_frame_count;
}

std::shared_ptr<TRANSPORT::I420Frame> TransportManager::ConvertToTransportVideoFrame(
    const std::shared_ptr<I420Frame> &frame) const {
  if (!frame) {
    return nullptr;
  }

  auto transport_frame = std::make_shared<TRANSPORT::I420Frame>();
  transport_frame->data[0] = frame->data[0];
  transport_frame->data[1] = frame->data[1];
  transport_frame->data[2] = frame->data[2];
  transport_frame->user_id = frame->user_id;
  transport_frame->width = frame->width;
  transport_frame->height = frame->height;
  transport_frame->timestamp_us = frame->timestamp_us;
  return transport_frame;
}

std::shared_ptr<TRANSPORT::AudioFrame> TransportManager::ConvertToTransportAudioFrame(
    const std::shared_ptr<AudioFrame> &frame) const {
  if (!frame) {
    return nullptr;
  }

  auto transport_frame = std::make_shared<TRANSPORT::AudioFrame>();
  transport_frame->data = frame->data;
  transport_frame->user_id = frame->user_id;
  transport_frame->sample_rate = frame->sample_rate;
  transport_frame->channels = frame->channels;
  transport_frame->samples = frame->samples;
  transport_frame->timestamp_us = frame->timestamp_us;
  return transport_frame;
}

std::shared_ptr<I420Frame> TransportManager::ConvertToVceVideoFrame(
    const std::shared_ptr<TRANSPORT::I420Frame> &frame) const {
  if (!frame) {
    return nullptr;
  }

  auto vce_frame = std::make_shared<I420Frame>();
  vce_frame->data[0] = frame->data[0];
  vce_frame->data[1] = frame->data[1];
  vce_frame->data[2] = frame->data[2];
  vce_frame->user_id = frame->user_id;
  vce_frame->width = frame->width;
  vce_frame->height = frame->height;
  vce_frame->timestamp_us = frame->timestamp_us;
  return vce_frame;
}

std::shared_ptr<AudioFrame> TransportManager::ConvertToVceAudioFrame(
    const std::shared_ptr<TRANSPORT::AudioFrame> &frame) const {
  if (!frame) {
    return nullptr;
  }

  auto vce_frame = std::make_shared<AudioFrame>();
  vce_frame->data = frame->data;
  vce_frame->user_id = frame->user_id;
  vce_frame->sample_rate = frame->sample_rate;
  vce_frame->channels = frame->channels;
  vce_frame->samples = frame->samples;
  vce_frame->timestamp_us = frame->timestamp_us;
  return vce_frame;
}

TransportState TransportManager::ConvertTransportState(
    TRANSPORT::ConnectionState state) {
  switch (state) {
  case TRANSPORT::ConnectionState::kDisconnected:
    return TransportState::kDisconnected;
  case TRANSPORT::ConnectionState::kConnecting:
    return TransportState::kConnecting;
  case TRANSPORT::ConnectionState::kConnected:
    return TransportState::kConnected;
  case TRANSPORT::ConnectionState::kDisconnecting:
    return TransportState::kDisconnecting;
  case TRANSPORT::ConnectionState::kFailed:
    return TransportState::kFailed;
  case TRANSPORT::ConnectionState::kClosed:
    return TransportState::kClosed;
  }

  return TransportState::kFailed;
}

void TransportManager::SetState(TransportState state) {
  bool changed = false;
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (!m_initialized && state != TransportState::kClosed) {
      return;
    }

    changed = m_state != state;
    m_state = state;
  }

  if (changed) {
    if (auto callback = GetCallback()) {
      callback->OnTransportConnectionStateChanged(state);
    }
  }
}

void TransportManager::OnEngineVideoFrame(
    const std::string &user_id,
    const std::shared_ptr<TRANSPORT::I420Frame> &frame) {
  auto vce_frame = ConvertToVceVideoFrame(frame);
  if (!vce_frame || !vce_frame->HasData()) {
    return;
  }

  if (auto callback = GetCallback()) {
    callback->OnTransportVideoFrame(user_id, vce_frame);
  }
}

void TransportManager::OnEngineAudioFrame(
    const std::string &user_id,
    const std::shared_ptr<TRANSPORT::AudioFrame> &frame) {
  auto vce_frame = ConvertToVceAudioFrame(frame);
  if (!vce_frame || !vce_frame->HasData()) {
    return;
  }

  if (auto callback = GetCallback()) {
    callback->OnTransportAudioFrame(user_id, vce_frame);
  }
}

void TransportManager::OnEngineConnectionStateChanged(
    TRANSPORT::ConnectionState state) {
  SetState(ConvertTransportState(state));
}

std::shared_ptr<ITransportDataCallback> TransportManager::GetCallback() const {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  return m_callback.lock();
}

} // namespace VCE
