#include "engine/private/capture/CaptureManager.h"

#include "capture/include/CaptureDefine.h"
#include "capture/include/ICameraSource.h"
#include "capture/include/IMicSource.h"

#include <string>

namespace VCE {
namespace {

constexpr int kDefaultVideoWidth = 640;
constexpr int kDefaultVideoHeight = 480;
constexpr int kDefaultVideoFps = 24;
constexpr int kDefaultAudioSampleRate = 48000;
constexpr int kDefaultAudioChannels = 2;
constexpr char kDefaultCameraSourceName[] = "default_camera";
constexpr char kDefaultMicrophoneSourceName[] = "default_microphone";

Result ToVceResult(CAPTURE::CaptureResult result) {
  switch (result) {
    case CAPTURE::CaptureResult::kSuccess:
      return Result::kRet_Success;
    case CAPTURE::CaptureResult::kAlreadyInitialized:
      return Result::kRet_AlreadyInitialized;
    case CAPTURE::CaptureResult::kNotInitialized:
      return Result::kRet_NotInitialized;
    case CAPTURE::CaptureResult::kInvalidArgument:
    case CAPTURE::CaptureResult::kInvalidData:
    case CAPTURE::CaptureResult::kInvalidSize:
    case CAPTURE::CaptureResult::kInvalidFormat:
      return Result::kRet_InvalidArgument;
    default:
      return Result::kRet_InternalError;
  }
}

std::shared_ptr<I420Frame> ToVceVideoFrame(
    const std::shared_ptr<CAPTURE::I420Frame> &frame) {
  if (!frame || frame->Validate() != CAPTURE::CaptureResult::kSuccess) {
    return nullptr;
  }

  auto output = std::make_shared<I420Frame>();
  output->data[0] = frame->data[0];
  output->data[1] = frame->data[1];
  output->data[2] = frame->data[2];
  output->width = frame->width;
  output->height = frame->height;
  output->timestamp_us = frame->timestamp_us;
  return output;
}

std::shared_ptr<AudioFrame> ToVceAudioFrame(
    const std::shared_ptr<CAPTURE::AudioFrame> &frame) {
  if (!frame || frame->Validate() != CAPTURE::CaptureResult::kSuccess) {
    return nullptr;
  }

  auto output = std::make_shared<AudioFrame>();
  output->data = frame->data;
  output->sample_rate = frame->sample_rate;
  output->channels = frame->channels;
  output->samples = frame->samples;
  output->timestamp_us = frame->timestamp_us;
  return output;
}

} // namespace

Result CaptureManager::Init() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_initialized) {
    return Result::kRet_AlreadyInitialized;
  }
  m_initialized = true;

  //初始化真实的一些功能件
  return Result::kRet_Success;
}

Result CaptureManager::Uninit() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_Success;
  }

  if (m_capture_engine) {
    m_capture_engine->UnInit();
    m_capture_engine.reset();
  }

  m_camera_open = false;
  m_microphone_open = false;
  m_preferred_camera_device_id.clear();
  m_preferred_microphone_device_id.clear();
  SetCaptureDataCallback(nullptr);
  m_initialized = false;
  return Result::kRet_Success;
}

Result CaptureManager::OpenMicrophone() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  if (!m_capture_engine->GetSource(kDefaultMicrophoneSourceName) &&
      !CreateFirstAvailableSourceLocked(CAPTURE::CaptureSourceType::kCST_Microphone,
                                        kDefaultMicrophoneSourceName)) {
    return Result::kRet_InternalError;
  }

  m_microphone_open = true;
  return Result::kRet_Success;
}

Result CaptureManager::CloseMicrophone() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (m_capture_engine && m_capture_engine->GetSource(kDefaultMicrophoneSourceName)) {
    const auto remove_result =
        m_capture_engine->RemoveSource(kDefaultMicrophoneSourceName);
    if (remove_result != CAPTURE::CaptureResult::kSuccess &&
        remove_result != CAPTURE::CaptureResult::kNotFound) {
      return ToVceResult(remove_result);
    }
  }

  m_microphone_open = false;
  return Result::kRet_Success;
}

