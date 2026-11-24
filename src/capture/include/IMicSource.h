#ifndef I_MIC_SOURCE_H
#define I_MIC_SOURCE_H
/*
组件位置：
ISource
   |
   v
IMicSource
   |
   v
src/source/MicSource.h/.cpp
   |
   v
OBS microphone source / wasapi_input_capture / 系统音频采集能力

功能：
1.表示麦克风源
2.负责源类型、源名称、源id（这里都从ISource处继承了）和获取属性对象
*/

#include <memory>
#include "capture/include/IMicSourceProperty.h"
#include "capture/include/ISource.h"
namespace CAPTURE {
class IMicSource : public virtual ISource {
public:
  virtual ~IMicSource() = default;

  virtual std::shared_ptr<IMicSourceProperty> GetProperty() const = 0;
};

} // namespace CAPTURE
#endif //I_MIC_SOURCE_H
