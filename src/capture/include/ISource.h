#ifndef I_SOURCE_H
#define I_SOURCE_H

/*
  功能：
  1.ISource表示最抽象的源，后续将会被Camera和Mic的source继承。
  2.ISource给出最基本的源功能：获取源类型、源名称、源id
*/

#include "capture/include/CaptureDefine.h"
namespace CAPTURE {
class ISource {
public:
  virtual ~ISource() = default;

  virtual CaptureSourceType GetSourceType() const = 0;
  virtual const char *GetSourceName() const = 0;
  virtual const char *GetSourceId() const = 0;
};

} // namespace CAPTURE

#endif //I_SOURCE_H