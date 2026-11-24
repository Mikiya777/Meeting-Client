#include "capture/include/CaptureDefine.h"
#include "capture/include/ICaptureEngine.h"
#include "capture/include/ICameraSource.h"
#include "capture/include/IMicSource.h"
#include "capture/include/ISource.h"

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

int main() {
  std::unique_ptr<CAPTURE::ICaptureEngine> engine = CAPTURE::ICaptureEngine::Create();
  assert(engine != nullptr);

  auto result = engine->RegisterVideoCallback({});
  assert(result == CAPTURE::CaptureResult::kInvalidArgument);
  result = engine->RegisterAudioCallback({});
  assert(result == CAPTURE::CaptureResult::kInvalidArgument);

  result = engine->RegisterVideoCallback(
      [](const std::shared_ptr<CAPTURE::I420Frame> &) {});
  assert(result == CAPTURE::CaptureResult::kSuccess);
  result = engine->RegisterAudioCallback(
      [](const std::shared_ptr<CAPTURE::AudioFrame> &) {});
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = engine->RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kNotInitialized);

  auto source = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "camera_before_init");
  assert(source == nullptr);

  result = engine->Init(48000, 2, 1280, 720, 30);
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(engine->IsInitialized());

  source = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "");
  assert(source == nullptr);

  std::vector<std::string> camera_device_ids;
  result = engine->EnumCameraDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    camera_device_ids.emplace_back(device_id);
    return true;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  std::vector<std::string> mic_device_ids;
  result = engine->EnumMicDevices([&](const char *device_id, const char *device_name) {
    assert(device_id != nullptr);
    assert(device_name != nullptr);
    mic_device_ids.emplace_back(device_id);
    return true;
  });
  assert(result == CAPTURE::CaptureResult::kSuccess);

  auto camera = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "camera_0");
  assert(camera != nullptr);
  assert(camera->GetSourceType() == CAPTURE::CaptureSourceType::kCST_Camera);
  assert(std::strcmp(camera->GetSourceName(), "camera_0") == 0);
  assert(std::strcmp(camera->GetSourceId(), "camera_0") == 0);

  auto retained_camera_source = std::dynamic_pointer_cast<CAPTURE::ICameraSource>(camera);
  assert(retained_camera_source != nullptr);
  auto retained_camera_property = retained_camera_source->GetProperty();
  assert(retained_camera_property != nullptr);

  auto camera_again = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "camera_0");
  assert(camera_again == camera);

  auto mic = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone, "mic_0");
  assert(mic != nullptr);
  assert(mic->GetSourceType() == CAPTURE::CaptureSourceType::kCST_Microphone);
  assert(std::strcmp(mic->GetSourceName(), "mic_0") == 0);
  assert(std::strcmp(mic->GetSourceId(), "mic_0") == 0);

  std::vector<std::shared_ptr<CAPTURE::ISource>> sources = engine->GetAllSources();
  assert(sources.size() == 2);

  assert(engine->GetSource("camera_0") == camera);
  assert(engine->GetSource("mic_0") == mic);
  assert(engine->GetSource("missing") == nullptr);

  if (!camera_device_ids.empty()) {
    auto selected_camera = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera,
                                                "camera_selected_0",
                                                camera_device_ids.front());
    assert(selected_camera != nullptr);
    assert(selected_camera->GetSourceType() == CAPTURE::CaptureSourceType::kCST_Camera);

    auto camera_source = std::dynamic_pointer_cast<CAPTURE::ICameraSource>(selected_camera);
    assert(camera_source != nullptr);
    auto camera_property = camera_source->GetProperty();
    assert(camera_property != nullptr);
    assert(std::string(camera_property->GetCurrentDeviceId()) == camera_device_ids.front());
    const std::string &updated_camera_id =
        camera_device_ids.size() > 1 ? camera_device_ids[1] : camera_device_ids.front();
    result = camera_property->SetVideoDevice(updated_camera_id.c_str(), true);
    assert(result == CAPTURE::CaptureResult::kSuccess);
    assert(std::string(camera_property->GetCurrentDeviceId()) == updated_camera_id);

    result = engine->RemoveSource("camera_selected_0");
    assert(result == CAPTURE::CaptureResult::kSuccess);
  }

  if (!mic_device_ids.empty()) {
    auto selected_mic = engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Microphone,
                                             "mic_selected_0",
                                             mic_device_ids.front());
    assert(selected_mic != nullptr);
    assert(selected_mic->GetSourceType() == CAPTURE::CaptureSourceType::kCST_Microphone);

    auto mic_source = std::dynamic_pointer_cast<CAPTURE::IMicSource>(selected_mic);
    assert(mic_source != nullptr);
    auto mic_property = mic_source->GetProperty();
    assert(mic_property != nullptr);
    assert(std::string(mic_property->GetCurrentMicDeviceId()) == mic_device_ids.front());
    const std::string &updated_mic_id =
        mic_device_ids.size() > 1 ? mic_device_ids[1] : mic_device_ids.front();
    result = mic_property->SetMicDevice(updated_mic_id.c_str());
    assert(result == CAPTURE::CaptureResult::kSuccess);
    assert(std::string(mic_property->GetCurrentMicDeviceId()) == updated_mic_id);

    result = engine->RemoveSource("mic_selected_0");
    assert(result == CAPTURE::CaptureResult::kSuccess);
  }

  result = engine->RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(engine->GetSource("camera_0") == nullptr);

  result = retained_camera_property->SetVideoDevice("camera-after-remove", true);
  assert(result == CAPTURE::CaptureResult::kNotFound);
  assert(std::string(retained_camera_property->GetCurrentDeviceId()).empty());

  auto replacement_camera =
      engine->CreateSource(CAPTURE::CaptureSourceType::kCST_Camera, "camera_0");
  assert(replacement_camera != nullptr);
  result = retained_camera_property->SetVideoDevice("camera-after-recreate", true);
  assert(result == CAPTURE::CaptureResult::kNotFound);

  result = engine->RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  result = engine->RemoveSource("camera_0");
  assert(result == CAPTURE::CaptureResult::kNotFound);

  result = engine->RemoveSource("mic_0");
  assert(result == CAPTURE::CaptureResult::kSuccess);

  sources = engine->GetAllSources();
  assert(sources.empty());

  result = engine->UnInit();
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(!engine->IsInitialized());

  result = retained_camera_property->SetVideoDevice("camera-after-uninit", true);
  assert(result == CAPTURE::CaptureResult::kNotInitialized);

  engine.reset();
  result = retained_camera_property->SetVideoDevice("camera-after-destroy", true);
  assert(result == CAPTURE::CaptureResult::kNotInitialized);

  return 0;
}
