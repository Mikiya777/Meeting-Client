#ifndef VCE_ENGINE_H
#define VCE_ENGINE_H

#include "engine/public/VceTypes.h"

#include <memory>
#include <mutex>
#include <string>

namespace VCE {

class VceEngineImpl;
class IMeetingEventObserver;

class VceEngine {
public:
  static std::shared_ptr<VceEngine> GetInstance();
  static void ReleaseInstance();

  ~VceEngine();

  Result Init();
  Result Uninit();

  Result OpenMicrophone();
  Result CloseMicrophone();
  Result OpenCamera();
  Result CloseCamera();

  // Fixed light smoothing; disabled by default.
  Result SetBeautyEnabled(bool enabled);

  Result GetMicrophoneDevices(std::vector<MediaDeviceInfo> *devices);
  Result GetCameraDevices(std::vector<MediaDeviceInfo> *devices);
  Result SetMicrophoneDevice(const std::string &device_id);
  Result SetCameraDevice(const std::string &device_id);

  Result AddRenderUser(const std::string &user_id, bool is_local);
  Result RemoveRenderUser(const std::string &user_id);
  Result RenderUser(const std::string &user_id);
  Result UpdateUserVideoSize(const std::string &user_id, int width, int height);

  Result RegisterUser(const std::string &username,
                      const std::string &password,
                      uint32_t client_ip = 0,
                      ServiceUserInfo *user_info = nullptr);
  Result Login(const std::string &username,
               const std::string &password,
               uint32_t client_ip = 0,
               ServiceUserInfo *user_info = nullptr);
  Result CreateMeeting(const std::string &title,
                       const std::string &description,
                       int64_t start_time,
                       MeetingInfo *meeting_info = nullptr);
  Result JoinMeeting(const std::string &meeting_id,
                     MeetingInfo *meeting_info = nullptr);
  Result LeaveMeeting();
  Result EndMeeting();
  Result ListMeetings(bool active_only, std::vector<MeetingInfo> *meetings);

  EngineState GetState() const;
  ServiceUserInfo GetCurrentUserInfo() const;
  MeetingInfo GetCurrentMeetingInfo() const;
  TransportRoomInfo GetCurrentTransportRoomInfo() const;

  VceEngine(const VceEngine &) = delete;
  VceEngine &operator=(const VceEngine &) = delete;

  void AddObserver(const std::shared_ptr<IMeetingEventObserver> &observer);
  void RemoveObserver(const std::shared_ptr<IMeetingEventObserver> &observer);

private:
  VceEngine();

  std::shared_ptr<VceEngineImpl> m_impl;

  static std::shared_ptr<VceEngine> m_instance;
  static std::mutex m_instance_mutex;
};

} // namespace VCE

#endif // VCE_ENGINE_H
