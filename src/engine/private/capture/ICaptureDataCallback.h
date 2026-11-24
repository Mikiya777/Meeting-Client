#ifndef ICAPTUREDATACALLBACK_H
#define ICAPTUREDATACALLBACK_H

#include "engine/public/VceTypes.h"

namespace VCE {

class ICaptureDataCallback {
public:
  virtual ~ICaptureDataCallback() = default;

  virtual void OnCaptureVideoFrame(const std::shared_ptr<I420Frame> &frame) = 0;
  virtual void OnCaptureAudioFrame(const std::shared_ptr<AudioFrame> &frame) = 0;
};

} // namespace VCE

#endif // ICAPTUREDATACALLBACK_H
