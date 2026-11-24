#ifndef I_CAMERA_SOURCE_H
#define I_CAMERA_SOURCE_H
/*
组件位置：
外部业务层 / UI / CaptureManager
        |
        v
ICaptureEngine::CreateSource(...)
        |
        v
ISource / ICameraSource
        |
        v
src/source/CameraSource.h/.cpp
        |
        v
OBS camera source / dshow_input / av_capture_input 等第三方实现

功能：
1.表示摄像头源
2.负责源类型、源名称、源id（这里都从ISource处继承了）和获取属性对象
*/
#include "capture/include/ISource.h"
#include "capture/include/ICameraSourceProperty.h"

#include <memory>

namespace CAPTURE {
class ICameraSource : public virtual ISource {
public:
  //允许外部通过基类指针安全释放派生类对象。
  virtual ~ICameraSource() = default;

  //源具有属性，返回类型为std::shared_ptr<ICameraSourceProperty>；使用shared_ptr是为了在多个模块间共享，例如UI也要用到ICameraSourceProperty
  virtual std::shared_ptr<ICameraSourceProperty> GetProperty() = 0;
};
} // namespace CAPTURE

#endif //I_CAMERA_SOURCE_H
