#ifndef MIC_SOURCE_H
#define MIC_SOURCE_H

#include "capture/include/IMicSource.h"
#include "capture/src/source/BaseSource.h"

#include <memory>
#include <string>

namespace CAPTURE {

class MicSource final : public BaseSource, public IMicSource {
public:
  explicit MicSource(
      std::string name,
      std::string id = {},
      std::shared_ptr<IMicSourceProperty> property = nullptr);
  ~MicSource() override = default;

  std::shared_ptr<IMicSourceProperty> GetProperty() const override;

private:
  std::shared_ptr<IMicSourceProperty> m_property;
};

} // namespace CAPTURE

#endif // MIC_SOURCE_H
