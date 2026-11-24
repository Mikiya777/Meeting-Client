#include "engine/private/VceEngineImpl.h"
#include "engine/public/VceEngine.h"
#include "engine/public/VceTypes.h"
#include "engine/private/audio/AudioProcessingPipeline.h"
#include "engine/private/capture/CaptureManager.h"
#include "engine/public/IMeetingEventObserver.h"
#include "engine/private/render/RenderManager.h"
#include "engine/private/transport/TransportManager.h"
#include "engine/private/service/GrpcServiceManager.h"
#include "engine/private/service/MockServiceManager.h"

#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <algorithm>
#include <utility>

namespace VCE {
namespace {

constexpr int kDefaultVideoWidth = 640;
constexpr int kDefaultVideoHeight = 480;
constexpr int kDefaultVideoFps = 24;
constexpr int kDefaultAudioSampleRate = 48000;
constexpr int kDefaultAudioChannels = 2;
constexpr char kDefaultLocalUserId[] = "local";

class ScopeGuard {
public:
  explicit ScopeGuard(std::function<void()> cleanup)
      : m_cleanup(std::move(cleanup)) {}

  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;

  ScopeGuard(ScopeGuard &&other) noexcept
      : m_cleanup(std::move(other.m_cleanup)),
        m_active(other.m_active) {
    other.m_active = false;
  }

  ~ScopeGuard() {
    if (m_active && m_cleanup) {
      m_cleanup();
    }
  }

  void Dismiss() {
    m_active = false;
  }

private:
  std::function<void()> m_cleanup;
  bool m_active = true;
};

bool ContainsParticipant(const MeetingInfo &meeting_info,
                         const std::string &user_id) {
  for (const auto &participant : meeting_info.participants) {
    if (participant.user_id == user_id) {
      return true;
    }
  }
  return false;
}

bool ShouldUseGrpcServiceManager() {
  const char *value = std::getenv("MEETING_SERVICE_MANAGER");
  if (value == nullptr) {
    return false;
  }

  const std::string mode(value);
  return mode == "grpc" || mode == "server";
}

std::unique_ptr<IServiceManager> CreateDefaultServiceManager() {
  if (ShouldUseGrpcServiceManager()) {
    return std::make_unique<GrpcServiceManager>();
  }
  return std::make_unique<MockServiceManager>();
}

} // namespace

VceEngineImpl::VceEngineImpl()
    : m_capture_manager(std::make_unique<CaptureManager>()),
      m_render_manager(std::make_unique<RenderManager>()),
      m_transport_manager(std::make_unique<TransportManager>()),
      m_service_manager(CreateDefaultServiceManager()),
      m_audio_processing_pipeline(
          std::make_shared<AudioProcessingPipeline>()) {}

VceEngineImpl::VceEngineImpl(
    std::unique_ptr<ICaptureManager> capture_manager,
    std::unique_ptr<IRenderManager> render_manager,
    std::unique_ptr<ITransportManager> transport_manager,
    std::unique_ptr<IServiceManager> service_manager)
    : m_capture_manager(std::move(capture_manager)),
      m_render_manager(std::move(render_manager)),
      m_transport_manager(std::move(transport_manager)),
      m_service_manager(std::move(service_manager)),
      m_audio_processing_pipeline(
          std::make_shared<AudioProcessingPipeline>()) {}

VceEngineImpl::~VceEngineImpl() {
  Uninit();
}

//单例初始化(设置实例的各种初始属性)
Result VceEngineImpl::Init() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);
  //检查 Engine状态
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state == EngineState::kES_Initialized) {
      return Result::kRet_AlreadyInitialized;
    }

    if (m_state == EngineState::kES_ShuttingDown) {
      return Result::kRet_InternalError;
    }
  }

  const auto init_result = InitManagers();
  if (init_result != Result::kRet_Success) {
    return init_result;
  }

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_state = EngineState::kES_Initialized;
  }

  // Release both operation and state locks before invoking observers.
  operation_lock.unlock();
  NotifyEngineStateChanged(EngineState::kES_Initialized);
  return Result::kRet_Success;
}

