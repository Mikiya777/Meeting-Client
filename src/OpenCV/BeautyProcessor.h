#pragma once

#include "engine/public/VceTypes.h"

#include <atomic>
#include <memory>

namespace VCE {

// Fixed, light full-frame smoothing. Input planes are never modified.
class BeautyProcessor {
public:
  void SetEnabled(bool enabled) { m_enabled.store(enabled); }
  bool IsEnabled() const { return m_enabled.load(); }

  // Disabled, invalid, or failed processing returns the original frame.
  std::shared_ptr<I420Frame> Process(
      const std::shared_ptr<I420Frame> &frame) const;

private:
  std::atomic<bool> m_enabled{false};
};

} // namespace VCE
