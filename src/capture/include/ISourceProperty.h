#ifndef I_SOURCE_PROPERTY_H
#define I_SOURCE_PROPERTY_H

/*
 组件位置：
 ISourceProperty
    |
    |-- ICameraSourceProperty
    |
    |-- IMicSourceProperty

  功能：
  1.它是采集源属性对象的公共接口基类
  2.负责类型统一
*/
namespace CAPTURE {

class ISourceProperty {
public:
  virtual ~ISourceProperty() = default;
};

} // namespace CAPTURE

#endif // I_SOURCE_PROPERTY_H