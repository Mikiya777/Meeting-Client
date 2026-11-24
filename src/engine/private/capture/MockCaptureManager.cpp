#include "engine/private/capture/MockCaptureManager.h"

namespace VCE {

Result MockCaptureManager::Init() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }

  m_microphone_open = false;
  m_camera_open = false;
  m_initialized = true;

  return Result::kRet_Success;
}

Result MockCaptureManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_Success;
  }

  m_microphone_open = false;
  m_camera_open = false;
  m_callback.reset();
  m_initialized = false;

  return Result::kRet_Success;
}

Result MockCaptureManager::OpenMicrophone() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_microphone_open = true;
  return Result::kRet_Success;
}

Result MockCaptureManager::CloseMicrophone() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_microphone_open = false;
  return Result::kRet_Success;
}

Result MockCaptureManager::OpenCamera() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_camera_open = true;
  return Result::kRet_Success;
}

Result MockCaptureManager::CloseCamera() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  m_camera_open = false;
  return Result::kRet_Success;
}

Result MockCaptureManager::GetMicrophoneDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }
  *devices = {{"mock-microphone", "Mock Microphone", true}};
  return Result::kRet_Success;
}

Result MockCaptureManager::GetCameraDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }
  *devices = {{"mock-camera", "Mock Camera", true}};
  return Result::kRet_Success;
}

Result MockCaptureManager::SetMicrophoneDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  return device_id == "mock-microphone" ? Result::kRet_Success
                                         : Result::kRet_InvalidArgument;
}

Result MockCaptureManager::SetCameraDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  return device_id == "mock-camera" ? Result::kRet_Success
                                     : Result::kRet_InvalidArgument;
}

bool MockCaptureManager::IsMicrophoneOpen() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_microphone_open;
}

bool MockCaptureManager::IsCameraOpen() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_camera_open;
}

void MockCaptureManager::SetCaptureDataCallback(
    const std::shared_ptr<ICaptureDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_callback = callback;
}

} // namespace VCE
