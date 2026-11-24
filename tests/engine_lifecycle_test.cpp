#include "engine/public/VceEngine.h"

#include <iostream>
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

} // namespace

int main() {
  bool passed = true;

  auto engine = VCE::VceEngine::GetInstance();

  passed &= Check(engine != nullptr,
                  "GetInstance returns a valid engine");

  passed &= Check(
      engine->GetState() == VCE::EngineState::kES_Created,
      "initial state is Created");

  passed &= Check(
      engine->OpenCamera() == VCE::Result::kRet_NotInitialized,
      "camera cannot open before Init");

  std::vector<VCE::MediaDeviceInfo> camera_devices;
  passed &= Check(
      engine->GetCameraDevices(&camera_devices) ==
          VCE::Result::kRet_NotInitialized,
      "camera devices cannot be queried before Init");

  passed &= Check(
      engine->Init() == VCE::Result::kRet_Success,
      "Init succeeds");

  passed &= Check(
      engine->GetState() == VCE::EngineState::kES_Initialized,
      "state becomes Initialized");

  passed &= Check(
      engine->Init() == VCE::Result::kRet_AlreadyInitialized,
      "repeated Init is rejected");

  std::vector<VCE::MediaDeviceInfo> microphone_devices;
  passed &= Check(
      engine->GetMicrophoneDevices(&microphone_devices) ==
          VCE::Result::kRet_Success &&
          !microphone_devices.empty(),
      "microphone devices are exposed through VceEngine");
  passed &= Check(
      engine->SetMicrophoneDevice(microphone_devices.front().device_id) ==
          VCE::Result::kRet_Success,
      "microphone selection reaches the capture source");

  passed &= Check(
      engine->GetCameraDevices(&camera_devices) ==
          VCE::Result::kRet_Success &&
          !camera_devices.empty(),
      "camera devices are exposed through VceEngine");
  passed &= Check(
      engine->SetCameraDevice(camera_devices.front().device_id) ==
          VCE::Result::kRet_Success,
      "camera selection reaches the capture source");

  passed &= Check(
      engine->OpenMicrophone() == VCE::Result::kRet_Success,
      "microphone opens after Init");

  passed &= Check(
      engine->OpenCamera() == VCE::Result::kRet_Success,
      "camera opens after Init");

  passed &= Check(
      engine->Uninit() == VCE::Result::kRet_Success,
      "Uninit succeeds");

  passed &= Check(
      engine->GetState() == VCE::EngineState::kES_Stopped,
      "state becomes Stopped");

  passed &= Check(
      engine->Uninit() == VCE::Result::kRet_Success,
      "repeated Uninit is safe");

  VCE::VceEngine::ReleaseInstance();

  return passed ? 0 : 1;
}
