#include "capture/include/CaptureDefine.h"
#include "capture/src/property/CameraSourceProperty.h"
#include "capture/src/property/MicSourceProperty.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
  int camera_update_count = 0;
  CAPTURE::CameraSourceProperty camera_property(
      "camera-initial",
      [](CAPTURE::ICameraSourceProperty::DeviceEnumCallback callback) {
        callback("camera-a", "Camera A");
        callback("camera-b", "Camera B");
        return CAPTURE::CaptureResult::kSuccess;
      },
      [&camera_update_count](const std::string &device_id) {
        ++camera_update_count;
        return device_id == "camera-failing"
                   ? CAPTURE::CaptureResult::kInternalError
                   : CAPTURE::CaptureResult::kSuccess;
      });

  assert(std::string(camera_property.GetCurrentDeviceId()) == "camera-initial");

  std::vector<std::string> camera_ids;
  camera_property.EnumCameraDevices(
      [&camera_ids](const char *device_id, const char *device_name) {
        assert(device_id != nullptr);
        assert(device_name != nullptr);
        camera_ids.emplace_back(device_id);
        return true;
      });
  assert(camera_ids.size() == 2);

  auto result = camera_property.SetVideoDevice("camera-staged", false);
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(camera_update_count == 0);
  assert(std::string(camera_property.GetCurrentDeviceId()) == "camera-staged");

  result = camera_property.SetVideoDevice("camera-b", true);
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(camera_update_count == 1);
  assert(std::string(camera_property.GetCurrentDeviceId()) == "camera-b");

  result = camera_property.SetVideoDevice("camera-failing", true);
  assert(result == CAPTURE::CaptureResult::kInternalError);
  assert(camera_update_count == 2);
  assert(std::string(camera_property.GetCurrentDeviceId()) == "camera-b");

  int mic_update_count = 0;
  CAPTURE::MicSourceProperty mic_property(
      "mic-initial",
      [](CAPTURE::IMicSourceProperty::DeviceEnumCallback callback) {
        callback("mic-a", "Microphone A");
        callback("mic-b", "Microphone B");
        return CAPTURE::CaptureResult::kSuccess;
      },
      [&mic_update_count](const std::string &device_id) {
        ++mic_update_count;
        return device_id == "mic-failing"
                   ? CAPTURE::CaptureResult::kInternalError
                   : CAPTURE::CaptureResult::kSuccess;
      });

  std::vector<std::string> mic_ids;
  mic_property.EnumMicDevices(
      [&mic_ids](const char *device_id, const char *device_name) {
        assert(device_id != nullptr);
        assert(device_name != nullptr);
        mic_ids.emplace_back(device_id);
        return true;
      });
  assert(mic_ids.size() == 2);

  result = mic_property.SetMicDevice("mic-b");
  assert(result == CAPTURE::CaptureResult::kSuccess);
  assert(mic_update_count == 1);
  assert(std::string(mic_property.GetCurrentMicDeviceId()) == "mic-b");
  assert(std::string(mic_property.GetCurrentMicDeviceName()) == "Microphone B");

  result = mic_property.SetMicDevice("mic-failing");
  assert(result == CAPTURE::CaptureResult::kInternalError);
  assert(mic_update_count == 2);
  assert(std::string(mic_property.GetCurrentMicDeviceId()) == "mic-b");

  return 0;
}
