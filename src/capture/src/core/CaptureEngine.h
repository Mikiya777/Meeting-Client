#ifndef CAPTURE_ENGINE_H
#define CAPTURE_ENGINE_H

#include "capture/include/CaptureDefine.h"
#include "capture/include/ICaptureEngine.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace CAPTURE {

#ifdef _WIN32
class QtCaptureRuntime;
using CaptureRuntime = QtCaptureRuntime;
#else
class ObsRuntime;
using CaptureRuntime = ObsRuntime;
#endif
class ICameraSourceProperty;
class IMicSourceProperty;

class CaptureEngine final : public ICaptureEngine {
public:
  CaptureEngine();
  ~CaptureEngine() override;

  CaptureResult Init(int sample_rate,
                     int channels,
                     int width,
                     int height,
                     int fps) override;
  CaptureResult UnInit() override;
  bool IsInitialized() const override;

  std::shared_ptr<ISource> CreateSource(
      CaptureSourceType type,
      const std::string &name) override;
  std::shared_ptr<ISource> CreateSource(CaptureSourceType type,
                                        const std::string &name,
                                        const std::string &device_id) override;
  CaptureResult RemoveSource(const std::string &name) override;
  std::shared_ptr<ISource> GetSource(const std::string &name) override;
  std::vector<std::shared_ptr<ISource>> GetAllSources() override;

  CaptureResult EnumCameraDevices(CaptureDeviceEnumCallback callback) override;
  CaptureResult EnumMicDevices(CaptureDeviceEnumCallback callback) override;

  CaptureResult RegisterVideoCallback(VideoDataCallback callback) override;
  CaptureResult RegisterAudioCallback(AudioDataCallback callback) override;

private:
  // 属性对象可能比对应 source 或 CaptureEngine 活得更久。
  // Binding 保护 Engine 生命周期，Token 区分同名 source 的不同实例。
  struct SourcePropertyBinding {
    std::mutex mutex;
    CaptureEngine *engine = nullptr;
  };

  struct SourcePropertyToken {};

  std::shared_ptr<ISource> CreateSourceLocked(
      CaptureSourceType type,
      const std::string &name,
      const std::string *device_id);
  void RollbackSourceCreation(const std::string &name,
                              bool source_inserted,
                              bool token_inserted) noexcept;

  //创建源属性
  std::shared_ptr<ICameraSourceProperty> CreateCameraSourceProperty(
      const std::string &source_name,
      const std::string &initial_device_id,
      const std::shared_ptr<SourcePropertyToken> &source_token);
  std::shared_ptr<IMicSourceProperty> CreateMicSourceProperty(
      const std::string &source_name,
      const std::string &initial_device_id,
      const std::shared_ptr<SourcePropertyToken> &source_token);
  //切换设备
  CaptureResult UpdateCameraSourceDevice(const std::string &source_name,
                                         const std::string &device_id,
                                         const std::shared_ptr<SourcePropertyToken> &source_token);
  CaptureResult UpdateMicSourceDevice(const std::string &source_name,
                                      const std::string &device_id,
                                      const std::shared_ptr<SourcePropertyToken> &source_token);

  // Protects engine state, source ownership, and callback registration.
  mutable std::mutex m_mutex;
  bool m_initialized = false;

  int m_sample_rate = 0;
  int m_channels = 0;
  int m_width = 0;
  int m_height = 0;
  int m_fps = 0;

  VideoDataCallback m_video_callback;
  AudioDataCallback m_audio_callback;

  std::unique_ptr<CaptureRuntime> m_runtime;
  std::unordered_map<std::string, std::shared_ptr<ISource>> m_sources;
  std::unordered_map<std::string, std::shared_ptr<SourcePropertyToken>>
      m_source_property_tokens;
  std::shared_ptr<SourcePropertyBinding> m_source_property_binding;
};

} // namespace CAPTURE

#endif // CAPTURE_ENGINE_H
