#ifndef ISERVICEMANAGER_H
#define ISERVICEMANAGER_H

#include "engine/private/service/IServiceDataCallback.h"
#include "engine/public/VceTypes.h"

#include <memory>
#include <string>

namespace VCE {
class IServiceManager {
public:
  virtual ~IServiceManager() = default;

  virtual Result Init() = 0;
  virtual Result Uninit() = 0;
  virtual void SetServiceDataCallback(const std::shared_ptr<IServiceDataCallback> &callback) = 0;

  virtual Result RegisterUser(const std::string &username,
                              const std::string &password,
                              uint32_t client_ip,
                              ServiceUserInfo *user_info) = 0;
  virtual Result Login(const std::string &username,
                       const std::string &password,
                       uint32_t client_ip,
                       ServiceUserInfo *user_info) = 0;

  virtual Result CreateMeeting(const ServiceUserInfo &creator,
                               const std::string &title,
                               const std::string &description,
                               int64_t start_time,
                               MeetingInfo *meeting_info) = 0;
  virtual Result JoinMeeting(const ServiceUserInfo &user,
                             const std::string &meeting_id,
                             MeetingInfo *meeting_info) = 0;
  virtual Result LeaveMeeting(const ServiceUserInfo &user,
                              const std::string &meeting_id,
                              std::vector<ServiceUserInfo> *participants) = 0;
  virtual Result EndMeeting(const ServiceUserInfo &user,
                            const std::string &meeting_id,
                            std::vector<ServiceUserInfo> *participants) = 0;
  virtual Result ListMeetings(bool active_only,
                              std::vector<MeetingInfo> *meetings) = 0;
};
} // namespace VCE
#endif //ISERVICEMANAGER_H
