#ifndef BASE_SOURCE_PROPERTY_H
#define BASE_SOURCE_PROPERTY_H

/*
模块位置：
include/ISourceProperty.h
        |
        v
src/property/BaseSourceProperty.h
        |
        |-- src/property/CameraSourceProperty.h/.cpp
        |
        |-- src/property/MicSourceProperty.h/.cpp
功能：
1. 作为所有属性实现类的公共基类
2. 提供一些通用状态，比如属性对象当前是否有效/是否已绑定 source
*/

#include "capture/include/ISourceProperty.h"

namespace CAPTURE {
class BaseSourceProperty : public ISourceProperty {
public:
  BaseSourceProperty() = default;
  ~BaseSourceProperty() override = default;
};
} // namespace CAPTURE

#endif //BASE_SOURCE_PROPERTY_H