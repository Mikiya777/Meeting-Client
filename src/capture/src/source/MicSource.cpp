#include "capture/src/source/MicSource.h"

#include <utility>

namespace CAPTURE {

MicSource::MicSource(std::string name,
                     std::string id,
                     std::shared_ptr<IMicSourceProperty> property)
    : BaseSource(CaptureSourceType::kCST_Microphone,
                 std::move(name),
                 std::move(id)),
      m_property(property ? std::move(property) : IMicSourceProperty::Create()) {}

std::shared_ptr<IMicSourceProperty> MicSource::GetProperty() const {
  return m_property;
}

} // namespace CAPTURE