Result VceEngineImpl::Uninit() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);
  //检查 engine状态 与 设置 ShuttingDown
  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state == EngineState::kES_Created ||
        m_state == EngineState::kES_Stopped) {
      return Result::kRet_Success;
    }

    if (m_state == EngineState::kES_ShuttingDown) {
      return Result::kRet_InternalError;
    }

    m_state = EngineState::kES_ShuttingDown;
  }

  // ShuttingDown rejects reentrant mutations while callbacks run unlocked.
  operation_lock.unlock();
  NotifyEngineStateChanged(EngineState::kES_ShuttingDown);
  operation_lock.lock();

  const auto result = UninitManagers();
  m_beauty_processor.SetEnabled(false);

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);
    m_state = EngineState::kES_Stopped;
  }

  // 回调可能重新调用 Engine，所以必须放在锁外。
  operation_lock.unlock();
  NotifyEngineStateChanged(EngineState::kES_Stopped);
  return result;
}

//ScopeGuard
Result VceEngineImpl::InitManagers() {
  if (!m_service_manager || !m_capture_manager ||
      !m_render_manager || !m_transport_manager) {
    return Result::kRet_InternalError;
  }

  const auto service_result = m_service_manager->Init();
  if (service_result != Result::kRet_Success) {
    return service_result;
  }
  ScopeGuard service_cleanup([this]() {
    m_service_manager->Uninit();
  });

  const auto capture_result = m_capture_manager->Init();
  if (capture_result != Result::kRet_Success) {
    return capture_result;
  }
  ScopeGuard capture_cleanup([this]() {
    m_capture_manager->Uninit();
  });

  const auto render_result = m_render_manager->Init();
  if (render_result != Result::kRet_Success) {
    return render_result;
  }
  ScopeGuard render_cleanup([this]() {
    m_render_manager->Uninit();
  });

  if (!m_audio_processing_pipeline ||
      !m_audio_processing_pipeline->Initialize(kDefaultAudioSampleRate,
                                               kDefaultAudioChannels)) {
    return Result::kRet_InternalError;
  }
  ScopeGuard audio_processing_cleanup([this]() {
    m_audio_processing_pipeline->Uninitialize();
  });

  TransportPublishConfig transport_config;
  transport_config.video_width = kDefaultVideoWidth;
  transport_config.video_height = kDefaultVideoHeight;
  transport_config.video_fps = kDefaultVideoFps;
  transport_config.audio_sample_rate = kDefaultAudioSampleRate;
  transport_config.audio_channels = kDefaultAudioChannels;

  const auto transport_result = m_transport_manager->Init(transport_config);
  if (transport_result != Result::kRet_Success) {
    return transport_result;
  }
  ScopeGuard transport_cleanup([this]() {
    m_transport_manager->Uninit();
  });

  //注册transport （关键技术点）
  //shared_from_this（） 让 VceEngineImpl 在自己的成员函数里，安全拿到一个指向自己的 std::shared_ptr。
  m_capture_manager->SetCaptureDataCallback(shared_from_this());
  m_transport_manager->SetTransportDataCallback(shared_from_this());
  m_service_manager->SetServiceDataCallback(shared_from_this());

  std::weak_ptr<AudioProcessingPipeline> audio_processing =
      m_audio_processing_pipeline;
  m_render_manager->SetPlaybackAudioCallback(
      [audio_processing](const float *samples,
                         size_t sample_count,
                         int sample_rate,
                         int channels) {
        if (auto pipeline = audio_processing.lock()) {
          pipeline->FeedPlaybackReference(samples,
                                          sample_count,
                                          sample_rate,
                                          channels);
        }
      });

  transport_cleanup.Dismiss();
  audio_processing_cleanup.Dismiss();
  render_cleanup.Dismiss();
  capture_cleanup.Dismiss();
  service_cleanup.Dismiss();
  return Result::kRet_Success;
}
Result VceEngineImpl::UninitManagers() {
  Result result = Result::kRet_Success;

  const auto remember_first_error = [&result](Result current) {
    if (current != Result::kRet_Success &&
        result == Result::kRet_Success) {
      result = current;
    }
  };

  m_render_manager->SetPlaybackAudioCallback(nullptr);
  remember_first_error(m_transport_manager->Uninit());
  remember_first_error(m_render_manager->Uninit());
  remember_first_error(m_capture_manager->Uninit());
  if (m_audio_processing_pipeline) {
    m_audio_processing_pipeline->Uninitialize();
  }
  remember_first_error(m_service_manager->Uninit());

  return result;
}