Result CaptureManager::OpenCamera() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  if (!m_capture_engine->GetSource(kDefaultCameraSourceName) &&
      !CreateFirstAvailableSourceLocked(CAPTURE::CaptureSourceType::kCST_Camera,
                                        kDefaultCameraSourceName)) {
    return Result::kRet_InternalError;
  }

  m_camera_open = true;
  return Result::kRet_Success;
}
Result CaptureManager::CloseCamera() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (m_capture_engine && m_capture_engine->GetSource(kDefaultCameraSourceName)) {
    const auto remove_result =
        m_capture_engine->RemoveSource(kDefaultCameraSourceName);
    if (remove_result != CAPTURE::CaptureResult::kSuccess &&
        remove_result != CAPTURE::CaptureResult::kNotFound) {
      return ToVceResult(remove_result);
    }
  }

  m_camera_open = false;
  return Result::kRet_Success;
}

Result CaptureManager::GetMicrophoneDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }

  devices->clear();
  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  std::string selected_id = m_preferred_microphone_device_id;
  auto source = std::dynamic_pointer_cast<CAPTURE::IMicSource>(
      m_capture_engine->GetSource(kDefaultMicrophoneSourceName));
  if (source) {
    auto property = source->GetProperty();
    if (property && property->GetCurrentMicDeviceId()) {
      selected_id = property->GetCurrentMicDeviceId();
    }
  }

  const auto result = m_capture_engine->EnumMicDevices(
      [devices](const char *device_id, const char *device_name) {
        if (device_id && device_id[0] != '\0') {
          devices->push_back({device_id,
                              device_name ? device_name : device_id,
                              false});
        }
        return true;
      });
  if (result != CAPTURE::CaptureResult::kSuccess) {
    return ToVceResult(result);
  }

  if (selected_id.empty() && !devices->empty()) {
    selected_id = devices->front().device_id;
    m_preferred_microphone_device_id = selected_id;
  }
  for (auto &device : *devices) {
    device.selected = device.device_id == selected_id;
  }
  return Result::kRet_Success;
}

Result CaptureManager::GetCameraDevices(
    std::vector<MediaDeviceInfo> *devices) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (!devices) {
    return Result::kRet_InvalidArgument;
  }

  devices->clear();
  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  std::string selected_id = m_preferred_camera_device_id;
  auto source = std::dynamic_pointer_cast<CAPTURE::ICameraSource>(
      m_capture_engine->GetSource(kDefaultCameraSourceName));
  if (source) {
    auto property = source->GetProperty();
    if (property && property->GetCurrentDeviceId()) {
      selected_id = property->GetCurrentDeviceId();
    }
  }

  const auto result = m_capture_engine->EnumCameraDevices(
      [devices](const char *device_id, const char *device_name) {
        if (device_id && device_id[0] != '\0') {
          devices->push_back({device_id,
                              device_name ? device_name : device_id,
                              false});
        }
        return true;
      });
  if (result != CAPTURE::CaptureResult::kSuccess) {
    return ToVceResult(result);
  }

  if (selected_id.empty() && !devices->empty()) {
    selected_id = devices->front().device_id;
    m_preferred_camera_device_id = selected_id;
  }
  for (auto &device : *devices) {
    device.selected = device.device_id == selected_id;
  }
  return Result::kRet_Success;
}

Result CaptureManager::SetMicrophoneDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (device_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  bool found = false;
  const auto enum_result = m_capture_engine->EnumMicDevices(
      [&found, &device_id](const char *current_id, const char *) {
        found = current_id && device_id == current_id;
        return !found;
      });
  if (enum_result != CAPTURE::CaptureResult::kSuccess) {
    return ToVceResult(enum_result);
  }
  if (!found) {
    return Result::kRet_InvalidArgument;
  }

  auto source = std::dynamic_pointer_cast<CAPTURE::IMicSource>(
      m_capture_engine->GetSource(kDefaultMicrophoneSourceName));
  if (source) {
    auto property = source->GetProperty();
    if (!property) {
      return Result::kRet_InternalError;
    }
    const auto result = property->SetMicDevice(device_id.c_str());
    if (result != CAPTURE::CaptureResult::kSuccess) {
      return ToVceResult(result);
    }
  }

  m_preferred_microphone_device_id = device_id;
  return Result::kRet_Success;
}

