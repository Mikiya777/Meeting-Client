#ifndef MIC_SOURCE_PROPERTY_H
#define MIC_SOURCE_PROPERTY_H

/*
模块位置：
include/IMicSourceProperty.h
        |
        v
src/property/MicSourceProperty.h/.cpp
        |
        v
OBS / 系统麦克风设备枚举与属性配置
功能：
枚举麦克风设备
设置当前麦克风设备
获取当前麦克风设备 id
获取当前麦克风设备名称
*/

#include "capture/include/IMicSourceProperty.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace CAPTURE {

class MicSourceProperty final : public IMicSourceProperty {
public:
  using DeviceEnumerator = std::function<CaptureResult(DeviceEnumCallback)>;
  using DeviceUpdater = std::function<CaptureResult(const std::string &)>;

  MicSourceProperty() = default;
  MicSourceProperty(std::string initial_device_id,
                    DeviceEnumerator device_enumerator,
                    DeviceUpdater device_updater);
  ~MicSourceProperty() override = default;

  void EnumMicDevices(DeviceEnumCallback callback) override;
  CaptureResult SetMicDevice(const char *device_id) override;

  const char *GetCurrentMicDeviceId() override;
  const char *GetCurrentMicDeviceName() override;

private:
  std::unordered_map<std::string, std::string> m_device_names;
  std::string m_current_device_id;
  std::string m_current_device_name;
  DeviceEnumerator m_device_enumerator;
  DeviceUpdater m_device_updater;
};

} // namespace CAPTURE

#endif // MIC_SOURCE_PROPERTY_H
