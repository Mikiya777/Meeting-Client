#include "network/include/HttpClient.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include <curl/curl.h>

#include "log/include/Logger.h"

namespace HTTP {
namespace {

std::mutex g_curl_init_mutex;
int g_curl_init_count = 0;

const std::array<const char *, 9> kPublicIpServices = {
    "http://api.ipify.org",
    "http://icanhazip.com",
    "http://checkip.amazonaws.com",
    "http://ipinfo.io/ip",
    "http://myexternalip.com/raw",
    "http://www.trackip.net/ip",
    "http://ipecho.net/plain",
    "http://myip.dnsomatic.com",
    "http://www.ip-adress.com/",
};

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

std::string ToLower(std::string value) {
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

std::optional<uint32_t> ParseIpv4(const std::string &value) {
  std::array<unsigned int, 4> octets{};
  char dot1 = '\0';
  char dot2 = '\0';
  char dot3 = '\0';
  std::istringstream stream(Trim(value));
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

Client::Client() {
  GlobalInit();
}

Client::~Client() {
  GlobalCleanup();
}

void Client::GlobalInit() {
  std::lock_guard<std::mutex> lock(g_curl_init_mutex);
  if (g_curl_init_count == 0) {
    curl_global_init(CURL_GLOBAL_ALL);
  }
  ++g_curl_init_count;
}

void Client::GlobalCleanup() {
  std::lock_guard<std::mutex> lock(g_curl_init_mutex);
  if (g_curl_init_count <= 0) {
    return;
  }

  --g_curl_init_count;
  if (g_curl_init_count == 0) {
    curl_global_cleanup();
  }
}

Response Client::GetRequest(
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    uint32_t timeout_ms) {
  return SendRequest("GET", url, headers, "", timeout_ms);
}

Response Client::PostRequest(
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    const std::string &body,
    uint32_t timeout_ms) {
  return SendRequest("POST", url, headers, body, timeout_ms);
}

Response Client::DeleteRequest(
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    uint32_t timeout_ms) {
  return SendRequest("DELETE", url, headers, "", timeout_ms);
}

bool Client::GetPublicIp(std::string *ip) {
  if (!ip) {
    return false;
  }

  for (const auto *url : kPublicIpServices) {
    const auto response = GetRequest(url, {}, kPublicIpTimeoutMs);
    const std::string candidate = Trim(response.body);
    if (response.status_code >= 200 &&
        response.status_code < 300 &&
        IsValidIpv4(candidate)) {
      *ip = candidate;
      return true;
    }
  }

  ip->clear();
  return false;
}

Response Client::SendRequest(
    const std::string &method,
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    const std::string &body,
    uint32_t timeout_ms) {
  Response response;
  CURL *curl = curl_easy_init();
  if (!curl) {
    LOG_ERROR("Failed to initialize curl easy handle");
    return response;
  }

  curl_slist *header_list = nullptr;
  for (const auto &header : headers) {
    const std::string line = header.first + ": " + header.second;
    header_list = curl_slist_append(header_list, line.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
  curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

  if (!body.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(body.size()));
  }

  const CURLcode result = curl_easy_perform(curl);
  if (result == CURLE_OK) {
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    response.status_code = static_cast<int>(status_code);
    LOG_DEBUG_STREAM << "HTTP " << method << " " << url
                     << " status=" << response.status_code;
  } else {
    LOG_WARN_STREAM << "HTTP " << method << " " << url
                    << " failed: " << curl_easy_strerror(result);
  }

  if (header_list) {
    curl_slist_free_all(header_list);
  }
  curl_easy_cleanup(curl);
  return response;
}

size_t Client::WriteCallback(void *contents,
                             size_t size,
                             size_t nmemb,
                             void *userp) {
  const size_t total = size * nmemb;
  auto *body = static_cast<std::string *>(userp);
  body->append(static_cast<const char *>(contents), total);
  return total;
}

size_t Client::HeaderCallback(char *buffer,
                              size_t size,
                              size_t nitems,
                              void *userdata) {
  const size_t total = size * nitems;
  std::string header(buffer, total);
  const size_t position = header.find(':');
  if (position == std::string::npos) {
    return total;
  }

  auto *headers = static_cast<std::map<std::string, std::string> *>(userdata);
  const std::string key = ToLower(Trim(header.substr(0, position)));
  const std::string value = Trim(header.substr(position + 1));
  if (!key.empty()) {
    (*headers)[key] = value;
  }
  return total;
}

bool Client::IsValidIpv4(const std::string &ip) {
  return ParseIpv4(ip).has_value();
}

} // namespace HTTP
