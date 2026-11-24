#ifndef LOG_LEVEL_H
#define LOG_LEVEL_H

#include <cstdint>

namespace LOGGER {

enum class LogLevel : uint8_t {
  kOff = 0,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kCritical
};

enum class LogOutput : uint8_t {
  kConsole = 0,
  kFile,
  kConsoleAndFile
};

} // namespace LOGGER

#endif // LOG_LEVEL_H
