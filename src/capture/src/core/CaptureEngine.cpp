#include "capture/src/core/CaptureEngine.h"
#include "capture/src/source/CameraSource.h"
#include "capture/src/source/MicSource.h"
#ifdef _WIN32
#include "capture/src/windows/QtCaptureRuntime.h"
#else
#include "capture/src/macos/ObsRuntime.h"
#endif
#include "capture/src/property/CameraSourceProperty.h"
#include "capture/src/property/MicSourceProperty.h"

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace CAPTURE {

std::unique_ptr<ICaptureEngine> ICaptureEngine::Create() {
  return std::make_unique<CaptureEngine>();
}

CaptureEngine::CaptureEngine()
    : m_source_property_binding(std::make_shared<SourcePropertyBinding>()) {
  m_source_property_binding->engine = this;
}

CaptureEngine::~CaptureEngine() {
  if (m_source_property_binding) {
    std::lock_guard<std::mutex> lock(m_source_property_binding->mutex);
    m_source_property_binding->engine = nullptr;
  }
  UnInit();
}

// 初始化当前平台的采集后端。
CaptureResult CaptureEngine::Init(int sample_rate, int channels, int width, int height, int fps) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_initialized) {
    return CaptureResult::kAlreadyInitialized;
  }

  //参数检验
  if (sample_rate <= 0 || channels <= 0 || width <= 0 || height <= 0 || fps <= 0) {
    return CaptureResult::kInvalidArgument;
  }

  m_runtime = std::make_unique<CaptureRuntime>();

  const CaptureResult runtime_result = m_runtime->Init(sample_rate, channels, width, height, fps);
  if (runtime_result != CaptureResult::kSuccess) {
    m_runtime.reset();
    return runtime_result;
  }

  if (m_video_callback) {
    const CaptureResult callback_result =
        m_runtime->RegisterVideoCallback(m_video_callback);
    if (callback_result != CaptureResult::kSuccess) {
      m_runtime->UnInit();
      m_runtime.reset();
      return callback_result;
    }
  }

  if (m_audio_callback) {
    const CaptureResult callback_result =
        m_runtime->RegisterAudioCallback(m_audio_callback);
    if (callback_result != CaptureResult::kSuccess) {
      m_runtime->UnInit();
      m_runtime.reset();
      return callback_result;
    }
  }

  m_sample_rate = sample_rate;
  m_channels = channels;
  m_width = width;
  m_height = height;
  m_fps = fps;
  m_initialized = true;
  return CaptureResult::kSuccess;
}

CaptureResult CaptureEngine::UnInit() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized) {
    return CaptureResult::kSuccess;
  }

  //清理两个容器里的内容 clear是哈希表的清理API
  m_sources.clear();
  m_source_property_tokens.clear();

  if (m_runtime != nullptr) {
    // 停止采集并释放设备。
    m_runtime->UnInit();
    m_runtime.reset();
  }

  m_initialized = false;

  return CaptureResult::kSuccess;
}

bool CaptureEngine::IsInitialized() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_initialized;
}

std::shared_ptr<ISource> CaptureEngine::CreateSource(CaptureSourceType type, const std::string &name) {
  std::lock_guard<std::mutex> lock(m_mutex);
  return CreateSourceLocked(type, name, nullptr);
}

std::shared_ptr<ISource> CaptureEngine::CreateSource(CaptureSourceType type,
                                                     const std::string &name,
                                                     const std::string &device_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  return CreateSourceLocked(type, name, &device_id);
}

std::shared_ptr<ISource> CaptureEngine::CreateSourceLocked(
    CaptureSourceType type,
    const std::string &name,
    const std::string *device_id) {
  if (!m_initialized || m_runtime == nullptr || name.empty() ||
      (device_id != nullptr && device_id->empty())) {
    return nullptr;
  }

  auto existing = m_sources.find(name);
  if (existing != m_sources.end()) {
    return existing->second;
  }

  CaptureResult runtime_result = CaptureResult::kInvalidArgument;
  if (type == CaptureSourceType::kCST_Camera) {
    runtime_result = device_id == nullptr
                     ? m_runtime->AddCameraSource(name)
                     : m_runtime->AddCameraSource(name, *device_id);
  } else if (type == CaptureSourceType::kCST_Microphone) {
    runtime_result = device_id == nullptr
                     ? m_runtime->AddMicSource(name)
                     : m_runtime->AddMicSource(name, *device_id);
  } else {
    return nullptr;
  }

  if (runtime_result != CaptureResult::kSuccess) {
    return nullptr;
  }

  bool token_inserted = false;
  bool source_inserted = false;
  try {
    const std::string initial_device_id =
        device_id != nullptr ? *device_id : std::string{};
    const auto source_token = std::make_shared<SourcePropertyToken>();
    std::shared_ptr<ISource> source;

    if (type == CaptureSourceType::kCST_Camera) {
      source = std::make_shared<CameraSource>(
          name, name,
          CreateCameraSourceProperty(name, initial_device_id, source_token));
    } else {
      source = std::make_shared<MicSource>(
          name, name,
          CreateMicSourceProperty(name, initial_device_id, source_token));
    }

    const auto token_result = m_source_property_tokens.emplace(name, source_token);
    if (!token_result.second) {
      RollbackSourceCreation(name, false, false);
      return nullptr;
    }
    token_inserted = true;

    const auto source_result = m_sources.emplace(name, source);
    if (!source_result.second) {
      RollbackSourceCreation(name, false, token_inserted);
      return nullptr;
    }
    source_inserted = true;

    return source;
  } catch (...) {
    RollbackSourceCreation(name, source_inserted, token_inserted);
    return nullptr;
  }
}

