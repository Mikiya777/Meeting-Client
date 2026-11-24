#ifndef I_MIC_SOURCE_PROPERTY_H
#define I_MIC_SOURCE_PROPERTY_H

#include "capture/include/CaptureDefine.h"
#include "ISourceProperty.h"

#include <functional>
#include <memory>

namespace CAPTURE {
class IMicSourceProperty : public ISourceProperty {
public:
  using DeviceEnumCallback = std::function<bool(const char *device_id, const char *device_name)>;

  virtual ~IMicSourceProperty() = default;

  static std::shared_ptr<IMicSourceProperty> Create();

  virtual void EnumMicDevices(DeviceEnumCallback callback) = 0;
  virtual CaptureResult SetMicDevice(const char *device_id) = 0;

  virtual const char *GetCurrentMicDeviceId() = 0;
  virtual const char *GetCurrentMicDeviceName() = 0;
};

} // namespace CAPTURE
#endif //I_MIC_SOURCE_PROPERTY_H
