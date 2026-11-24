#ifndef MOCK_CAPTURE_MANAGER_H
#define MOCK_CAPTURE_MANAGER_H

#include "engine/private/capture/ICaptureManager.h"

#include <memory>
#include <mutex>

namespace VCE {

class MockCaptureManager final : public ICaptureManager {
public:
  MockCaptureManager() = default;
  ~MockCaptureManager() override = default;

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
  mutable std::mutex m_mutex;
  std::weak_ptr<ICaptureDataCallback> m_callback;
  bool m_initialized{false};
  bool m_microphone_open{false};
  bool m_camera_open{false};
};

} // namespace VCE

#endif // MOCK_CAPTURE_MANAGER_H
