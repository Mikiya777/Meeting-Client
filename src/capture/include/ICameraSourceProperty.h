#ifndef I_CAMERA_SOURCE_PROPERTY_H
#define I_CAMERA_SOURCE_PROPERTY_H
/*
组件位置：
ISourceProperty
        |
        v
ICameraSourceProperty
        |
        v
src/property/CameraSourceProperty.h/.cpp
        |
        v
OBS / 系统摄像头设备能力

功能：
1.ICameraSourceProperty表示摄像头源的属性
2.负责摄像头设备、分辨率、视频格式的选择；即枚举当前可用的选项、设置目标选项、查询目前选项
*/

#include "capture/include/CaptureDefine.h"
#include "capture/include/ISourceProperty.h"

#include <functional>
#include <memory>
#include <cstdint>

namespace CAPTURE {
class ICameraSourceProperty : public ISourceProperty {
public:
  //device_id：摄像头设备 ID，通常用于后续切换设备; device_name：摄像头设备名称，通常用于展示给用户; 返回 bool：可以用来控制是否继续枚举。比如返回 false 表示停止枚举。
  //const char * 表示只读的字符串，一般用于显示。
  using DeviceEnumCallback = std::function<bool(const char *device_id, const char *device_name)>;
  using ResolutionEnumCallback = std::function<bool(const char *resolution)>;
  using VideoFormatEnumCallback = std::function<bool(const char *format, int32_t index)>;

  virtual ~ICameraSourceProperty() = default;

  //静态工厂函数
  static std::shared_ptr<ICameraSourceProperty> Create();

  //摄像头
  virtual void EnumCameraDevices(DeviceEnumCallback callback) = 0;
  //update_now：是否立即更新到底层采集源，默认是 true。
  virtual CaptureResult SetVideoDevice(const char *device_id, bool update_now = true) = 0;
  virtual const char *GetCurrentDeviceId() = 0;

  //分辨率
  virtual void EnumResolution(ResolutionEnumCallback callback) = 0;
  virtual CaptureResult SetResolution(const char *resolution) = 0;
  virtual const char *GetCurrentResolution() = 0;

  //视频格式
  virtual void EnumVideoFormat(VideoFormatEnumCallback callback) = 0;
  //index 根据格式编号设置当前视频格式；int32_t是用来表示视频格式列表中的序号（包括非法值-1）。
  virtual CaptureResult SetVideoFormat(int32_t index) = 0;
  virtual const char *GetCurrentVideoFormat() = 0;
};

} // namespace CAPTURE

#endif // I_CAMERA_SOURCE_PROPERTY_H
