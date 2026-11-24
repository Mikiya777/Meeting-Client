#include "transport/src/srs/SrsWhipWhepClient.h"

#include "network/include/HttpClient.h"

#include <regex>

#include "log/include/Logger.h"

namespace TRANSPORT {
namespace {

HttpResponse ToTransportResponse(const HTTP::Response &response) {
  HttpResponse result;
  result.status_code = response.status_code;
  result.body = response.body;
  result.headers = response.headers;
  return result;
}

} // namespace

HttpResponse SendHttpRequest(const std::string &method,
                             const std::string &url,
                             const std::map<std::string, std::string> &headers,
                             const std::string &body,
                             int timeout_ms) {
  HttpResponse response;
  HTTP::Client client;
  if (method == "GET") {
    return ToTransportResponse(
        client.GetRequest(url, headers, static_cast<uint32_t>(timeout_ms)));
  }
  if (method == "POST") {
    return ToTransportResponse(
        client.PostRequest(url, headers, body, static_cast<uint32_t>(timeout_ms)));
  }
  if (method == "DELETE") {
    return ToTransportResponse(
        client.DeleteRequest(url, headers, static_cast<uint32_t>(timeout_ms)));
  }
  LOG_WARN_STREAM << "Unsupported HTTP method: " << method;
  (void)body;
  return response;
}

std::string ParseResourceUrl(const std::map<std::string, std::string> &headers) {
  const auto location = headers.find("location");
  if (location != headers.end()) {
    return location->second;
  }

  const auto link = headers.find("link");
  if (link != headers.end()) {
    const std::regex pattern(R"(<([^>]+)>)");
    std::smatch match;
    if (std::regex_search(link->second, match, pattern)) {
      return match[1].str();
    }
  }
  return {};
}

} // namespace TRANSPORT
