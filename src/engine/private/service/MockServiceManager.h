#ifndef MOCKSERVICEMANAGER_H
#define MOCKSERVICEMANAGER_H

#include "engine/private/service/IServiceManager.h"

#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

namespace VCE {
class MockServiceManager final : public IServiceManager {
public:
  MockServiceManager() = default;
  ~MockServiceManager() override = default;

  //接口方法
  Result Init() override;
  Result Uninit() override;
  void SetServiceDataCallback(const std::shared_ptr<IServiceDataCallback> &callback) override;

  Result RegisterUser(const std::string &username,
                      const std::string &password,
                      uint32_t client_ip,
                      ServiceUserInfo *user_info) override;
  Result Login(const std::string &username,
               const std::string &password,
               uint32_t client_ip,
               ServiceUserInfo *user_info) override;

  Result CreateMeeting(const ServiceUserInfo &creator,
                       const std::string &title,
                       const std::string &description,
                       int64_t start_time,
                       MeetingInfo *meeting_info) override;
  Result JoinMeeting(const ServiceUserInfo &user,
                     const std::string &meeting_id,
                     MeetingInfo *meeting_info) override;
  Result LeaveMeeting(const ServiceUserInfo &user,
                      const std::string &meeting_id,
                      std::vector<ServiceUserInfo> *participants) override;
  Result EndMeeting(const ServiceUserInfo &user,
                    const std::string &meeting_id,
                    std::vector<ServiceUserInfo> *participants) override;
  Result ListMeetings(bool active_only,
                      std::vector<MeetingInfo> *meetings) override;

  //专用方法
  void SimulateUserJoined(const std::string &user_id);
  void SimulateUserLeft(const std::string &user_id);
  void SimulateMeetingEnded();

private:
  MeetingMediaRoute BuildDefaultMediaRoute(const std::string &meeting_id) const;

  bool m_initialized = false;
  int m_next_meeting_index = 1;
  std::unordered_map<std::string, std::string> m_password_by_username;
  std::unordered_map<std::string, ServiceUserInfo> m_users_by_username;
  std::unordered_map<std::string, MeetingInfo> m_meetings_by_id;
  std::weak_ptr<IServiceDataCallback> m_callback;
  mutable std::mutex m_mutex;
};

} // namespace VCE
#endif //MOCKSERVICEMANAGER_H
