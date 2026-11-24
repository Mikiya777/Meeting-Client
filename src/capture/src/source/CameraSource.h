#ifndef CAMERA_SOURCE_H
#define CAMERA_SOURCE_H

#include "capture/include/ICameraSource.h"
#include "capture/src/source/BaseSource.h"

#include <memory>
#include <string>

namespace CAPTURE {

class CameraSource final : public BaseSource, public ICameraSource {
public:
  explicit CameraSource(
      std::string name,
      std::string id = {},
      std::shared_ptr<ICameraSourceProperty> property = nullptr);
  ~CameraSource() override = default;

  std::shared_ptr<ICameraSourceProperty> GetProperty() override;

private:
  // TODO(obs): 后续接入真实 OBS source 后，property 需要和底层 source 配置联动。
  std::shared_ptr<ICameraSourceProperty> m_property;
};

} // namespace CAPTURE

#endif // CAMERA_SOURCE_H
