#ifndef I_CAPTURE_MANAGER_H
#define I_CAPTURE_MANAGER_H

#include "engine/private/capture/ICaptureDataCallback.h"
#include "engine/public/VceTypes.h"

#include <memory>

namespace VCE {

class ICaptureManager {
public:
  virtual ~ICaptureManager() = default;

  virtual Result Init() = 0;
  virtual Result Uninit() = 0;

  virtual Result OpenMicrophone() = 0;
  virtual Result CloseMicrophone() = 0;

  virtual Result OpenCamera() = 0;
  virtual Result CloseCamera() = 0;

  virtual Result GetMicrophoneDevices(
      std::vector<MediaDeviceInfo> *devices) = 0;
  virtual Result GetCameraDevices(
      std::vector<MediaDeviceInfo> *devices) = 0;
  virtual Result SetMicrophoneDevice(const std::string &device_id) = 0;
  virtual Result SetCameraDevice(const std::string &device_id) = 0;

  virtual bool IsMicrophoneOpen() const = 0;
  virtual bool IsCameraOpen() const = 0;

  virtual void SetCaptureDataCallback(
      const std::shared_ptr<ICaptureDataCallback> &callback) = 0;
};

} // namespace VCE

#endif // I_CAPTURE_MANAGER_H
