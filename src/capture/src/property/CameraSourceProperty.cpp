#include "capture/src/property/CameraSourceProperty.h"

#include <memory>
#include <utility>

namespace CAPTURE {

std::shared_ptr<ICameraSourceProperty> ICameraSourceProperty::Create() {
  return std::make_shared<CameraSourceProperty>();
}

CameraSourceProperty::CameraSourceProperty(std::string initial_device_id,
                                           DeviceEnumerator device_enumerator,
                                           DeviceUpdater device_updater)
    : m_current_device_id(std::move(initial_device_id)),
      m_device_enumerator(std::move(device_enumerator)),
      m_device_updater(std::move(device_updater)) {}

//摄像头
void CameraSourceProperty::EnumCameraDevices(DeviceEnumCallback callback) {
  if (!callback) {
    return;
  }

  if (m_device_enumerator) {
    (void)m_device_enumerator(std::move(callback));
  }
}
CaptureResult CameraSourceProperty::SetVideoDevice(const char *device_id, bool update_now) {
  if (device_id == nullptr || device_id[0] == '\0') {
    return CaptureResult::kInvalidArgument;
  }

  const std::string target_device_id = device_id;
  if (update_now && m_device_updater) {
    const CaptureResult result = m_device_updater(target_device_id);
    if (result != CaptureResult::kSuccess) {
      return result;
    }
  }

  m_current_device_id = target_device_id;
  return CaptureResult::kSuccess;
}
const char *CameraSourceProperty::GetCurrentDeviceId() {
  return m_current_device_id.c_str();
}

//分辨率
void CameraSourceProperty::EnumResolution(ResolutionEnumCallback callback) {
  if (!callback) {
    return;
  }

  // TODO(obs-property): 当前分辨率列表是业务默认值，后续需要从 OBS 设备能力中读取。
  callback("1280x720");
  callback("1920x1080");
}
CaptureResult CameraSourceProperty::SetResolution(const char *resolution) {
  if (resolution == nullptr || resolution[0] == '\0') {
    return CaptureResult::kInvalidArgument;
  }

  m_current_resolution = resolution;

  return CaptureResult::kSuccess;
}
const char *CameraSourceProperty::GetCurrentResolution() {
  return m_current_resolution.c_str();
}

//视频格式
void CameraSourceProperty::EnumVideoFormat(VideoFormatEnumCallback callback) {
  if (!callback) {
    return;
  }

  // TODO(obs-property): 当前视频格式列表先固定为 I420，后续需要从 OBS 设备能力中读取。
  callback("I420", 1);
}
CaptureResult CameraSourceProperty::SetVideoFormat(int32_t index) {
  if (index < 0) {
    return CaptureResult::kInvalidArgument;
  }
  m_current_video_format_index = index;
  m_current_video_format = "format_" + std::to_string(index);
  return CaptureResult::kSuccess;
}
const char *CameraSourceProperty::GetCurrentVideoFormat() {
  return m_current_video_format.c_str();
}
} // namespace CAPTURE