//capture Module
Result VceEngineImpl::OpenMicrophone() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state != EngineState::kES_Initialized) {
      return Result::kRet_NotInitialized;
    }
  }

  if (m_audio_processing_pipeline) {
    m_audio_processing_pipeline->SetCaptureActive(true);
  }

  const auto result = m_capture_manager->OpenMicrophone();
  if (result != Result::kRet_Success) {
    if (m_audio_processing_pipeline) {
      m_audio_processing_pipeline->SetCaptureActive(false);
    }
    return result;
  }
  auto transport_result = m_transport_manager->StartPublishAudio();
  if (transport_result != Result::kRet_Success) {
    m_capture_manager->CloseMicrophone();
    if (m_audio_processing_pipeline) {
      m_audio_processing_pipeline->SetCaptureActive(false);
    }
    return transport_result;
  }

  // 调用外部观察者之前必须释放 Engine 状态锁。
  operation_lock.unlock();
  if (result == Result::kRet_Success) {
    NotifyMicrophoneStateChanged(true);
  }

  return result;
}
Result VceEngineImpl::CloseMicrophone() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state != EngineState::kES_Initialized) {
      return Result::kRet_NotInitialized;
    }
  }

  Result result = Result::kRet_Success;

  const auto transport_result = m_transport_manager->StopPublishAudio();
  if (transport_result != Result::kRet_Success) {
    result = transport_result;
  }

  const auto capture_result = m_capture_manager->CloseMicrophone();
  if (capture_result != Result::kRet_Success &&
      result == Result::kRet_Success) {
    result = capture_result;
  }

  if (m_audio_processing_pipeline) {
    m_audio_processing_pipeline->SetCaptureActive(false);
  }

  // 调用外部观察者之前必须释放 Engine 状态锁。
  operation_lock.unlock();
  if (result == Result::kRet_Success) {
    NotifyMicrophoneStateChanged(false);
  }

  return result;
}
Result VceEngineImpl::SetBeautyEnabled(bool enabled) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  m_beauty_processor.SetEnabled(enabled);
  return Result::kRet_Success;
}

Result VceEngineImpl::OpenCamera() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state != EngineState::kES_Initialized) {
      return Result::kRet_NotInitialized;
    }
  }

  const auto result = m_capture_manager->OpenCamera();
  if (result != Result::kRet_Success) {
    return result;
  }
  auto transport_result = m_transport_manager->StartPublishCameraVideo();
  if (transport_result != Result::kRet_Success) {
    m_capture_manager->CloseCamera();
    return transport_result;
  }
  // 离开作用域后，m_state_mutex 已经自动释放。
  operation_lock.unlock();
  if (result == Result::kRet_Success) {
    NotifyCameraStateChanged(true);
  }

  return result;
}
Result VceEngineImpl::CloseCamera() {
  std::unique_lock<std::mutex> operation_lock(m_operation_mutex);

  {
    std::lock_guard<std::mutex> lock(m_state_mutex);

    if (m_state != EngineState::kES_Initialized) {
      return Result::kRet_NotInitialized;
    }
  }

  Result result = Result::kRet_Success;

  const auto transport_result = m_transport_manager->StopPublishCameraVideo();
  if (transport_result != Result::kRet_Success) {
    result = transport_result;
  }

  const auto capture_result = m_capture_manager->CloseCamera();
  if (capture_result != Result::kRet_Success &&
      result == Result::kRet_Success) {
    result = capture_result;
  }

  // 离开作用域后，m_state_mutex 已经自动释放。
  operation_lock.unlock();
  if (result == Result::kRet_Success) {
    NotifyCameraStateChanged(false);
  }

  return result;
}

