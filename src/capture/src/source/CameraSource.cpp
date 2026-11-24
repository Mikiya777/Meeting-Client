#include "capture/src/source/CameraSource.h"

#include <utility>

namespace CAPTURE {

CameraSource::CameraSource(std::string name,
                           std::string id,
                           std::shared_ptr<ICameraSourceProperty> property)
    : BaseSource(CaptureSourceType::kCST_Camera,
                 std::move(name),
                 std::move(id)),
      m_property(property ? std::move(property) : ICameraSourceProperty::Create()) {}

std::shared_ptr<ICameraSourceProperty> CameraSource::GetProperty() {
  return m_property;
}

} // namespace CAPTURE
