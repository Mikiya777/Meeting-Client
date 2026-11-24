#include "engine/private/service/MockServiceManager.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>

#include "engine/private/service/IServiceDataCallback.h"
#include "engine/public/VceTypes.h"

namespace VCE {
namespace {

constexpr char kDefaultSrsServerUrl[] = "http://62.234.24.190:2022";
constexpr char kDefaultSrsApp[] = "meeting";
constexpr char kDefaultMeetingTitle[] = "Demo Meeting";

std::string GetEnvOrDefault(const char *name, const char *default_value) {
  const char *value = std::getenv(name);
  if (value == nullptr || std::string(value).empty()) {
    return default_value;
  }
  return value;
}

bool ContainsParticipant(const MeetingInfo &meeting,
                         const std::string &user_id) {
  for (const auto &participant : meeting.participants) {
    if (participant.user_id == user_id) {
      return true;
    }
  }
  return false;
}

} // namespace

//接口方法
Result MockServiceManager::Init() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }

  m_initialized = true;
  return Result::kRet_Success;
}

Result MockServiceManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_Success;
  }

  m_initialized = false;
  m_meetings_by_id.clear();
  return Result::kRet_Success;
}

void MockServiceManager::SetServiceDataCallback(const std::shared_ptr<IServiceDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_callback = callback;
}

