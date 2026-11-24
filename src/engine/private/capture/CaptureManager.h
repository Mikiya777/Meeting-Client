#ifndef CAPTURE_MANAGER_H
#define CAPTURE_MANAGER_H

#include "capture/include/ICaptureEngine.h"
#include "engine/private/capture/ICaptureDataCallback.h"
#include "engine/private/capture/ICaptureManager.h"
#include "engine/public/VceTypes.h"

#include <memory>
#include <mutex>
#include <string>

namespace VCE {

class CaptureManager final : public ICaptureManager {
public:
  CaptureManager() = default;
  ~CaptureManager() override = default;

  Result Init() override;
  Result Uninit() override;

  Result OpenMicrophone() override;
  Result CloseMicrophone() override;

  Result OpenCamera() override;
  Result CloseCamera() override;

  Result GetMicrophoneDevices(
      std::vector<MediaDeviceInfo> *devices) override;
  Result GetCameraDevices(
      std::vector<MediaDeviceInfo> *devices) override;
  Result SetMicrophoneDevice(const std::string &device_id) override;
  Result SetCameraDevice(const std::string &device_id) override;

  bool IsMicrophoneOpen() const override;
  bool IsCameraOpen() const override;

  void SetCaptureDataCallback(
      const std::shared_ptr<ICaptureDataCallback> &callback) override;

private:
  Result EnsureCaptureEngineLocked();
  void RegisterCallbacksLocked();
  std::shared_ptr<ICaptureDataCallback> GetCaptureDataCallback() const;
  std::shared_ptr<CAPTURE::ISource> CreateFirstAvailableSourceLocked(
      CAPTURE::CaptureSourceType type,
      const std::string &source_name);

  bool m_initialized = false;
  bool m_microphone_open = false;
  bool m_camera_open = false;

  mutable std::mutex m_mutex;
  // OBS shutdown can wait for frame callbacks while holding m_mutex.
  mutable std::mutex m_callback_mutex;
  std::weak_ptr<ICaptureDataCallback> m_callback;
  std::unique_ptr<CAPTURE::ICaptureEngine> m_capture_engine;
  std::string m_preferred_microphone_device_id;
  std::string m_preferred_camera_device_id;
};
} // namespace VCE

#endif // CAPTURE_MANAGER_H
