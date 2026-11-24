#ifndef CAPTURE_DEFINE_H
#define CAPTURE_DEFINE_H

#include <cstddef>

namespace CAPTURE {
enum class CaptureSourceType {
  kCST_Unknown = 0,
  kCST_Camera,
  kCST_Microphone
};

//防御性编程时需要的各种结果情况
enum class CaptureResult {
  kSuccess = 0,
  kAlreadyInitialized,
  kNotInitialized,
  kInvalidArgument,
  kInvalidData,
  kInvalidSize,
  kInvalidFormat,
  kInternalError,
  kNotFound
};

} // namespace CAPTURE

#endif //CAPTURE_DEFINE_H