Result VceEngineImpl::GetMicrophoneDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  return m_capture_manager->GetMicrophoneDevices(devices);
}

Result VceEngineImpl::GetCameraDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  return m_capture_manager->GetCameraDevices(devices);
}

Result VceEngineImpl::SetMicrophoneDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (device_id.empty()) {
    return Result::kRet_InvalidArgument;
  }
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  return m_capture_manager->SetMicrophoneDevice(device_id);
}

Result VceEngineImpl::SetCameraDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (device_id.empty()) {
    return Result::kRet_InvalidArgument;
  }
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  return m_capture_manager->SetCameraDevice(device_id);
}

Result VceEngineImpl::AddRenderUser(const std::string &user_id, bool is_local) {
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_state != EngineState::kES_Initialized) {
    return Result::kRet_NotInitialized;
  }

  return m_render_manager->AddUser(user_id, is_local);
}

Result VceEngineImpl::RemoveRenderUser(const std::string &user_id) {
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_state != EngineState::kES_Initialized) {
    return Result::kRet_NotInitialized;
  }

  return m_render_manager->RemoveUser(user_id);
}

Result VceEngineImpl::RenderUser(const std::string &user_id) {
  if (user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_state != EngineState::kES_Initialized) {
    return Result::kRet_NotInitialized;
  }

  return m_render_manager->RenderUser(user_id);
}

Result VceEngineImpl::UpdateUserVideoSize(const std::string &user_id,
                                          int width,
                                          int height) {
  if (user_id.empty() || width <= 0 || height <= 0) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_state_mutex);
  if (m_state != EngineState::kES_Initialized) {
    return Result::kRet_NotInitialized;
  }

  return m_render_manager->UpdateUserVideoSize(user_id, width, height);
}

Result VceEngineImpl::RegisterUser(const std::string &username,
                                   const std::string &password,
                                   uint32_t client_ip,
                                   ServiceUserInfo *user_info) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo registered_user;
  const auto result =
      m_service_manager->RegisterUser(username, password, client_ip,
                                      &registered_user);
  if (result != Result::kRet_Success) {
    return result;
  }

  if (user_info) {
    *user_info = registered_user;
  }
  return Result::kRet_Success;
}

Result VceEngineImpl::Login(const std::string &username,
                            const std::string &password,
                            uint32_t client_ip,
                            ServiceUserInfo *user_info) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo logged_in_user;
  const auto result =
      m_service_manager->Login(username, password, client_ip, &logged_in_user);
  if (result != Result::kRet_Success) {
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    m_current_user = logged_in_user;
    m_local_user_id = logged_in_user.user_id.empty()
                          ? kDefaultLocalUserId
                          : logged_in_user.user_id;
  }

  if (user_info) {
    *user_info = logged_in_user;
  }
  return Result::kRet_Success;
}

Result VceEngineImpl::CreateMeeting(const std::string &title,
                                    const std::string &description,
                                    int64_t start_time,
                                    MeetingInfo *meeting_info) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo current_user;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_user = m_current_user;
  }

  if (current_user.user_id.empty()) {
    return Result::kRet_InvalidState;
  }

  MeetingInfo created_meeting;
  const auto result =
      m_service_manager->CreateMeeting(current_user, title, description,
                                       start_time, &created_meeting);
  if (result != Result::kRet_Success) {
    return result;
  }

  const auto transport_result = ApplyMeetingTransportInfo(created_meeting);
  if (transport_result != Result::kRet_Success) {
    return transport_result;
  }

  if (meeting_info) {
    *meeting_info = created_meeting;
  }
  return Result::kRet_Success;
}

Result VceEngineImpl::JoinMeeting(const std::string &meeting_id,
                                  MeetingInfo *meeting_info) {
  if (meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo current_user;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_user = m_current_user;
  }

  if (current_user.user_id.empty()) {
    return Result::kRet_InvalidState;
  }

  MeetingInfo joined_meeting;
  const auto result =
      m_service_manager->JoinMeeting(current_user, meeting_id,
                                     &joined_meeting);
  if (result != Result::kRet_Success) {
    return result;
  }

  const auto transport_result = ApplyMeetingTransportInfo(joined_meeting);
  if (transport_result != Result::kRet_Success) {
    return transport_result;
  }

  if (meeting_info) {
    *meeting_info = joined_meeting;
  }
  return Result::kRet_Success;
}

