#include "capture/src/source/BaseSource.h"
#include <utility>

namespace CAPTURE {
BaseSource::BaseSource(CaptureSourceType type, std::string name, std::string id)
    : m_type(type),
      m_name(std::move(name)),
      m_id(std::move(id)) {}

CaptureSourceType BaseSource::GetSourceType() const {
  return m_type;
}

const char *BaseSource::GetSourceName() const {
  return m_name.c_str();
}

const char *BaseSource::GetSourceId() const {
  return m_id.c_str();
}

void BaseSource::SetSourceId(std::string id) {
  m_id = std::move(id);
}

} // namespace CAPTURE