void CaptureEngine::RollbackSourceCreation(const std::string &name,
                                           bool source_inserted,
                                           bool token_inserted) noexcept {
  if (m_runtime != nullptr) {
    (void)m_runtime->RemoveSource(name);
  }
  if (token_inserted) {
    m_source_property_tokens.erase(name);
  }
  if (source_inserted) {
    m_sources.erase(name);
  }
}

CaptureResult CaptureEngine::RemoveSource(const std::string &name) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (name.empty()) {
    return CaptureResult::kInvalidArgument;
  }

  auto it = m_sources.find(name);
  if (it == m_sources.end()) {
    return CaptureResult::kNotFound;
  }

  if (m_runtime != nullptr) {
    const CaptureResult runtime_result = m_runtime->RemoveSource(name);
    if (runtime_result != CaptureResult::kSuccess) {
      return runtime_result;
    }
  }

  m_source_property_tokens.erase(name);
  m_sources.erase(it);
  return CaptureResult::kSuccess;
}

std::shared_ptr<ISource> CaptureEngine::GetSource(const std::string &name) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized || name.empty()) {
    return nullptr;
  }

  auto it = m_sources.find(name);
  if (it == m_sources.end()) {
    return nullptr;
  }

  return it->second;
}

std::vector<std::shared_ptr<ISource>> CaptureEngine::GetAllSources() {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<std::shared_ptr<ISource>> sources;
  sources.reserve(m_sources.size());

  for (const auto &item : m_sources) {
    sources.push_back(item.second);
  }

  return sources;
}

CaptureResult CaptureEngine::EnumCameraDevices(CaptureDeviceEnumCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_runtime == nullptr) {
    return CaptureResult::kInternalError;
  }

  return m_runtime->EnumCameraDevices(std::move(callback));
}

CaptureResult CaptureEngine::EnumMicDevices(CaptureDeviceEnumCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_initialized) {
    return CaptureResult::kNotInitialized;
  }

  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_runtime == nullptr) {
    return CaptureResult::kInternalError;
  }

  return m_runtime->EnumMicDevices(std::move(callback));
}

//注册回调
CaptureResult CaptureEngine::RegisterVideoCallback(VideoDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_initialized && m_runtime != nullptr) {
    const CaptureResult result = m_runtime->RegisterVideoCallback(callback);
    if (result != CaptureResult::kSuccess) {
      return result;
    }
  }

  m_video_callback = std::move(callback);
  return CaptureResult::kSuccess;
}

CaptureResult CaptureEngine::RegisterAudioCallback(AudioDataCallback callback) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!callback) {
    return CaptureResult::kInvalidArgument;
  }

  if (m_initialized && m_runtime != nullptr) {
    const CaptureResult result = m_runtime->RegisterAudioCallback(callback);
    if (result != CaptureResult::kSuccess) {
      return result;
    }
  }

  m_audio_callback = std::move(callback);
  return CaptureResult::kSuccess;
}

//创建源属性：私有对象装配函数
std::shared_ptr<ICameraSourceProperty> CaptureEngine::CreateCameraSourceProperty(
    const std::string &source_name,
    const std::string &initial_device_id,
    const std::shared_ptr<SourcePropertyToken> &source_token) {
  const std::weak_ptr<SourcePropertyBinding> weak_binding = m_source_property_binding;
  const std::weak_ptr<SourcePropertyToken> weak_source_token = source_token;

  CameraSourceProperty::DeviceEnumerator enumerator =
      [weak_binding, weak_source_token](ICameraSourceProperty::DeviceEnumCallback callback) {
        if (weak_source_token.expired()) {
          return CaptureResult::kNotFound;
        }
        const auto binding = weak_binding.lock();
        if (!binding) {
          return CaptureResult::kNotInitialized;
        }

        std::vector<std::pair<std::string, std::string>> devices;
        CaptureResult result = CaptureResult::kNotInitialized;
        {
          std::lock_guard<std::mutex> lock(binding->mutex);
          if (binding->engine == nullptr) {
            return CaptureResult::kNotInitialized;
          }
          result = binding->engine->EnumCameraDevices(
              [&devices](const char *device_id, const char *device_name) {
                devices.emplace_back(device_id != nullptr ? device_id : "",
                                     device_name != nullptr ? device_name : "");
                return true;
              });
        }

        if (result != CaptureResult::kSuccess) {
          return result;
        }
        for (const auto &device : devices) {
          if (!callback(device.first.c_str(), device.second.c_str())) {
            break;
          }
        }
        return CaptureResult::kSuccess;
      };

  CameraSourceProperty::DeviceUpdater updater =
      [weak_binding, weak_source_token, source_name](const std::string &device_id) {
        const auto source_token = weak_source_token.lock();
        if (!source_token) {
          return CaptureResult::kNotFound;
        }
        const auto binding = weak_binding.lock();
        if (!binding) {
          return CaptureResult::kNotInitialized;
        }
        std::lock_guard<std::mutex> lock(binding->mutex);
        if (binding->engine == nullptr) {
          return CaptureResult::kNotInitialized;
        }
        return binding->engine->UpdateCameraSourceDevice(
            source_name, device_id, source_token);
      };

  return std::make_shared<CameraSourceProperty>(
      initial_device_id, std::move(enumerator), std::move(updater));
}

