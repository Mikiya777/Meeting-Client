#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <cstdint>
#include <map>
#include <string>

namespace HTTP {

struct Response {
  int status_code = 0;
  std::string body;
  std::map<std::string, std::string> headers;
};

class Client {
public:
  static constexpr uint32_t kDefaultTimeoutMs = 30000;

  Client();
  ~Client();

  Response GetRequest(
      const std::string &url,
      const std::map<std::string, std::string> &headers = {},
      uint32_t timeout_ms = kDefaultTimeoutMs);
  Response PostRequest(
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const std::string &body,
      uint32_t timeout_ms = kDefaultTimeoutMs);
  Response DeleteRequest(
      const std::string &url,
      const std::map<std::string, std::string> &headers = {},
      uint32_t timeout_ms = kDefaultTimeoutMs);

  bool GetPublicIp(std::string *ip);

  static void GlobalInit();
  static void GlobalCleanup();

private:
  static constexpr uint32_t kPublicIpTimeoutMs = 1500;

  Response SendRequest(
      const std::string &method,
      const std::string &url,
      const std::map<std::string, std::string> &headers,
      const std::string &body,
      uint32_t timeout_ms);

  static size_t WriteCallback(void *contents,
                              size_t size,
                              size_t nmemb,
                              void *userp);
  static size_t HeaderCallback(char *buffer,
                               size_t size,
                               size_t nitems,
                               void *userdata);
  static bool IsValidIpv4(const std::string &ip);
};

} // namespace HTTP

#endif // HTTP_CLIENT_H