Result MockServiceManager::RegisterUser(const std::string &username,
                                        const std::string &password,
                                        uint32_t client_ip,
                                        ServiceUserInfo *user_info) {
  if (username.empty() || password.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  ServiceUserInfo user;
  auto user_iter = m_users_by_username.find(username);
  if (user_iter == m_users_by_username.end()) {
    user.user_id = username;
    user.username = username;
    user.client_ip = client_ip;
    m_users_by_username.emplace(username, user);
    m_password_by_username.emplace(username, password);
  } else {
    user = user_iter->second;
    user.client_ip = client_ip;
    user_iter->second.client_ip = client_ip;
    m_password_by_username[username] = password;
  }

  if (user_info) {
    *user_info = user;
  }
  return Result::kRet_Success;
}

Result MockServiceManager::Login(const std::string &username,
                                 const std::string &password,
                                 uint32_t client_ip,
                                 ServiceUserInfo *user_info) {
  if (username.empty() || password.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  auto password_iter = m_password_by_username.find(username);
  if (password_iter == m_password_by_username.end()) {
    ServiceUserInfo user;
    user.user_id = username;
    user.username = username;
    user.client_ip = client_ip;
    m_users_by_username[username] = user;
    m_password_by_username[username] = password;
  } else if (password_iter->second != password) {
    return Result::kRet_InvalidArgument;
  }

  auto &user = m_users_by_username[username];
  user.client_ip = client_ip;
  if (user_info) {
    *user_info = user;
  }
  return Result::kRet_Success;
}

Result MockServiceManager::CreateMeeting(const ServiceUserInfo &creator,
                                         const std::string &title,
                                         const std::string &description,
                                         int64_t /*start_time*/,
                                         MeetingInfo *meeting_info) {
  if (creator.user_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  std::ostringstream id_stream;
  id_stream << "demo-room-" << m_next_meeting_index++;

  MeetingInfo meeting;
  meeting.meeting_id = id_stream.str();
  meeting.title = title.empty() ? kDefaultMeetingTitle : title;
  meeting.description = description;
  meeting.creator = creator;
  meeting.participants.push_back(creator);
  meeting.active = true;
  meeting.media_route = BuildDefaultMediaRoute(meeting.meeting_id);

  m_meetings_by_id[meeting.meeting_id] = meeting;
  if (meeting_info) {
    *meeting_info = meeting;
  }
  return Result::kRet_Success;
}

Result MockServiceManager::JoinMeeting(const ServiceUserInfo &user,
                                       const std::string &meeting_id,
                                       MeetingInfo *meeting_info) {
  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  auto meeting_iter = m_meetings_by_id.find(meeting_id);
  if (meeting_iter == m_meetings_by_id.end() ||
      !meeting_iter->second.active) {
    return Result::kRet_InvalidArgument;
  }

  auto &meeting = meeting_iter->second;
  if (!ContainsParticipant(meeting, user.user_id)) {
    meeting.participants.push_back(user);
  }

  if (meeting_info) {
    *meeting_info = meeting;
  }
  return Result::kRet_Success;
}

Result MockServiceManager::LeaveMeeting(const ServiceUserInfo &user,
                                        const std::string &meeting_id,
                                        std::vector<ServiceUserInfo> *participants) {
  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  auto meeting_iter = m_meetings_by_id.find(meeting_id);
  if (meeting_iter == m_meetings_by_id.end()) {
    return Result::kRet_InvalidArgument;
  }

  auto &meeting = meeting_iter->second;
  auto &meeting_participants = meeting.participants;
  meeting_participants.erase(
      std::remove_if(meeting_participants.begin(),
                     meeting_participants.end(),
                     [&user](const ServiceUserInfo &participant) {
                       return participant.user_id == user.user_id;
                     }),
      meeting_participants.end());

  if (participants) {
    *participants = meeting_participants;
  }
  return Result::kRet_Success;
}

Result MockServiceManager::EndMeeting(const ServiceUserInfo &user,
                                      const std::string &meeting_id,
                                      std::vector<ServiceUserInfo> *participants) {
  if (user.user_id.empty() || meeting_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  std::shared_ptr<IServiceDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_initialized) {
      return Result::kRet_NotInitialized;
    }

    auto meeting_iter = m_meetings_by_id.find(meeting_id);
    if (meeting_iter == m_meetings_by_id.end()) {
      return Result::kRet_InvalidArgument;
    }

    meeting_iter->second.active = false;
    if (participants) {
      *participants = meeting_iter->second.participants;
    }
    callback = m_callback.lock();
  }

  if (callback) {
    callback->OnMeetingEnded();
  }
  return Result::kRet_Success;
}

Result MockServiceManager::ListMeetings(
    bool active_only,
    std::vector<MeetingInfo> *meetings) {
  if (!meetings) {
    return Result::kRet_InvalidArgument;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  meetings->clear();
  meetings->reserve(m_meetings_by_id.size());
  for (const auto &entry : m_meetings_by_id) {
    if (!active_only || entry.second.active) {
      meetings->push_back(entry.second);
    }
  }
  return Result::kRet_Success;
}

//专用方法
void MockServiceManager::SimulateUserJoined(const std::string &user_id) {
  std::shared_ptr<IServiceDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }

  if (callback && !user_id.empty()) {
    ServiceUserInfo user;
    user.user_id = user_id;
    user.username = user_id;
    callback->OnUserJoined(user);
  }
}

void MockServiceManager::SimulateUserLeft(const std::string &user_id) {
  std::shared_ptr<IServiceDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }

  if (callback && !user_id.empty()) {
    callback->OnUserLeft(user_id);
  }
}

void MockServiceManager::SimulateMeetingEnded() {
  std::shared_ptr<IServiceDataCallback> callback;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    callback = m_callback.lock();
  }

  if (callback) {
    callback->OnMeetingEnded();
  }
}

MeetingMediaRoute MockServiceManager::BuildDefaultMediaRoute(
    const std::string &meeting_id) const {
  MeetingMediaRoute route;
  const std::string server_url =
      GetEnvOrDefault("MEETING_SRS_URL", kDefaultSrsServerUrl);
  route.push_server_url = GetEnvOrDefault("MEETING_SRS_PUSH_URL",
                                          server_url.c_str());
  route.pull_server_url = GetEnvOrDefault("MEETING_SRS_PULL_URL",
                                          server_url.c_str());
  route.srs_app = GetEnvOrDefault("MEETING_SRS_APP", kDefaultSrsApp);
  route.srs_secret = GetEnvOrDefault("MEETING_SRS_SECRET", "");
  route.stream_prefix = meeting_id;
  return route;
}

} // namespace VCE
