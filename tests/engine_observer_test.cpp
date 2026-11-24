#include "engine/public/IMeetingEventObserver.h"
#include "engine/public/VceEngine.h"

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

// 测试观察者不操作真实 UI，只记录 Engine 发来的事件。
// 这种测试替身通常称为 Fake 或 Spy，它让我们能够验证回调是否发生。
class TestMeetingObserver final : public VCE::IMeetingEventObserver {
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

  const std::vector<VCE::EngineState> &GetEngineStates() const {
    return m_engine_states;
  }

  const std::vector<bool> &GetMicrophoneStates() const {
    return m_microphone_states;
  }

  const std::vector<bool> &GetCameraStates() const {
    return m_camera_states;
  }

  const std::vector<bool> &GetTransportConnectionStates() const {
    return m_transport_connection_states;
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

  auto engine = VCE::VceEngine::GetInstance();
  auto observer = std::make_shared<TestMeetingObserver>();
  engine->AddObserver(observer);

  passed &= Check(engine->Init() == VCE::Result::kRet_Success,
                  "Init succeeds with an observer");
  passed &= Check(observer->GetEngineStates().size() == 1,
                  "observer receives one Init state event");
  passed &= Check(
      observer->GetEngineStates().back() ==
          VCE::EngineState::kES_Initialized,
      "Init event contains Initialized state");

  engine->OpenMicrophone();
  engine->CloseMicrophone();
  passed &= Check(observer->GetMicrophoneStates().size() == 2,
                  "observer receives microphone open and close events");
  passed &= Check(observer->GetMicrophoneStates()[0] &&
                      !observer->GetMicrophoneStates()[1],
                  "microphone events preserve their enabled values");

  engine->OpenCamera();
  engine->CloseCamera();
  passed &= Check(observer->GetCameraStates().size() == 2,
                  "observer receives camera open and close events");
  passed &= Check(observer->GetCameraStates()[0] &&
                      !observer->GetCameraStates()[1],
                  "camera events preserve their enabled values");

  engine->RemoveObserver(observer);
  const std::size_t camera_event_count =
      observer->GetCameraStates().size();
  engine->OpenCamera();
  passed &= Check(observer->GetCameraStates().size() ==
                      camera_event_count,
                  "removed observer no longer receives events");

  // Engine 内部只保存 weak_ptr。测试对象释放后，Engine 不应阻止其析构，
  // 下一次通知还应安全地清理已经失效的 weak_ptr。
  std::weak_ptr<TestMeetingObserver> weak_observer;
  {
    auto temporary_observer =
        std::make_shared<TestMeetingObserver>();
    weak_observer = temporary_observer;
    engine->AddObserver(temporary_observer);
  }

  passed &= Check(weak_observer.expired(),
                  "Engine does not own observers through shared_ptr");
  passed &= Check(engine->CloseCamera() ==
                      VCE::Result::kRet_Success,
                  "expired observer is ignored safely");

  // 重新注册主观察者，以验证 Uninit 的状态事件。
  engine->AddObserver(observer);
  passed &= Check(engine->Uninit() == VCE::Result::kRet_Success,
                  "Uninit succeeds with an observer");
  passed &= Check(
      !observer->GetEngineStates().empty() &&
          observer->GetEngineStates().back() ==
              VCE::EngineState::kES_Stopped,
      "observer receives Stopped state");

  engine->RemoveObserver(observer);
  VCE::VceEngine::ReleaseInstance();

  return passed ? 0 : 1;
}