Result VceEngineImpl::LeaveMeeting() {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo current_user;
  std::string meeting_id;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_user = m_current_user;
    meeting_id = m_current_meeting.meeting_id;
  }

  if (current_user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidState;
  }

  std::vector<ServiceUserInfo> participants;
  const auto result =
      m_service_manager->LeaveMeeting(current_user, meeting_id,
                                      &participants);
  ClearCurrentMeetingTransport();
  return result;
}

Result VceEngineImpl::EndMeeting() {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo current_user;
  std::string meeting_id;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_user = m_current_user;
    meeting_id = m_current_meeting.meeting_id;
  }

  if (current_user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidState;
  }

  std::vector<ServiceUserInfo> participants;
  const auto result =
      m_service_manager->EndMeeting(current_user, meeting_id, &participants);
  ClearCurrentMeetingTransport();
  return result;
}

Result VceEngineImpl::ListMeetings(
    bool active_only,
    std::vector<MeetingInfo> *meetings) {
  std::lock_guard<std::mutex> operation_lock(m_operation_mutex);
  if (!IsInitialized()) {
    return Result::kRet_NotInitialized;
  }
  if (!meetings) {
    return Result::kRet_InvalidArgument;
  }

  return m_service_manager->ListMeetings(active_only, meetings);
}

EngineState VceEngineImpl::GetState() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_state;
}

ServiceUserInfo VceEngineImpl::GetCurrentUserInfo() const {
  std::lock_guard<std::mutex> lock(m_session_mutex);
  return m_current_user;
}

MeetingInfo VceEngineImpl::GetCurrentMeetingInfo() const {
  std::lock_guard<std::mutex> lock(m_session_mutex);
  return m_current_meeting;
}

TransportRoomInfo VceEngineImpl::GetCurrentTransportRoomInfo() const {
  return m_transport_manager->GetRoomInfo();
}

bool VceEngineImpl::IsInitialized() const {
  std::lock_guard<std::mutex> lock(m_state_mutex);
  return m_state == EngineState::kES_Initialized;
}

Result VceEngineImpl::ApplyMeetingTransportInfo(
    const MeetingInfo &meeting_info) {
  if (meeting_info.meeting_id.empty() || !meeting_info.active) {
    return Result::kRet_InvalidArgument;
  }

  ServiceUserInfo current_user;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_user = m_current_user;
  }

  if (current_user.user_id.empty()) {
    return Result::kRet_InvalidState;
  }

  TransportRoomInfo room_info;
  room_info.local_user_id = current_user.user_id;
  room_info.room_id = meeting_info.meeting_id;
  room_info.push_server_url = meeting_info.media_route.push_server_url;
  room_info.pull_server_url = meeting_info.media_route.pull_server_url;
  room_info.srs_app = meeting_info.media_route.srs_app;
  room_info.srs_secret = meeting_info.media_route.srs_secret;

  const auto set_room_result = m_transport_manager->SetRoomInfo(room_info);
  if (set_room_result != Result::kRet_Success) {
    return set_room_result;
  }

  for (const auto &participant : meeting_info.participants) {
    if (participant.user_id.empty() ||
        participant.user_id == current_user.user_id) {
      continue;
    }

    m_transport_manager->SubscribeUserAV(participant.user_id);
  }

  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    m_current_meeting = meeting_info;
    m_local_user_id = current_user.user_id;
  }
  return Result::kRet_Success;
}

