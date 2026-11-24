#include "log/include/Logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    std::exit(1);
  }
  std::cout << "[PASSED] " << message << '\n';
}

std::string ReadFirstLogFile(const std::filesystem::path &log_dir) {
  for (const auto &entry : std::filesystem::directory_iterator(log_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream file(entry.path());
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
  }
  return {};
}

} // namespace

int main() {
  const auto log_dir =
      std::filesystem::temp_directory_path() / "meeting-client-logger-test";
  std::filesystem::create_directories(log_dir);

  LOGGER::Logger::Instance().Shutdown();
  LOGGER::Logger::Instance().Initialize(LOGGER::LogOutput::kConsoleAndFile,
                                        LOGGER::LogLevel::kDebug,
                                        log_dir.string());

  LOG_DEBUG("debug message");
  LOG_INFO_STREAM << "stream message " << 42;
  LOGGER::Logger::Instance().Shutdown();

  const auto content = ReadFirstLogFile(log_dir);
  Expect(content.find("debug message") != std::string::npos,
         "non-stream log is written");
  Expect(content.find("stream message 42") != std::string::npos,
         "stream log is written");
  return 0;
}
