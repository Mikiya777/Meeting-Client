#ifndef CAMERA_SOURCE_PROPERTY_H
#define CAMERA_SOURCE_PROPERTY_H
/*
模块位置：
include/ICameraSourceProperty.h
        |
        v
src/property/CameraSourceProperty.h/.cpp
        |
        v
OBS / 系统摄像头设备枚举与属性配置

功能：
负责摄像头设备、分辨率、视频格式的选择；即枚举当前可用的选项、设置目标选项、查询目前选项
*/

#include "capture/include/ICameraSourceProperty.h"
#include <cstdint>
#include <functional>
#include <string>

namespace CAPTURE {
class CameraSourceProperty final : public ICameraSourceProperty {
public:
  using DeviceEnumerator = std::function<CaptureResult(DeviceEnumCallback)>;
  using DeviceUpdater = std::function<CaptureResult(const std::string &)>;

  CameraSourceProperty() = default;
  CameraSourceProperty(std::string initial_device_id,
                       DeviceEnumerator device_enumerator,
                       DeviceUpdater device_updater);
  ~CameraSourceProperty() override = default;

  //接口方法
  //摄像头
  void EnumCameraDevices(DeviceEnumCallback callback) override;
  CaptureResult SetVideoDevice(const char *device_id, bool update_now = true) override;
  const char *GetCurrentDeviceId() override;

  //分辨率
  void EnumResolution(ResolutionEnumCallback callback) override;
  CaptureResult SetResolution(const char *resolution) override;
  const char *GetCurrentResolution() override;

  //视频格式
  void EnumVideoFormat(VideoFormatEnumCallback callback) override;
  CaptureResult SetVideoFormat(int32_t index) override;
  const char *GetCurrentVideoFormat() override;

private:
  std::string m_current_device_id;
  std::string m_current_resolution;
  std::string m_current_video_format;
  int32_t m_current_video_format_index = -1;
  DeviceEnumerator m_device_enumerator;
  DeviceUpdater m_device_updater;
};
} // namespace CAPTURE
#endif //CAMERA_SOURCE_PROPERTY_H
