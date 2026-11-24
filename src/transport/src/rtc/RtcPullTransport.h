#ifndef RTC_PULL_TRANSPORT_H
#define RTC_PULL_TRANSPORT_H

#include "transport/include/ITransportEngine.h"

#include <memory>
#include <string>
/*
一路真实拉流会话。负责创建 sendonly PeerConnection/Track，生成 Offer，
通过 WHIP 协商，然后把本地 I420Frame / AudioFrame 编码后送进 WebR6TC Track 发给 SRS。
*/
namespace TRANSPORT {

class RtcPullTransport final {
public:
  RtcPullTransport();
  ~RtcPullTransport();

  bool Open(const PublishInfo &publish_info,
            const TargetRoomInfo &room_info,
            const std::string &user_id,
            const std::string &endpoint,
            VideoDataCallback video_callback,
            AudioDataCallback audio_callback);
  void Close();
  const std::string &ResourceUrl() const;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace TRANSPORT

#endif // RTC_PULL_TRANSPORT_H
