#ifndef SRS_WHIP_WHEP_CLIENT_H
#define SRS_WHIP_WHEP_CLIENT_H

#include <map>
#include <string>
/*
HTTP 协商客户端。
负责向 SRS 发 WHIP/WHEP 请求，也就是把 SDP Offer POST 给 SRS，然后拿回 SDP Answer 和 resource url。
它不负责真正传媒体数据，只负责“协商”。
*/
namespace TRANSPORT {

struct HttpResponse {
  int status_code = 0;
  std::string body;
  std::map<std::string, std::string> headers;
};

HttpResponse SendHttpRequest(const std::string &method,
                             const std::string &url,
                             const std::map<std::string, std::string> &headers,
                             const std::string &body,
                             int timeout_ms);
std::string ParseResourceUrl(const std::map<std::string, std::string> &headers);

} // namespace TRANSPORT

#endif // SRS_WHIP_WHEP_CLIENT_H
