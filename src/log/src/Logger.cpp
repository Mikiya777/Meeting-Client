#include "log/include/Logger.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>

namespace LOGGER {
namespace {

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
  switch (level) {
  case LogLevel::kDebug:
    return spdlog::level::debug;
  case LogLevel::kInfo:
    return spdlog::level::info;
  case LogLevel::kWarn:
    return spdlog::level::warn;
  case LogLevel::kError:
    return spdlog::level::err;
  case LogLevel::kCritical:
    return spdlog::level::critical;
  case LogLevel::kOff:
    return spdlog::level::off;
  }
  return spdlog::level::info;
}

std::string BuildLogFileName() {
  const auto now = std::chrono::system_clock::now();
  const auto now_time = std::chrono::system_clock::to_time_t(now);

  std::tm time_info{};
#if defined(_WIN32) || defined(_WIN64)
  localtime_s(&time_info, &now_time);
#else
  localtime_r(&now_time, &time_info);
#endif

  std::ostringstream stream;
  stream << "meeting-client-" << std::put_time(&time_info, "%Y%m%d-%H%M%S")
         << ".log";
  return stream.str();
}

} // namespace

Logger &Logger::Instance() {
  static Logger logger;
  return logger;
}

Logger::~Logger() {
  Shutdown();
}

void Logger::Initialize(LogOutput output,
                        LogLevel level,
                        const std::string &log_dir) {
  std::lock_guard<std::mutex> lock(mutex_);
  InitializeLocked(output, level, log_dir);
}

void Logger::InitializeLocked(LogOutput output,
                              LogLevel level,
                              const std::string &log_dir) {
  if (initialized_) {
    return;
  }

  output_ = output;
  level_ = level;

  std::vector<spdlog::sink_ptr> sinks;
  if (output == LogOutput::kConsole || output == LogOutput::kConsoleAndFile) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern(
        "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] [tid:%t] %v");
    sinks.push_back(console_sink);
  }

  if (output == LogOutput::kFile || output == LogOutput::kConsoleAndFile) {
    const std::filesystem::path dir =
        log_dir.empty() ? std::filesystem::path("logs")
                        : std::filesystem::path(log_dir);
    std::filesystem::create_directories(dir);

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        (dir / BuildLogFileName()).string(), true);
    file_sink->set_pattern(
        "[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] [tid:%t] %v");
    sinks.push_back(file_sink);
  }

  if (sinks.empty()) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(console_sink);
  }

  logger_ =
      std::make_shared<spdlog::logger>("meeting-client", sinks.begin(), sinks.end());
  logger_->set_level(ToSpdlogLevel(level_));
  logger_->flush_on(spdlog::level::debug);
  initialized_ = true;
}

void Logger::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return;
  }

  if (logger_) {
    logger_->flush();
    logger_.reset();
  }
  initialized_ = false;
}

void Logger::SetLevel(LogLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  level_ = level;
  if (logger_) {
    logger_->set_level(ToSpdlogLevel(level));
  }
}

void Logger::Log(LogLevel level,
                 const char *file,
                 int line,
                 const std::string &message) {
  std::shared_ptr<spdlog::logger> current_logger;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
      InitializeLocked(LogOutput::kConsole, LogLevel::kDebug, "");
    }
    current_logger = logger_;
  }

  if (!current_logger) {
    return;
  }

  const spdlog::source_loc source{file ? file : "unknown", line, ""};
  current_logger->log(source, ToSpdlogLevel(level), message);
}

bool Logger::IsInitialized() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return initialized_;
}

LogLevel Logger::GetLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return level_;
}

LogStream::LogStream(LogLevel level, const char *file, int line)
    : level_(level), file_(file), line_(line) {}

LogStream::~LogStream() {
  Logger::Instance().Log(level_, file_, line_, stream_.str());
}

} // namespace LOGGER
