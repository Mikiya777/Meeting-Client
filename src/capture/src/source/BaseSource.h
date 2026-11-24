#ifndef BASE_SOURCE_H
#define BASE_SOURCE_H
/*
组件位置：
include/ISource.h
        |
        v
src/source/BaseSource.h/.cpp
        |
        |-- src/source/CameraSource.h/.cpp
        |
        |-- src/source/MicSource.h/.cpp
功能：BaseSource 是内部实现基类，负责把 ISource 里所有 source 通用的部分先实现掉
*/
#include "capture/include/CaptureDefine.h"
#include "capture/include/ISource.h"

#include <string>
namespace CAPTURE {
class BaseSource : public virtual ISource {
public:
  BaseSource(CaptureSourceType type,
             std::string name,
             std::string id);
  ~BaseSource() override = default;

  //接口方法部分
  CaptureSourceType GetSourceType() const override;
  const char *GetSourceName() const override;
  const char *GetSourceId() const override;

  //专用方法部分
protected:
  void SetSourceId(std::string id);

private:
  CaptureSourceType m_type = CaptureSourceType::kCST_Unknown;
  std::string m_name;
  std::string m_id;
};
} // namespace CAPTURE
#endif //BASE_SOURCE_H
