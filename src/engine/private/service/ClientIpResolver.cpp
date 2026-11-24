#include "engine/private/service/ClientIpResolver.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "log/include/Logger.h"
#include "network/include/HttpClient.h"

namespace VCE {
namespace {

std::mutex g_resolved_client_ip_mutex;
bool g_resolved_client_ip_ready = false;
uint32_t g_resolved_client_ip = 0;

std::string TrimAscii(const std::string &value) {
  size_t begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }

  size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::optional<uint32_t> ParseIpv4(const std::string &value) {
  std::array<unsigned int, 4> octets{};
  char dot1 = '\0';
  char dot2 = '\0';
  char dot3 = '\0';
  std::istringstream stream(TrimAscii(value));
  stream >> octets[0] >> dot1 >> octets[1] >> dot2 >> octets[2] >> dot3 >>
      octets[3];
  if (!stream || dot1 != '.' || dot2 != '.' || dot3 != '.') {
    return std::nullopt;
  }

  char extra = '\0';
  if (stream >> extra) {
    return std::nullopt;
  }

  for (const auto octet : octets) {
    if (octet > 255) {
      return std::nullopt;
    }
  }

  return (octets[0] << 24) |
         (octets[1] << 16) |
         (octets[2] << 8) |
         octets[3];
}

} // namespace

uint32_t ResolveClientIp(uint32_t client_ip) {
  if (client_ip != 0) {
    return client_ip;
  }

  {
    std::lock_guard<std::mutex> lock(g_resolved_client_ip_mutex);
    if (g_resolved_client_ip_ready) {
      return g_resolved_client_ip;
    }
  }

  uint32_t resolved_ip = 0;
  if (const char *value = std::getenv("MEETING_CLIENT_IP")) {
    if (auto parsed = ParseIpv4(value)) {
      resolved_ip = *parsed;
      LOG_INFO("Client IP resolved from MEETING_CLIENT_IP");
    } else {
      LOG_WARN("MEETING_CLIENT_IP is not a valid IPv4 address");
    }
  }

  if (resolved_ip == 0) {
    HTTP::Client client;
    std::string ip;
    if (client.GetPublicIp(&ip)) {
      if (auto parsed = ParseIpv4(ip)) {
        resolved_ip = *parsed;
        LOG_INFO("Client public IP resolved by HTTP");
      }
    }
  }

  if (resolved_ip == 0) {
    LOG_WARN("Client public IP is unavailable; meeting service will receive 0");
  }

  std::lock_guard<std::mutex> lock(g_resolved_client_ip_mutex);
  g_resolved_client_ip = resolved_ip;
  g_resolved_client_ip_ready = true;
  return g_resolved_client_ip;
}

} // namespace VCE
