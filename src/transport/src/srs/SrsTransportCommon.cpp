#include "transport/src/srs/SrsTransportCommon.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>

#if TRANSPORT_ENABLE_REAL_SRS
#include <rtc/rtc.hpp>
#endif

namespace TRANSPORT {

bool IsValidPublishInfo(const PublishInfo &info) {
  return info.video_width > 0 &&
         info.video_height > 0 &&
         info.video_fps > 0 &&
         info.audio_sample_rate > 0 &&
         info.audio_channels > 0;
}

bool StartsWithHttpScheme(const std::string &url) {
  return url.rfind("http://", 0) == 0 ||
         url.rfind("https://", 0) == 0;
}

bool EnvFlagEnabled(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }

  const std::string flag(value);
  return flag == "1" || flag == "true" || flag == "TRUE" ||
         flag == "on" || flag == "ON" || flag == "yes" || flag == "YES";
}

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::string Trim(std::string value) {
  auto is_space = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };

  value.erase(value.begin(),
              std::find_if_not(value.begin(), value.end(), is_space));
  value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(),
              value.end());
  return value;
}

uint32_t GenerateRandomSsrc() {
  static std::random_device rd;
  static std::mt19937 generator(rd());
  static std::uniform_int_distribution<uint32_t> distribution(1, UINT32_MAX);
  return distribution(generator);
}

std::string GenerateId() {
  static std::atomic<uint64_t> counter{1};
  std::ostringstream stream;
  stream << std::hex
         << std::chrono::steady_clock::now().time_since_epoch().count()
         << counter.fetch_add(1);
  return stream.str();
}

std::string ExtractHostFromHttpUrl(const std::string &url) {
  static const std::regex pattern(R"(^https?://([^/:?#]+))",
                                  std::regex::icase);
  std::smatch match;
  if (!std::regex_search(url, match, pattern) || match.size() < 2) {
    return {};
  }
  return match[1].str();
}

std::string ReplaceRtcMethodPath(const std::string &url,
                                 const std::string &from,
                                 const std::string &to) {
  std::string result = url;
  const std::string needle = "/rtc/v1/" + from + "/";
  const std::string replacement = "/rtc/v1/" + to + "/";
  const auto position = result.find(needle);
  if (position == std::string::npos) {
    return result;
  }
  result.replace(position, needle.size(), replacement);
  return result;
}

std::string StripUrlQueryAndFragment(const std::string &url) {
  const auto position = url.find_first_of("?#");
  return position == std::string::npos ? url : url.substr(0, position);
}

void RealSrsDebug(const std::string &message) {
  if (EnvFlagEnabled("MEETING_SRS_DEBUG")) {
    std::cerr << "[SRS-TRANSPORT] " << message << '\n';
  }
}

#if TRANSPORT_ENABLE_REAL_SRS
namespace {
class RtcRuntime {
public:
  RtcRuntime() {
    rtcPreload();
  }

  ~RtcRuntime() {
    rtcCleanup();
  }
};
} // namespace
#endif

void EnsureCurlRuntime() {
  // curl runtime is owned by HTTP::Client in the network module.
}

void EnsureRtcRuntime() {
#if TRANSPORT_ENABLE_REAL_SRS
  static RtcRuntime runtime;
  (void)runtime;
#endif
}

std::string BuildSrsEndpoint(const std::string &server_url,
                             const std::string &method,
                             const std::string &app,
                             const std::string &room_id,
                             const std::string &user_id,
                             const std::string &secret) {
  if (!StartsWithHttpScheme(server_url) || room_id.empty() || user_id.empty()) {
    return {};
  }

  if (LooksLikeSrsRtcEndpoint(server_url, method)) {
    return server_url;
  }

  std::ostringstream endpoint;
  endpoint << TrimTrailingSlash(server_url)
           << "/rtc/v1/" << method << "/?app=" << UrlEncode(ResolveSrsApp(app))
           << "&stream=" << UrlEncode(MakeStreamName(room_id, user_id));
  if (method == "whip" && !secret.empty()) {
    endpoint << "&secret=" << UrlEncode(secret);
  }
  return endpoint.str();
}

std::string MakeStreamName(const std::string &room_id,
                           const std::string &user_id) {
  if (room_id.empty() || user_id.empty()) {
    return {};
  }

  return room_id + "_" + user_id;
}

std::string ResolveSrsApp(const std::string &app) {
  return app.empty() ? kDefaultSrsApp : app;
}

std::string UrlEncode(const std::string &value) {
  std::ostringstream encoded;
  encoded << std::uppercase << std::hex;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded << static_cast<char>(ch);
    } else {
      encoded << '%' << std::setw(2) << std::setfill('0')
              << static_cast<int>(ch);
    }
  }
  return encoded.str();
}

bool LooksLikeSrsRtcEndpoint(const std::string &url,
                             const std::string &method) {
  return url.find("/rtc/v1/" + method + "/") != std::string::npos;
}

std::string TrimTrailingSlash(const std::string &value) {
  if (value.empty()) {
    return value;
  }

  size_t end = value.size();
  while (end > 0 && value[end - 1] == '/') {
    --end;
  }
  return value.substr(0, end);
}

} // namespace TRANSPORT
