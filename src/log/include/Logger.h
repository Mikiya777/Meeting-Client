#ifndef LOGGER_H
#define LOGGER_H

#include "log/include/LogLevel.h"

#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace spdlog {
class logger;
}

namespace LOGGER {

class Logger {
public:
  static Logger &Instance();

  void Initialize(LogOutput output,
                  LogLevel level,
                  const std::string &log_dir = "");
  void Shutdown();
  void SetLevel(LogLevel level);

  void Log(LogLevel level, const char *file, int line, const std::string &message);

  bool IsInitialized() const;
  LogLevel GetLevel() const;

private:
  Logger() = default;
  ~Logger();

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  void InitializeLocked(LogOutput output,
                        LogLevel level,
                        const std::string &log_dir);

  mutable std::mutex mutex_;
  bool initialized_ = false;
  LogLevel level_ = LogLevel::kDebug;
  LogOutput output_ = LogOutput::kConsole;
  std::shared_ptr<spdlog::logger> logger_;
};

class LogStream {
public:
  LogStream(LogLevel level, const char *file, int line);
  ~LogStream();

  template <typename T>
  LogStream &operator<<(const T &value) {
    stream_ << value;
    return *this;
  }

private:
  LogLevel level_;
  const char *file_;
  int line_;
  std::ostringstream stream_;
};

} // namespace LOGGER

#define LOG_DEBUG(message)                                                     \
  LOGGER::Logger::Instance().Log(LOGGER::LogLevel::kDebug, __FILE__, __LINE__, \
                                 (message))

#define LOG_INFO(message)                                                     \
  LOGGER::Logger::Instance().Log(LOGGER::LogLevel::kInfo, __FILE__, __LINE__, \
                                 (message))

#define LOG_WARN(message)                                                     \
  LOGGER::Logger::Instance().Log(LOGGER::LogLevel::kWarn, __FILE__, __LINE__, \
                                 (message))

#define LOG_ERROR(message)                                                     \
  LOGGER::Logger::Instance().Log(LOGGER::LogLevel::kError, __FILE__, __LINE__, \
                                 (message))

#define LOG_CRITICAL(message)                                                  \
  LOGGER::Logger::Instance().Log(LOGGER::LogLevel::kCritical, __FILE__,        \
                                 __LINE__, (message))

#define LOG_DEBUG_STREAM                                                       \
  LOGGER::LogStream(LOGGER::LogLevel::kDebug, __FILE__, __LINE__)

#define LOG_INFO_STREAM                                                        \
  LOGGER::LogStream(LOGGER::LogLevel::kInfo, __FILE__, __LINE__)

#define LOG_WARN_STREAM                                                        \
  LOGGER::LogStream(LOGGER::LogLevel::kWarn, __FILE__, __LINE__)

#define LOG_ERROR_STREAM                                                       \
  LOGGER::LogStream(LOGGER::LogLevel::kError, __FILE__, __LINE__)

#define LOG_CRITICAL_STREAM                                                    \
  LOGGER::LogStream(LOGGER::LogLevel::kCritical, __FILE__, __LINE__)

#endif // LOGGER_H