void VceEngineImpl::ClearCurrentMeetingTransport() {
  MeetingInfo current_meeting;
  std::string local_user_id;
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    current_meeting = m_current_meeting;
    local_user_id = m_local_user_id;
    m_current_meeting = MeetingInfo{};
  }

  m_transport_manager->StopPublishAudio();
  m_transport_manager->StopPublishCameraVideo();

  for (const auto &participant : current_meeting.participants) {
    if (participant.user_id.empty() || participant.user_id == local_user_id) {
      continue;
    }
    m_transport_manager->UnsubscribeUserAV(participant.user_id);
  }
}

//observer Module
//添加观察者
void VceEngineImpl::AddObserver(const std::shared_ptr<IMeetingEventObserver> &observer) {
  if (!observer) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_observer_mutex);

  for (auto it = m_observers.begin(); it != m_observers.end();) {
    auto current = it->lock();

    if (!current) {
      it = m_observers.erase(it);
      continue;
    }

    if (current == observer) {
      return;
    }

    ++it;
  }

  m_observers.emplace_back(observer);
}

//删除观察者
void VceEngineImpl::RemoveObserver(const std::shared_ptr<IMeetingEventObserver> &observer) {
  std::lock_guard<std::mutex> lock(m_observer_mutex);

  m_observers.erase(
      std::remove_if(
          m_observers.begin(),
          m_observers.end(),
          [&observer](const std::weak_ptr<IMeetingEventObserver> &item) {
            auto current = item.lock();

            // 同时删除已失效对象和指定观察者。
            return !current || current == observer;
          }),
      m_observers.end());
}

//在engine处浅拷贝观察者，返回还活着的那些observer
std::vector<std::shared_ptr<IMeetingEventObserver>> VceEngineImpl::GetActiveObservers() {
  std::vector<std::shared_ptr<IMeetingEventObserver>> active_observers;

  std::lock_guard<std::mutex> lock(m_observer_mutex);

  for (auto it = m_observers.begin(); it != m_observers.end();) {
    //检查observer 对象还活着吗？
    auto observer = it->lock(); // weak ptr 升级 增加引用计数 临时延长对象生命周期 可以安全调用对象方法

    if (!observer) {
      it = m_observers.erase(it);
      continue;
    }

    active_observers.push_back(std::move(observer));
    ++it;
  }

  return active_observers;
}
//通知引擎状态
void VceEngineImpl::NotifyEngineStateChanged(EngineState state) {
  const auto observers = GetActiveObservers();

  for (const auto &observer : observers) {
    observer->OnEngineStateChanged(state);
  }
}
//通知相机状态
void VceEngineImpl::NotifyCameraStateChanged(bool enabled) {
  const auto observers = GetActiveObservers();

  // 回调期间不持有 m_observer_mutex。
  // 因为回调可能再次进入 Engine，持锁会有死锁风险。
  for (const auto &observer : observers) {
    observer->OnCameraStateChanged(enabled);
  }
}
//通知麦克风状态
void VceEngineImpl::NotifyMicrophoneStateChanged(bool enabled) {
  const auto observers = GetActiveObservers();

  // active_observers 中的 shared_ptr 保证回调期间对象仍然存活。
  for (const auto &observer : observers) {
    observer->OnMicrophoneStateChanged(enabled);
  }
}

//Transport Module
//通知Transport
void VceEngineImpl::NotifyTransportConnectionStateChanged(bool connected) {
  const auto observers = GetActiveObservers();

  for (const auto &observer : observers) {
    observer->OnTransportConnectionStateChanged(connected);
  }
}

void VceEngineImpl::NotifyRenderVideoFrameReady(const std::string &user_id) {
  const auto observers = GetActiveObservers();

  for (const auto &observer : observers) {
    observer->OnRenderVideoFrameReady(user_id);
  }
}

void VceEngineImpl::OnCaptureVideoFrame(const std::shared_ptr<I420Frame> &frame) {
  if (!frame || !frame->HasData()) {
    return;
  }

  // Process once before preview and transport fan-out; never alter capture data.
  const auto processed_frame = m_beauty_processor.Process(frame);
  auto local_frame = std::make_shared<I420Frame>(*processed_frame);
  if (local_frame->user_id.empty()) {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    local_frame->user_id = m_local_user_id.empty()
                               ? kDefaultLocalUserId
                               : m_local_user_id;
  }

  if (m_render_manager->RenderLocalVideoFrame(*local_frame) ==
      Result::kRet_Success) {
    NotifyRenderVideoFrameReady(local_frame->user_id);
  }
  m_transport_manager->PushVideoFrame(local_frame);
}