Result CaptureManager::SetCameraDevice(const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }
  if (device_id.empty()) {
    return Result::kRet_InvalidArgument;
  }

  const auto engine_result = EnsureCaptureEngineLocked();
  if (engine_result != Result::kRet_Success &&
      engine_result != Result::kRet_AlreadyInitialized) {
    return engine_result;
  }

  bool found = false;
  const auto enum_result = m_capture_engine->EnumCameraDevices(
      [&found, &device_id](const char *current_id, const char *) {
        found = current_id && device_id == current_id;
        return !found;
      });
  if (enum_result != CAPTURE::CaptureResult::kSuccess) {
    return ToVceResult(enum_result);
  }
  if (!found) {
    return Result::kRet_InvalidArgument;
  }

  auto source = std::dynamic_pointer_cast<CAPTURE::ICameraSource>(
      m_capture_engine->GetSource(kDefaultCameraSourceName));
  if (source) {
    auto property = source->GetProperty();
    if (!property) {
      return Result::kRet_InternalError;
    }
    const auto result = property->SetVideoDevice(device_id.c_str(), true);
    if (result != CAPTURE::CaptureResult::kSuccess) {
      return ToVceResult(result);
    }
  }

  m_preferred_camera_device_id = device_id;
  return Result::kRet_Success;
}

bool CaptureManager::IsMicrophoneOpen() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_microphone_open;
}
bool CaptureManager::IsCameraOpen() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_camera_open;
}

void CaptureManager::SetCaptureDataCallback(const std::shared_ptr<ICaptureDataCallback> &callback) {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  m_callback = callback;
}

std::shared_ptr<ICaptureDataCallback> CaptureManager::GetCaptureDataCallback() const {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  return m_callback.lock();
}

Result CaptureManager::EnsureCaptureEngineLocked() {
  if (!m_initialized) {
    return Result::kRet_NotInitialized;
  }

  if (!m_capture_engine) {
    m_capture_engine = CAPTURE::ICaptureEngine::Create();
    if (!m_capture_engine) {
      return Result::kRet_InternalError;
    }
  }

  if (m_capture_engine->IsInitialized()) {
    return Result::kRet_AlreadyInitialized;
  }

  RegisterCallbacksLocked();
  const auto result =
      m_capture_engine->Init(kDefaultAudioSampleRate,
                             kDefaultAudioChannels,
                             kDefaultVideoWidth,
                             kDefaultVideoHeight,
                             kDefaultVideoFps);
  return ToVceResult(result);
}

void CaptureManager::RegisterCallbacksLocked() {
  if (!m_capture_engine) {
    return;
  }

  m_capture_engine->RegisterVideoCallback(
      [this](const std::shared_ptr<CAPTURE::I420Frame> &frame) {
        auto callback = GetCaptureDataCallback();
        if (!callback) {
          return;
        }
        auto output = ToVceVideoFrame(frame);
        if (output) {
          callback->OnCaptureVideoFrame(output);
        }
      });

  m_capture_engine->RegisterAudioCallback(
      [this](const std::shared_ptr<CAPTURE::AudioFrame> &frame) {
        auto callback = GetCaptureDataCallback();
        if (!callback) {
          return;
        }
        auto output = ToVceAudioFrame(frame);
        if (output) {
          callback->OnCaptureAudioFrame(output);
        }
      });
}

std::shared_ptr<CAPTURE::ISource> CaptureManager::CreateFirstAvailableSourceLocked(
    CAPTURE::CaptureSourceType type,
    const std::string &source_name) {
  if (!m_capture_engine || source_name.empty()) {
    return nullptr;
  }

  std::string device_id =
      type == CAPTURE::CaptureSourceType::kCST_Camera
          ? m_preferred_camera_device_id
          : m_preferred_microphone_device_id;
  if (!device_id.empty()) {
    auto source = m_capture_engine->CreateSource(type, source_name, device_id);
    if (source) {
      return source;
    }
    device_id.clear();
  }
  const auto enum_callback =
      [&device_id](const char *current_device_id, const char * /*device_name*/) {
        if (current_device_id != nullptr && current_device_id[0] != '\0') {
          device_id = current_device_id;
        }
        return false;
      };

  CAPTURE::CaptureResult enum_result = CAPTURE::CaptureResult::kInternalError;
  if (type == CAPTURE::CaptureSourceType::kCST_Camera) {
    enum_result = m_capture_engine->EnumCameraDevices(enum_callback);
  } else if (type == CAPTURE::CaptureSourceType::kCST_Microphone) {
    enum_result = m_capture_engine->EnumMicDevices(enum_callback);
  }

  if (enum_result == CAPTURE::CaptureResult::kSuccess && !device_id.empty()) {
    auto source = m_capture_engine->CreateSource(type, source_name, device_id);
    if (source) {
      if (type == CAPTURE::CaptureSourceType::kCST_Camera) {
        m_preferred_camera_device_id = device_id;
      } else {
        m_preferred_microphone_device_id = device_id;
      }
    }
    return source;
  }

  return m_capture_engine->CreateSource(type, source_name);
}
} // namespace VCE
