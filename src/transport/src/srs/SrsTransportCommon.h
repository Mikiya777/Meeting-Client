#ifndef SRS_TRANSPORT_COMMON_H
#define SRS_TRANSPORT_COMMON_H

#include "transport/include/TransportDefine.h"

#include <cstdint>
#include <string>

namespace TRANSPORT {

constexpr const char *kDefaultSrsApp = "meeting";
constexpr uint8_t kRtpPayloadTypeH264 = 96;
constexpr uint8_t kRtpPayloadTypeOpus = 111;
constexpr int kOpusSampleRate = 48000;
constexpr int kOpusFrameSamples = 960;
constexpr int kWhipTimeoutMs = 30000;
constexpr int kGatherTimeoutMs = 10000;

bool IsValidPublishInfo(const PublishInfo &info);
bool StartsWithHttpScheme(const std::string &url);
bool EnvFlagEnabled(const char *name);
std::string ToLower(std::string value);
std::string Trim(std::string value);
uint32_t GenerateRandomSsrc();
std::string GenerateId();
std::string ExtractHostFromHttpUrl(const std::string &url);
std::string ReplaceRtcMethodPath(const std::string &url,
                                 const std::string &from,
                                 const std::string &to);
std::string StripUrlQueryAndFragment(const std::string &url);
void RealSrsDebug(const std::string &message);
void EnsureCurlRuntime();
void EnsureRtcRuntime();

std::string BuildSrsEndpoint(const std::string &server_url,
                             const std::string &method,
                             const std::string &app,
                             const std::string &room_id,
                             const std::string &user_id,
                             const std::string &secret);
std::string MakeStreamName(const std::string &room_id,
                           const std::string &user_id);
std::string ResolveSrsApp(const std::string &app);
std::string UrlEncode(const std::string &value);
bool LooksLikeSrsRtcEndpoint(const std::string &url,
                             const std::string &method);
std::string TrimTrailingSlash(const std::string &value);

} // namespace TRANSPORT

#endif // SRS_TRANSPORT_COMMON_H