void VceEngineImpl::OnCaptureAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
  if (!frame || !frame->HasData()) {
    return;
  }

  auto local_frame = std::make_shared<AudioFrame>(*frame);
  if (local_frame->user_id.empty()) {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    local_frame->user_id = m_local_user_id.empty()
                               ? kDefaultLocalUserId
                               : m_local_user_id;
  }

  std::shared_ptr<AudioFrame> processed_frame = local_frame;
  if (m_audio_processing_pipeline) {
    auto next_frame =
        m_audio_processing_pipeline->ProcessCaptureFrame(*local_frame);
    if (next_frame && next_frame->HasData()) {
      processed_frame = std::move(next_frame);
    }
  }

  m_transport_manager->PushAudioFrame(processed_frame);
}

void VceEngineImpl::OnTransportVideoFrame(const std::string &user_id,
                                          const std::shared_ptr<I420Frame> &frame) {
  if (user_id.empty()) {
    return;
  }

  if (!frame || !frame->HasData()) {
    return;
  }

  VideoFrame transport_frame = *frame;
  transport_frame.user_id = user_id;
  if (m_render_manager->RenderRemoteVideoFrame(transport_frame) == Result::kRet_Success) {
    NotifyRenderVideoFrameReady(user_id);
  }
}

void VceEngineImpl::OnTransportAudioFrame(const std::string &user_id,
                                          const std::shared_ptr<AudioFrame> &frame) {
  if (user_id.empty()) {
    return;
  }

  if (!frame || !frame->HasData()) {
    return;
  }

  AudioFrame transport_frame = *frame;
  transport_frame.user_id = user_id;
  m_render_manager->PlayRemoteAudioFrame(transport_frame);
}

void VceEngineImpl::OnTransportConnectionStateChanged(TransportState state) {
  const bool connected = state == TransportState::kConnected;
  NotifyTransportConnectionStateChanged(connected);
}

//Service Module
void VceEngineImpl::NotifyUserJoined(const ServiceUserInfo &user) {
  if (user.user_id.empty()) {
    return;
  }

  const auto observers = GetActiveObservers();
  for (const auto &observer : observers) {
    observer->OnMeetingUserJoined(user.user_id, user.username);
  }
}
void VceEngineImpl::NotifyUserLeft(const std::string &user_id) {
  if (user_id.empty()) {
    return;
  }

  const auto observers = GetActiveObservers();
  for (const auto &observer : observers) {
    observer->OnMeetingUserLeft(user_id);
  }
}
void VceEngineImpl::NotifyMeetingEnded() {
  const auto observers = GetActiveObservers();
  for (const auto &observer : observers) {
    observer->OnMeetingEnded();
  }
}

void VceEngineImpl::OnUserJoined(const ServiceUserInfo &user) {
  if (user.user_id.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    if (user.user_id == m_local_user_id) {
      return;
    }
    if (!m_current_meeting.meeting_id.empty() &&
        !ContainsParticipant(m_current_meeting, user.user_id)) {
      m_current_meeting.participants.push_back(user);
    }
  }
  m_transport_manager->SubscribeUserAV(user.user_id);
  NotifyUserJoined(user);
}

void VceEngineImpl::OnUserLeft(const std::string &user_id) {
  if (user_id.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(m_session_mutex);
    auto &participants = m_current_meeting.participants;
    participants.erase(
        std::remove_if(participants.begin(),
                       participants.end(),
                       [&user_id](const ServiceUserInfo &participant) {
                         return participant.user_id == user_id;
                       }),
        participants.end());
  }
  m_transport_manager->UnsubscribeUserAV(user_id);
  NotifyUserLeft(user_id);
}

void VceEngineImpl::OnMeetingEnded() {
  ClearCurrentMeetingTransport();
  NotifyMeetingEnded();
}

} // namespace VCE
