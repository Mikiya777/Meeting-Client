#ifndef I_CAPTURE_ENGINE_H
#define I_CAPTURE_ENGINE_H

#include "capture/include/CaptureFrame.h"
#include "capture/include/CaptureDefine.h"
#include "capture/include/ISource.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>
/*
组件位置：
外部业务层 / UI / CaptureManager
        |
        v
ICaptureEngine.h
        |
        v
src/core/CaptureEngine.h/.cpp
        |
        v
OBS 核心初始化、音视频配置、source 管理、callback 注册
*/
namespace CAPTURE {
//回调函数
using VideoDataCallback = std::function<void(const std::shared_ptr<I420Frame> &frame)>;
using AudioDataCallback = std::function<void(const std::shared_ptr<AudioFrame> &frame)>;
using CaptureDeviceEnumCallback = std::function<bool(const char *device_id, const char *device_name)>;

class ICaptureEngine {
public:
  virtual ~ICaptureEngine() = default;

  static std::unique_ptr<ICaptureEngine> Create();

  //engine生命周期管理
  //对应 OBS 的初始化流程
  virtual CaptureResult Init(int sample_rate, int channels, int width, int height, int fps) = 0;
  virtual CaptureResult UnInit() = 0;
  virtual bool IsInitialized() const = 0;

  //源管理：创建/销毁/查询
  virtual std::shared_ptr<ISource> CreateSource(CaptureSourceType type, const std::string &name) = 0;
  virtual std::shared_ptr<ISource> CreateSource(CaptureSourceType type,
                                                const std::string &name,
                                                const std::string &device_id) = 0;
  virtual CaptureResult RemoveSource(const std::string &name) = 0;
  virtual std::shared_ptr<ISource> GetSource(const std::string &name) = 0;
  virtual std::vector<std::shared_ptr<ISource>> GetAllSources() = 0;

  //设备管理
  virtual CaptureResult EnumCameraDevices(CaptureDeviceEnumCallback callback) = 0;
  virtual CaptureResult EnumMicDevices(CaptureDeviceEnumCallback callback) = 0;

  // 回调在采集线程执行。替换回调时，已开始处理的帧可能仍调用旧回调。
  virtual CaptureResult RegisterVideoCallback(VideoDataCallback callback) = 0;
  virtual CaptureResult RegisterAudioCallback(AudioDataCallback callback) = 0;
};

} // namespace CAPTURE

#endif //I_CAPTURE_ENGINE_H
