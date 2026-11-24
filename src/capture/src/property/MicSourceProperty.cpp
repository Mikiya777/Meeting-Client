#include "capture/src/property/MicSourceProperty.h"

#include <memory>
#include <utility>

namespace CAPTURE {

std::shared_ptr<IMicSourceProperty> IMicSourceProperty::Create() {
  return std::make_shared<MicSourceProperty>();
}

MicSourceProperty::MicSourceProperty(std::string initial_device_id,
                                     DeviceEnumerator device_enumerator,
                                     DeviceUpdater device_updater)
    : m_current_device_id(std::move(initial_device_id)),
      m_device_enumerator(std::move(device_enumerator)),
      m_device_updater(std::move(device_updater)) {}

void MicSourceProperty::EnumMicDevices(DeviceEnumCallback callback) {
  if (!callback) {
    return;
  }

  if (!m_device_enumerator) {
    return;
  }

  m_device_names.clear();
  (void)m_device_enumerator(
      [this, &callback](const char *device_id, const char *device_name) {
        if (device_id != nullptr && device_id[0] != '\0') {
          m_device_names[device_id] = device_name != nullptr ? device_name : "";
        }
        return callback(device_id, device_name);
      });
}

CaptureResult MicSourceProperty::SetMicDevice(const char *device_id) {
  if (device_id == nullptr || device_id[0] == '\0') {
    return CaptureResult::kInvalidArgument;
  }

  const std::string target_device_id = device_id;
  if (m_device_updater) {
    const CaptureResult result = m_device_updater(target_device_id);
    if (result != CaptureResult::kSuccess) {
      return result;
    }
  }

  m_current_device_id = target_device_id;
  auto device_name = m_device_names.find(m_current_device_id);
  if (device_name != m_device_names.end()) {
    m_current_device_name = device_name->second;
  } else {
    m_current_device_name.clear();
  }

  return CaptureResult::kSuccess;
}

const char *MicSourceProperty::GetCurrentMicDeviceId() {
  return m_current_device_id.c_str();
}
const char *MicSourceProperty::GetCurrentMicDeviceName() {
  return m_current_device_name.c_str();
}

} // namespace CAPTURE
