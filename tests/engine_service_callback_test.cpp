#include "engine/private/VceEngineImpl.h"
#include "engine/private/service/MockServiceManager.h"
#include "engine/public/IMeetingEventObserver.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    return false;
  }

  std::cout << "[PASSED] " << message << '\n';
  return true;
}

class ServiceObserverSpy final : public VCE::IMeetingEventObserver {
public:
  void OnEngineStateChanged(VCE::EngineState state) override {
    m_engine_states.push_back(state);
  }

  void OnMicrophoneStateChanged(bool enabled) override {
    m_microphone_states.push_back(enabled);
  }

  void OnCameraStateChanged(bool enabled) override {
    m_camera_states.push_back(enabled);
  }

  void OnTransportConnectionStateChanged(bool connected) override {
    m_transport_connection_states.push_back(connected);
  }

  void OnRenderVideoFrameReady(const std::string &user_id) override {
    m_render_video_users.push_back(user_id);
  }

  void OnMeetingUserJoined(const std::string &user_id,
                           const std::string & /*username*/) override {
    m_joined_users.push_back(user_id);
  }

  void OnMeetingUserLeft(const std::string &user_id) override {
    m_left_users.push_back(user_id);
  }

  void OnMeetingEnded() override {
    ++m_meeting_ended_count;
  }

  const std::vector<std::string> &GetJoinedUsers() const {
    return m_joined_users;
  }

  const std::vector<std::string> &GetLeftUsers() const {
    return m_left_users;
  }

  int GetMeetingEndedCount() const {
    return m_meeting_ended_count;
  }

private:
  std::vector<VCE::EngineState> m_engine_states;
  std::vector<bool> m_microphone_states;
  std::vector<bool> m_camera_states;
  std::vector<bool> m_transport_connection_states;
  std::vector<std::string> m_render_video_users;
  std::vector<std::string> m_joined_users;
  std::vector<std::string> m_left_users;
  int m_meeting_ended_count = 0;
};

} // namespace

int main() {
  bool passed = true;

  auto engine_impl = std::make_shared<VCE::VceEngineImpl>();
  auto observer = std::make_shared<ServiceObserverSpy>();
  engine_impl->AddObserver(observer);

  VCE::MockServiceManager service_manager;
  passed &= Check(service_manager.Init() == VCE::Result::kRet_Success,
                  "MockServiceManager Init succeeds");

  service_manager.SetServiceDataCallback(engine_impl);

  service_manager.SimulateUserJoined("Alice");
  passed &= Check(observer->GetJoinedUsers().size() == 1,
                  "observer receives one user joined event");
  passed &= Check(observer->GetJoinedUsers().back() == "Alice",
                  "user joined event preserves user id");

  service_manager.SimulateUserLeft("Alice");
  passed &= Check(observer->GetLeftUsers().size() == 1,
                  "observer receives one user left event");
  passed &= Check(observer->GetLeftUsers().back() == "Alice",
                  "user left event preserves user id");

  service_manager.SimulateMeetingEnded();
  passed &= Check(observer->GetMeetingEndedCount() == 1,
                  "observer receives meeting ended event");

  VCE::ServiceUserInfo creator;
  creator.user_id = "creator";
  creator.username = "creator";
  VCE::MeetingInfo meeting;
  passed &= Check(service_manager.CreateMeeting(
                      creator, "Callback test meeting", "", 0, &meeting) ==
                      VCE::Result::kRet_Success,
                  "MockServiceManager creates a meeting");

  std::vector<VCE::MeetingInfo> meetings;
  passed &= Check(service_manager.ListMeetings(true, &meetings) ==
                      VCE::Result::kRet_Success,
                  "MockServiceManager lists active meetings");
  passed &= Check(!meetings.empty(),
                  "MockServiceManager list includes created meeting");

  const std::size_t joined_count = observer->GetJoinedUsers().size();
  service_manager.SimulateUserJoined("");
  passed &= Check(observer->GetJoinedUsers().size() == joined_count,
                  "empty user id is ignored");

  engine_impl->RemoveObserver(observer);
  service_manager.SimulateUserJoined("Bob");
  passed &= Check(observer->GetJoinedUsers().size() == joined_count,
                  "removed observer no longer receives service events");

  passed &= Check(service_manager.Uninit() == VCE::Result::kRet_Success,
                  "MockServiceManager Uninit succeeds");

  return passed ? 0 : 1;
}