std::shared_ptr<IMicSourceProperty> CaptureEngine::CreateMicSourceProperty(
    const std::string &source_name,
    const std::string &initial_device_id,
    const std::shared_ptr<SourcePropertyToken> &source_token) {
  const std::weak_ptr<SourcePropertyBinding> weak_binding = m_source_property_binding;
  const std::weak_ptr<SourcePropertyToken> weak_source_token = source_token;

  MicSourceProperty::DeviceEnumerator enumerator =
      [weak_binding, weak_source_token](IMicSourceProperty::DeviceEnumCallback callback) {
        if (weak_source_token.expired()) {
          return CaptureResult::kNotFound;
        }
        const auto binding = weak_binding.lock();
        if (!binding) {
          return CaptureResult::kNotInitialized;
        }

        std::vector<std::pair<std::string, std::string>> devices;
        CaptureResult result = CaptureResult::kNotInitialized;
        {
          std::lock_guard<std::mutex> lock(binding->mutex);
          if (binding->engine == nullptr) {
            return CaptureResult::kNotInitialized;
          }
          result = binding->engine->EnumMicDevices(
              [&devices](const char *device_id, const char *device_name) {
                devices.emplace_back(device_id != nullptr ? device_id : "",
                                     device_name != nullptr ? device_name : "");
                return true;
              });
        }

        if (result != CaptureResult::kSuccess) {
          return result;
        }
        for (const auto &device : devices) {
          if (!callback(device.first.c_str(), device.second.c_str())) {
            break;
          }
        }
        return CaptureResult::kSuccess;
      };

  MicSourceProperty::DeviceUpdater updater =
      [weak_binding, weak_source_token, source_name](const std::string &device_id) {
        const auto source_token = weak_source_token.lock();
        if (!source_token) {
          return CaptureResult::kNotFound;
        }
        const auto binding = weak_binding.lock();
        if (!binding) {
          return CaptureResult::kNotInitialized;
        }
        std::lock_guard<std::mutex> lock(binding->mutex);
        if (binding->engine == nullptr) {
          return CaptureResult::kNotInitialized;
        }
        return binding->engine->UpdateMicSourceDevice(
            source_name, device_id, source_token);
      };

  return std::make_shared<MicSourceProperty>(
      initial_device_id, std::move(enumerator), std::move(updater));
}

//切换摄像头设备，主要进行检查参数检查状态的一些工作。
CaptureResult CaptureEngine::UpdateCameraSourceDevice(
    const std::string &source_name,
    const std::string &device_id,
    const std::shared_ptr<SourcePropertyToken> &source_token) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || m_runtime == nullptr) {
    return CaptureResult::kNotInitialized;
  }

  const auto source_it = m_sources.find(source_name);
  if (source_it == m_sources.end()) {
    return CaptureResult::kNotFound;
  }
  const auto token_it = m_source_property_tokens.find(source_name);
  if (token_it == m_source_property_tokens.end() ||
      token_it->second != source_token) {
    return CaptureResult::kNotFound;
  }
  if (!source_it->second ||
      source_it->second->GetSourceType() != CaptureSourceType::kCST_Camera) {
    return CaptureResult::kInvalidArgument;
  }

  return m_runtime->UpdateCameraSourceDevice(source_name, device_id);
}

CaptureResult CaptureEngine::UpdateMicSourceDevice(
    const std::string &source_name,
    const std::string &device_id,
    const std::shared_ptr<SourcePropertyToken> &source_token) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_initialized || m_runtime == nullptr) {
    return CaptureResult::kNotInitialized;
  }

  const auto source_it = m_sources.find(source_name);
  if (source_it == m_sources.end()) {
    return CaptureResult::kNotFound;
  }
  const auto token_it = m_source_property_tokens.find(source_name);
  if (token_it == m_source_property_tokens.end() ||
      token_it->second != source_token) {
    return CaptureResult::kNotFound;
  }
  if (!source_it->second ||
      source_it->second->GetSourceType() != CaptureSourceType::kCST_Microphone) {
    return CaptureResult::kInvalidArgument;
  }

  return m_runtime->UpdateMicSourceDevice(source_name, device_id);
}

} // namespace CAPTURE
