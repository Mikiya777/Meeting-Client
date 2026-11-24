#include "engine/private/VceEngineImpl.h"
#include "engine/private/capture/MockCaptureManager.h"
#include "engine/private/render/MockRenderManager.h"
#include "engine/private/service/MockServiceManager.h"
#include "engine/private/transport/MockTransportManager.h"
#include "engine/public/IMeetingEventObserver.h"
#include <iostream>
#include <memory>
#include <vector>

using namespace VCE;
namespace {
bool passed = true;
void Check(bool ok, const char* message) {
  if (!ok) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
class FailingTransport : public ITransportManager {
public:
  bool fail = true;
  Result Init(const TransportPublishConfig& config) override {
    if (fail) return Result::kRet_InternalError;
    return delegate_.Init(config);
  }
  Result Uninit() override { return delegate_.Uninit(); }
  Result SetRoomInfo(const TransportRoomInfo &info) override { return delegate_.SetRoomInfo(info); }
  TransportRoomInfo GetRoomInfo() const override { return delegate_.GetRoomInfo(); }
  Result StartPublishCameraVideo() override { return delegate_.StartPublishCameraVideo(); }
  Result StopPublishCameraVideo() override { return delegate_.StopPublishCameraVideo(); }
  Result StartPublishAudio() override { return delegate_.StartPublishAudio(); }
  Result StopPublishAudio() override { return delegate_.StopPublishAudio(); }
  Result PushVideoFrame(const std::shared_ptr<I420Frame> &frame) override { return delegate_.PushVideoFrame(frame); }
  Result PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) override { return delegate_.PushAudioFrame(frame); }
  Result SubscribeUserAV(const std::string &user_id) override { return delegate_.SubscribeUserAV(user_id); }
  Result UnsubscribeUserAV(const std::string &user_id) override { return delegate_.UnsubscribeUserAV(user_id); }
  Result SubscribeUsersAV(const std::vector<std::string> &user_ids) override { return delegate_.SubscribeUsersAV(user_ids); }
  Result UnsubscribeUsersAV(const std::vector<std::string> &user_ids) override { return delegate_.UnsubscribeUsersAV(user_ids); }
  bool IsPublishingCameraVideo() const override { return delegate_.IsPublishingCameraVideo(); }
  bool IsPublishingAudio() const override { return delegate_.IsPublishingAudio(); }
  bool IsUserSubscribedAV(const std::string &user_id) const override { return delegate_.IsUserSubscribedAV(user_id); }
  std::vector<std::string> GetSubscribedUsers() const override { return delegate_.GetSubscribedUsers(); }
  void SetTransportDataCallback(const std::shared_ptr<ITransportDataCallback> &callback) override { delegate_.SetTransportDataCallback(callback); }
private:
  MockTransportManager delegate_;
};
class Observer : public IMeetingEventObserver {
public:
  std::weak_ptr<VceEngineImpl> engine;
  std::vector<EngineState> states;
  int camera_events = 0;
  int microphone_events = 0;
  void OnEngineStateChanged(EngineState state) override {
    states.push_back(state);
    auto e = engine.lock();
    if (state == EngineState::kES_Initialized) {
      Check(e->Init() == Result::kRet_AlreadyInitialized, "reentrant Init");
      Check(e->OpenCamera() == Result::kRet_Success, "open camera from observer");
      Check(e->OpenMicrophone() == Result::kRet_Success, "open mic from observer");
    } else if (state == EngineState::kES_ShuttingDown) {
      Check(e->Uninit() == Result::kRet_InternalError, "recursive shutdown rejected");
      Check(e->Init() == Result::kRet_InternalError, "Init rejected during shutdown");
      Check(e->OpenCamera() == Result::kRet_NotInitialized, "camera rejected during shutdown");
    } else if (state == EngineState::kES_Stopped) {
      Check(e->Uninit() == Result::kRet_Success, "reentrant stopped Uninit");
    }
  }
  void OnCameraStateChanged(bool enabled) override {
    ++camera_events;
    if (enabled) Check(engine.lock()->CloseCamera() == Result::kRet_Success, "close camera from observer");
    else Check(engine.lock()->SetBeautyEnabled(false) == Result::kRet_Success, "camera off callback reentry");
  }
  void OnMicrophoneStateChanged(bool enabled) override {
    ++microphone_events;
    if (enabled) Check(engine.lock()->CloseMicrophone() == Result::kRet_Success, "close mic from observer");
    else Check(engine.lock()->SetBeautyEnabled(false) == Result::kRet_Success, "mic off callback reentry");
  }
  void OnTransportConnectionStateChanged(bool) override {}
  void OnRenderVideoFrameReady(const std::string&) override {}
  void OnMeetingUserJoined(const std::string&, const std::string&) override {}
  void OnMeetingUserLeft(const std::string&) override {}
  void OnMeetingEnded() override {}
};
}
int main() {
  auto transport = std::make_unique<FailingTransport>();
  auto* transport_control = transport.get();
  auto capture = std::make_unique<MockCaptureManager>();
  auto* capture_control = capture.get();
  auto engine = std::make_shared<VceEngineImpl>(std::move(capture),
      std::make_unique<MockRenderManager>(), std::move(transport),
      std::make_unique<MockServiceManager>());
  auto observer = std::make_shared<Observer>();
  observer->engine = engine;
  engine->AddObserver(observer);
  Check(engine->Init() == Result::kRet_InternalError, "injected transport failure");
  Check(engine->GetState() == EngineState::kES_Created, "failed init preserves state");
  Check(observer->states.empty(), "no success notification after failure");
  Check(capture_control->OpenCamera() == Result::kRet_NotInitialized, "capture rolled back");
  Check(engine->Uninit() == Result::kRet_Success, "Uninit after failed init");
  transport_control->fail = false;
  for (int i = 0; i < 3; ++i) {
    Check(engine->Init() == Result::kRet_Success, "initialize/reinitialize");
    Check(engine->Uninit() == Result::kRet_Success, "shutdown");
    Check(engine->Uninit() == Result::kRet_Success, "idempotent shutdown");
  }
  const std::vector<EngineState> expected = {
      EngineState::kES_Initialized, EngineState::kES_ShuttingDown, EngineState::kES_Stopped,
      EngineState::kES_Initialized, EngineState::kES_ShuttingDown, EngineState::kES_Stopped,
      EngineState::kES_Initialized, EngineState::kES_ShuttingDown, EngineState::kES_Stopped};
  Check(observer->states == expected, "lifecycle event order");
  Check(observer->camera_events == 6 && observer->microphone_events == 6, "device event counts");
  engine->RemoveObserver(observer);
  std::cout << (passed ? "PASS\n" : "FAIL\n");
  return passed ? 0 : 1;
}
