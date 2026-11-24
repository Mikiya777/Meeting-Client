#ifndef RTC_PUSH_TRANSPORT_H
#define RTC_PUSH_TRANSPORT_H

#include "transport/include/ITransportEngine.h"

#include <memory>
#include <string>
/*
一路真实拉流会话。负责创建 recvonly PeerConnection/Track，通过 WHEP 协商，从 SRS 接收 RTP 媒体数据，解包/解码后通过回调交回上层。

1.RtcPushTransport 表示一路真实推流会话对象。
上游是 SrsTransportEngine，下游是 Codec / PeerConnection / SRS。
它接收原始 I420Frame / AudioFrame，内部编码后通过 WebRTC Track 推到 SRS。

2.生命周期为：构造 -> Open -> PushFrame 运行 -> Close -> 析构
3.它持有一个内部类Impl
4.它需要依赖：1.对象处于Open的生命状态，2.对应的track已经打开，3.参数合法。才能工作。
5.验证方法：本地验证：函数返回、状态、日志，端到端验证：SRS 收到、WHEP 拉到、回调收到帧
*/
namespace TRANSPORT {

class RtcPushTransport final {
public:
  RtcPushTransport();
  ~RtcPushTransport();

  //生命周期
  bool Open(const PublishInfo &publish_info,
            const TargetRoomInfo &room_info,
            const std::string &endpoint);
  void Close();

  //业务函数
  bool PushVideoFrame(const std::shared_ptr<I420Frame> &frame);
  bool PushAudioFrame(const std::shared_ptr<AudioFrame> &frame);
  const std::string &ResourceUrl() const;

private:
  // PImpl：隐藏 WebRTC / Codec / HTTP 等复杂实现和第三方依赖。
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace TRANSPORT

#endif // RTC_PUSH_TRANSPORT_H
