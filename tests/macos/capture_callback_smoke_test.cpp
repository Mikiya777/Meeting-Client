#include "capture/include/CaptureDefine.h"
#include "capture/include/CaptureFrame.h"
#include "capture/include/ICaptureEngine.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

int main() {
  // TODO(test): 该测试依赖 macOS 本机摄像头/麦克风权限，后续需要拆成手动集成测试或 mock OBS 输入。
  std::unique_ptr<CAPTURE::ICaptureEngine> engine = CAPTURE::ICaptureEngine::Create();
  assert(engine != nullptr);

  std::atomic<int> video_count = 0;
  std::atomic<int> audio_count = 0;

  engine->RegisterVideoCallback([&](const std::shared_ptr<CAPTURE::I420Frame> &frame) {
    if (frame != nullptr && frame->Validate() == CAPTURE::CaptureResult::kSuccess) {
      ++video_count;
    }
  });

  engine->RegisterAudioCallback([&](const std::shared_ptr<CAPTURE::AudioFrame> &frame) {
    if (frame != nullptr && frame->Validate() == CAPTURE::CaptureResult::kSuccess) {
      ++audio_count;
    }
  });

  auto result = engine->Init(48000, 2, 1280, 720, 30);
  assert(result == CAPTURE::CaptureResult::kSuccess);

  std::string camera_device_id;
  result = engine->EnumCameraDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    camera_device_id = device_id;
    return false;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  std::string mic_device_id;
  result = engine->EnumMicDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    mic_device_id = device_id;
    return false;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  auto camera = camera_device_id.empty()
                    ? engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "camera_0")
                    : engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera,
                                           "camera_0",
                                           camera_device_id);
  assert(camera != nullptr);

  auto mic = mic_device_id.empty()
                 ? engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone, "mic_0")
                 : engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone,
                                        "mic_0",
                                        mic_device_id);
  assert(mic != nullptr);

  std::this_thread::sleep_for(std::chrono::seconds(3));

  std::cout << "video_count=" << video_count.load()
            << ", audio_count=" << audio_count.load()
            << std::endl;

  result = engine->UnInit();
  assert(result == CAPTURE::CaptureResult::kSuccess);

  return 0;
}
