#include <memory>
#include <mutex>
#include <utility>

#include "engine/public/VceEngine.h"
#include "engine/private/VceEngineImpl.h"

namespace VCE {

std::shared_ptr<VceEngine> VceEngine::m_instance = nullptr;
std::mutex VceEngine::m_instance_mutex;

std::shared_ptr<VceEngine> VceEngine::GetInstance() {
  std::lock_guard<std::mutex> lock(m_instance_mutex);

  if (m_instance == nullptr) {
    m_instance = std::shared_ptr<VceEngine>(new VceEngine());
  }

  return m_instance;
}

void VceEngine::ReleaseInstance() {
  std::shared_ptr<VceEngine> instance;

  {
    std::lock_guard<std::mutex> lock(m_instance_mutex);
    instance = std::move(m_instance);
  }

  if (instance != nullptr) {
    instance->Uninit();
  }
}

VceEngine::VceEngine()
    : m_impl(std::make_shared<VceEngineImpl>()) {}

VceEngine::~VceEngine() = default;

//转发函数
Result VceEngine::Init() {
  return m_impl->Init();
}

Result VceEngine::Uninit() {
  return m_impl->Uninit();
}

Result VceEngine::OpenMicrophone() {
  return m_impl->OpenMicrophone();
}

Result VceEngine::CloseMicrophone() {
  return m_impl->CloseMicrophone();
}

Result VceEngine::SetBeautyEnabled(bool enabled) {
  return m_impl->SetBeautyEnabled(enabled);
}

Result VceEngine::OpenCamera() {
  return m_impl->OpenCamera();
}

Result VceEngine::CloseCamera() {
  return m_impl->CloseCamera();
}

Result VceEngine::GetMicrophoneDevices(
    std::vector<MediaDeviceInfo> *devices) {
  return m_impl->GetMicrophoneDevices(devices);
}

Result VceEngine::GetCameraDevices(
    std::vector<MediaDeviceInfo> *devices) {
  return m_impl->GetCameraDevices(devices);
}

Result VceEngine::SetMicrophoneDevice(const std::string &device_id) {
  return m_impl->SetMicrophoneDevice(device_id);
}

Result VceEngine::SetCameraDevice(const std::string &device_id) {
  return m_impl->SetCameraDevice(device_id);
}

Result VceEngine::AddRenderUser(const std::string &user_id, bool is_local) {
  return m_impl->AddRenderUser(user_id, is_local);
}

Result VceEngine::RemoveRenderUser(const std::string &user_id) {
  return m_impl->RemoveRenderUser(user_id);
}

Result VceEngine::RenderUser(const std::string &user_id) {
  return m_impl->RenderUser(user_id);
}

Result VceEngine::UpdateUserVideoSize(const std::string &user_id,
                                      int width,
                                      int height) {
  return m_impl->UpdateUserVideoSize(user_id, width, height);
}

Result VceEngine::RegisterUser(const std::string &username,
                               const std::string &password,
                               uint32_t client_ip,
                               ServiceUserInfo *user_info) {
  return m_impl->RegisterUser(username, password, client_ip, user_info);
}

Result VceEngine::Login(const std::string &username,
                        const std::string &password,
                        uint32_t client_ip,
                        ServiceUserInfo *user_info) {
  return m_impl->Login(username, password, client_ip, user_info);
}

Result VceEngine::CreateMeeting(const std::string &title,
                                const std::string &description,
                                int64_t start_time,
                                MeetingInfo *meeting_info) {
  return m_impl->CreateMeeting(title, description, start_time, meeting_info);
}

Result VceEngine::JoinMeeting(const std::string &meeting_id,
                              MeetingInfo *meeting_info) {
  return m_impl->JoinMeeting(meeting_id, meeting_info);
}

Result VceEngine::LeaveMeeting() {
  return m_impl->LeaveMeeting();
}

Result VceEngine::EndMeeting() {
  return m_impl->EndMeeting();
}

Result VceEngine::ListMeetings(bool active_only,
                               std::vector<MeetingInfo> *meetings) {
  return m_impl->ListMeetings(active_only, meetings);
}

EngineState VceEngine::GetState() const {
  return m_impl->GetState();
}

ServiceUserInfo VceEngine::GetCurrentUserInfo() const {
  return m_impl->GetCurrentUserInfo();
}

MeetingInfo VceEngine::GetCurrentMeetingInfo() const {
  return m_impl->GetCurrentMeetingInfo();
}

TransportRoomInfo VceEngine::GetCurrentTransportRoomInfo() const {
  return m_impl->GetCurrentTransportRoomInfo();
}

//观察者转发函数
void VceEngine::AddObserver(
    const std::shared_ptr<IMeetingEventObserver> &observer) {
  m_impl->AddObserver(observer);
}

void VceEngine::RemoveObserver(
    const std::shared_ptr<IMeetingEventObserver> &observer) {
  m_impl->RemoveObserver(observer);
}

} // namespace VCE
