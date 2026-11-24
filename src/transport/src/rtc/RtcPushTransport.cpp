#include "transport/src/rtc/RtcPushTransport.h"

#include "transport/src/codec/SrsMediaCodec.h"
#include "transport/src/srs/SrsTransportCommon.h"
#include "transport/src/srs/SrsWhipWhepClient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <sstream>
#include <vector>

#if TRANSPORT_ENABLE_REAL_SRS
#include <nlohmann/json.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/plihandler.hpp>
#include <rtc/rtcpnackresponder.hpp>
#include <rtc/rtcpsrreporter.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/track.hpp>
#endif

namespace TRANSPORT {

//PImpl 风格
class RtcPushTransport::Impl {
public:
  ~Impl() {
    Close();
  }

  //创建推流资源
  /*
    接收三个参数：
    1.
    2.
    3.endpoint：由服务端返回，记录Srs服务器地址。
  */
  bool Open(const PublishInfo &publish_info,
            const TargetRoomInfo &room_info,
            const std::string &endpoint) {
#if TRANSPORT_ENABLE_REAL_SRS
    /*
    准备运行库
    curl：负责HTTP请求，WHIP POST
    rtc：负责WebRTC PeerConnection / Track / SDP 
    */
    EnsureCurlRuntime();
    EnsureRtcRuntime();

    //存在数据竞争，上锁保护
    std::lock_guard<std::mutex> lock(m_mutex);
    //重复初始化，直接退出
    if (m_open) {
      return true;
    }

    /*
    保存推流上下文 
    publish_info：本地音视频参数，比如宽高、采样率、通道数。
    room_info：房间、用户、SRS app/secret/server url。
    endpoint：本次 WHIP 协商地址。
    */
    m_publish_info = publish_info;
    m_room_info = room_info;
    m_endpoint = endpoint;

    //打开编码器，失败返回CloseLocked （半初始化清理）
    if (!m_video_encoder.Open(publish_info) ||
        !m_audio_encoder.Open(publish_info)) {
      RealSrsDebug("encoder open failed");
      CloseLocked();
      return false;
    }

    /*
    创建PeerConnection
    准备配置对象 rtc::Configuration config
    iceServers:告诉WebRTC可以用哪些STUN/TURN服务器辅助建立连接。
    STUN的作用是帮助客户端发现自己在公网/NAT后的可连接地址。
    PeerConneciton对象负责：
    SDP 协商
    ICE 连接
    DTLS 加密
    SRTP 媒体传输
    Track 管理
    */
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    //使用此配置创建PeerConnection 连接总对象
    m_peer_connection = std::make_shared<rtc::PeerConnection>(config);

    //注册 PeerConnection 状态回调 ：当连接状态变化时，把外部记录的最新状态保存下来。
    m_peer_connection->onStateChange([this](rtc::PeerConnection::State state) {
      //数据竞争，上锁保护，修改自身状态。
      std::lock_guard<std::mutex> lock(m_signal_mutex);
      m_peer_state = state;
    });

    //注册 ICE gathering 完成回调 （异步过程通知端）：拿到完整 SDP Offer
    m_peer_connection->onGatheringStateChange(
        [this](rtc::PeerConnection::GatheringState state) {
          if (state != rtc::PeerConnection::GatheringState::Complete) {
            return;
          }

          //获取localDescription，然后用m_local_sdp记录
          auto description = m_peer_connection->localDescription();
          {
            std::lock_guard<std::mutex> lock(m_signal_mutex);
            if (description) {
              m_local_sdp = std::string(description.value());
            }
            m_gathering_complete = true;
          }
          //m_signal_cv.notify_all();的用法是什么？
          m_signal_cv.notify_all();
        });

    //创建 sendonly 音视频 Track，具体过程委托SetupTracksLocked辅助函数实现了。
    if (!SetupTracksLocked()) {
      RealSrsDebug("track setup failed");
      CloseLocked();
      return false;
    }

    //触发生成 SDP Offer，告诉 PeerConnection：请生成一个本地 SDP Offer。
    m_peer_connection->setLocalDescription(rtc::Description::Type::Offer);

    //等待完整 Offer SDP m_gathering_complete == true 并且 m_local_sdp 不为空
    //那么这一段完成也就意味着SDP offer 生成完毕。
    std::string offer_sdp;
    {
      //使用unique_lock 配合条件变量完成异步过程
      std::unique_lock<std::mutex> signal_lock(m_signal_mutex);
      //Open 会在这里等待 SDP，直到回调通知或超时。（异步过程等待端），检查predicate。
      const bool gathered = m_signal_cv.wait_for(
          signal_lock,
          std::chrono::milliseconds(kGatherTimeoutMs), //状态模型：超时失败。
          [this]() {
            return m_gathering_complete && !m_local_sdp.empty();
          });
      if (!gathered) {
        //RealSrsDebug，在开启 debug 时打印 SRS transport 日志。它不影响协商结果。
        RealSrsDebug("ICE gathering timed out before local SDP was ready");
        CloseLocked();
        return false;
      }
      offer_sdp = m_local_sdp;
    }

    // WHIP POST 给 SRS ,用 HTTP 把 WebRTC SDP Offer 发给 SRS。
    RealSrsDebug("POST WHIP endpoint: " + endpoint);
    HttpResponse response = SendHttpRequest(
        "POST",
        endpoint,
        {{"Content-Type", "application/sdp"}},
        offer_sdp,
        kWhipTimeoutMs);

    //处理处理 SRS 返回的 WHIP Answer 响应，成功和失败都有
    std::string answer_sdp = response.body;
    if (response.status_code < 200 || response.status_code >= 300 ||
        answer_sdp.empty()) {
      std::ostringstream message;
      message << "WHIP failed, status=" << response.status_code
              << ", body=" << response.body;
      //这个API是做什么的？
      RealSrsDebug(message.str());
      //备用线路
      if (!PublishWithNativeSrsApiLocked(offer_sdp, &answer_sdp)) {
        CloseLocked();
        return false;
      }
    } else {
      m_resource_url = ParseResourceUrl(response.headers);
      RealSrsDebug("WHIP answered, resource=" + m_resource_url);
    }

    //设置远端 SDP Answer
    m_peer_connection->setRemoteDescription(
        rtc::Description(answer_sdp, rtc::Description::Type::Answer));
    m_open = true;
    return true;
#else
    (void)publish_info;
    (void)room_info;
    (void)endpoint;
    return false;
#endif
  }

  void Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    CloseLocked();
  }

  bool PushVideoFrame(const std::shared_ptr<I420Frame> &frame) {
#if TRANSPORT_ENABLE_REAL_SRS
    //检查帧是否合法
    if (!frame || !frame->IsValid()) {
      return false;
    }

    //I420 原始帧 -> H264 编码数据。
    std::vector<uint8_t> encoded;
    if (!m_video_encoder.Encode(frame, &encoded)) {
      return false;
    }

    // 编码器可能因为缓存/等待关键帧等原因暂时没有输出
    if (encoded.empty()) {
      return true;
    }

    //检查推流会话和 video track 是否可用。
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_open || !m_video_track || !m_video_track->isOpen()) {
      return false;
    }

    //设置当前帧的 RTP 元信息：payload type + 媒体时间戳。这帧 payload type 是 H264,这帧在本次推流会话的媒体时间轴上处于什么位置,服务于接收端端排序，播放节奏控制，音视频同步
    rtc::FrameInfo info(0u);
    info.payloadType = kRtpPayloadTypeH264;
    //告诉WebRTC这帧在媒体时间轴上的位置。
    info.timestampSeconds =
        std::chrono::duration<double>(GetElapsedSeconds(frame->timestamp_us));
    // 把编码后的 H264 数据交给 video track；track 上的 H264RtpPacketizer
    // 会把它封装成 RTP 包，再由 WebRTC 发送到 SRS。
    m_video_track->sendFrame(
        reinterpret_cast<const std::byte *>(encoded.data()),
        encoded.size(),
        info);
    if (!m_logged_first_video_frame) {
      m_logged_first_video_frame = true;
      RealSrsDebug("first H264 video frame sent, bytes=" +
                   std::to_string(encoded.size()));
    }
    return true;
#else
    (void)frame;
    return false;
#endif
  }

  bool PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) {
#if TRANSPORT_ENABLE_REAL_SRS
    if (!frame || !frame->IsValid()) {
      return false;
    }

    //Audio 原始 PCM帧->Opus帧
    // 一个输入 AudioFrame 可能编码出多个 Opus packet。
    std::vector<std::vector<uint8_t>> encoded_frames;
    if (!m_audio_encoder.Encode(frame, &encoded_frames)) {
      return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_open || !m_audio_track || !m_audio_track->isOpen()) {
      return false;
    }
    //逐个把 Opus 包交给 audio track。OpusRtpPacketizer 会封包 RTP，
    // WebRTC 负责发送到 SRS。
    for (const auto &encoded : encoded_frames) {
      if (encoded.empty()) {
        continue;
      }
      rtc::FrameInfo info(0u);
      info.payloadType = kRtpPayloadTypeOpus;
      info.timestampSeconds =
          std::chrono::duration<double>(GetElapsedSeconds(frame->timestamp_us));
      m_audio_track->sendFrame(
          reinterpret_cast<const std::byte *>(encoded.data()),
          encoded.size(),
          info);
      if (!m_logged_first_audio_frame) {
        m_logged_first_audio_frame = true;
        RealSrsDebug("first Opus audio frame sent, bytes=" +
                     std::to_string(encoded.size()));
      }
    }
    return true;
#else
    (void)frame;
    return false;
#endif
  }

  const std::string &ResourceUrl() const {
    return m_resource_url;
  }

private:
  bool PublishWithNativeSrsApiLocked(const std::string &offer_sdp,
                                     std::string *answer_sdp) {
#if TRANSPORT_ENABLE_REAL_SRS
    const std::string native_url = StripUrlQueryAndFragment(
        ReplaceRtcMethodPath(m_endpoint, "whip", "publish"));
    const std::string host = ExtractHostFromHttpUrl(m_room_info.push_server_url);
    if (native_url.empty() || host.empty() || answer_sdp == nullptr) {
      return false;
    }

    const std::string app = ResolveSrsApp(m_room_info.srs_app);
    const std::string stream = MakeStreamName(m_room_info.room_id,
                                              m_room_info.local_user_id);

    nlohmann::json request_body = {
        {"api", native_url},
        {"streamurl", "webrtc://" + host + "/" + app + "/" + stream},
        {"sdp", offer_sdp}};

    RealSrsDebug("fallback POST native SRS publish endpoint: " + native_url);
    const HttpResponse response = SendHttpRequest(
        "POST",
        native_url,
        {{"Content-Type", "application/json"}},
        request_body.dump(),
        kWhipTimeoutMs);
    if (response.status_code < 200 || response.status_code >= 300 ||
        response.body.empty()) {
      std::ostringstream message;
      message << "native SRS publish failed, status=" << response.status_code
              << ", body=" << response.body;
      RealSrsDebug(message.str());
      return false;
    }

    try {
      const auto response_json = nlohmann::json::parse(response.body);
      const int code = response_json.value("code", -1);
      const std::string sdp = response_json.value("sdp", "");
      if (code != 0 || sdp.empty()) {
        std::ostringstream message;
        message << "native SRS publish rejected, code=" << code
                << ", body=" << response.body;
        RealSrsDebug(message.str());
        return false;
      }

      *answer_sdp = sdp;
      m_resource_url.clear();
      RealSrsDebug("native SRS publish answered");
      return true;
    } catch (const std::exception &error) {
      RealSrsDebug(std::string("native SRS response parse failed: ") +
                   error.what());
      return false;
    }
#else
    (void)offer_sdp;
    (void)answer_sdp;
    return false;
#endif
  }

  /*
  在生成 SDP Offer 之前，客户端如何告诉 WebRTC：我要发送一路 H264 视频和一路 Opus 音频？
    创建 sendonly 音视频 Track
    video track：sendonly，H264，payload type 96
    audio track：sendonly，Opus，payload type 111
    配置：
    H264RtpPacketizer
    OpusRtpPacketizer
    RtcpSrReporter
    RtcpNackResponder
    PliHandler
    */
  bool SetupTracksLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
    //防御性检查
    if (!m_peer_connection) {
      return false;
    }

    //cname：发送源名字
    const std::string cname =
        m_room_info.room_id + "_" + m_room_info.local_user_id;

    //创建视频 Track 描述对象Description:告诉 SRS 我要发什么。
    rtc::Description::Video video("video",
                                  rtc::Description::Direction::SendOnly);
    video.addH264Codec(
        kRtpPayloadTypeH264, //RTP payload type
        "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1;max-mbps=108000;max-fs=3600");

    //SSRC RTP的流ID :这一路视频 RTP 包的身份标识。
    const uint32_t video_ssrc = GenerateRandomSsrc();
    //往Track描述对象里追加信息：流id，发送源名称，后面两个GenerateId不懂是什么东西
    video.addSSRC(video_ssrc, cname, GenerateId(), GenerateId());

    //使用描述对象video创建Track
    m_video_track = m_peer_connection->addTrack(video);

    //video_packetizer的配置对象：包含ssrc,cname,payload type，和clock rate。
    //RTP clock rate 是 RTP 时间戳的计时单位，RTP 包里有一个 timestamp 字段，用来表示这个媒体包在媒体时间轴上的位置。
    //这个 timestamp 不是用“微秒/毫秒”直接表示，而是用某个 clock rate 的 tick 表示。
    auto video_rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
        video_ssrc,
        cname,
        kRtpPayloadTypeH264,
        rtc::H264RtpPacketizer::ClockRate);

    //使用此配置创建一个video_packetizer 封包器 ：编码后的 H264 数据 -> RTP 包
    auto video_packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::H264RtpPacketizer::Separator::LongStartSequence,
        video_rtp_config);

    //给RTP封包器加 RTCP 处理链:处理发送报告、丢包反馈、关键帧请求等质量控制。
    video_packetizer->addToChain(
        std::make_shared<rtc::RtcpSrReporter>(video_rtp_config));
    video_packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    video_packetizer->addToChain(std::make_shared<rtc::PliHandler>(
        [this]() {
          m_video_encoder.RequestKeyframe();
        }));
    m_video_track->setMediaHandler(video_packetizer);

    //同样的配置一套音频的
    rtc::Description::Audio audio("audio",
                                  rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(kRtpPayloadTypeOpus);
    const uint32_t audio_ssrc = GenerateRandomSsrc();

    //GenerateId 给 RTP/SDP 里这一路媒体源生成额外标识，辅助标识 track/stream。
    audio.addSSRC(audio_ssrc, cname, GenerateId(), GenerateId());
    m_audio_track = m_peer_connection->addTrack(audio);

    auto audio_rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(
        audio_ssrc,
        cname,
        kRtpPayloadTypeOpus,
        rtc::OpusRtpPacketizer::DefaultClockRate);
    auto audio_packetizer =
        std::make_shared<rtc::OpusRtpPacketizer>(audio_rtp_config);
    audio_packetizer->addToChain(
        std::make_shared<rtc::RtcpSrReporter>(audio_rtp_config));
    audio_packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    m_audio_track->setMediaHandler(audio_packetizer);
    return true;
#else
    return false;
#endif
  }

  //把原始帧 timestamp_us 转成“从本次推流会话开始算起的秒数”：也就是相对时间，因为WebRTC/RTP 发送时更关心这帧距离本次媒体会话开始过了多久？
  double GetElapsedSeconds(int64_t timestamp_us) {
    uint64_t expected = 0;
    //如果 m_session_start_us 现在还是 0，就把它设置为当前帧 timestamp_us:第一帧负责确定本次推流会话的时间基准。
    m_session_start_us.compare_exchange_strong(
        expected, static_cast<uint64_t>(std::max<int64_t>(0, timestamp_us))); //max(0, timestamp_us)，如果传入负时间戳，按 0 处理。

    const auto current =
        static_cast<uint64_t>(std::max<int64_t>(0, timestamp_us));
    const uint64_t base = m_session_start_us.load();
    return current >= base
               ? static_cast<double>(current - base) / 1000000.0
               : 0.0;
  }

  // C++ 常见写法：Public Close() 负责加锁，Private CloseLocked()
  // 假设调用方已经持有 m_mutex，避免重复加锁。
  // WHIP 成功时 SRS 可能返回 resource url，它表示本次推流会话
  // 在 SRS 侧的资源标识。关闭时通过 DELETE 尽力释放该远端资源。
  void CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
    //检查resource_url和endpoint 非空
    if (!m_resource_url.empty() && !m_endpoint.empty()) {
      std::string delete_url = m_resource_url;

      //拼接完整DELETE URL，因为有时 SRS 返回的是相对路径
      if (!StartsWithHttpScheme(delete_url)) {
        const std::string base = TrimTrailingSlash(m_room_info.push_server_url);
        if (!base.empty() && delete_url.front() == '/') {
          delete_url = base + delete_url;
        }
      }
      // Close 阶段 best-effort 删除远端资源，不阻断本地清理。
      if (StartsWithHttpScheme(delete_url)) {
        SendHttpRequest("DELETE", delete_url, {}, "", kWhipTimeoutMs);
      }
    }

    // 先关闭挂在 PeerConnection 上的音视频 Track，再关闭 PeerConnection 本身。
    if (m_video_track) {
      m_video_track->close();
      m_video_track.reset();
    }
    if (m_audio_track) {
      m_audio_track->close();
      m_audio_track.reset();
    }
    if (m_peer_connection) {
      m_peer_connection->close();
      m_peer_connection.reset();
    }
#endif

    //关闭编码器
    m_video_encoder.Close();
    m_audio_encoder.Close();
    //清理会话状态
    m_session_start_us = 0;
    m_resource_url.clear();
    m_endpoint.clear();
    m_local_sdp.clear();
    m_gathering_complete = false;
    m_logged_first_video_frame = false;
    m_logged_first_audio_frame = false;
    m_open = false;
  }

private:
  std::mutex m_mutex;
  std::mutex m_signal_mutex;
  std::condition_variable m_signal_cv;
  PublishInfo m_publish_info;
  TargetRoomInfo m_room_info;
  std::string m_endpoint;
  std::string m_resource_url;
#if TRANSPORT_ENABLE_REAL_SRS
  std::shared_ptr<rtc::PeerConnection> m_peer_connection;
  std::shared_ptr<rtc::Track> m_video_track;
  std::shared_ptr<rtc::Track> m_audio_track;
  rtc::PeerConnection::State m_peer_state{rtc::PeerConnection::State::New};
#endif
  H264Encoder m_video_encoder;
  OpusFloatEncoder m_audio_encoder;
  std::atomic<uint64_t> m_session_start_us{0}; //视频帧和音频帧可能来自不同线程
  bool m_gathering_complete = false;
  bool m_logged_first_video_frame = false;
  bool m_logged_first_audio_frame = false;
  bool m_open = false;
  std::string m_local_sdp;
};

RtcPushTransport::RtcPushTransport()
    : m_impl(std::make_unique<Impl>()) {}

RtcPushTransport::~RtcPushTransport() = default;

bool RtcPushTransport::Open(const PublishInfo &publish_info,
                            const TargetRoomInfo &room_info,
                            const std::string &endpoint) {
  return m_impl->Open(publish_info, room_info, endpoint);
}

void RtcPushTransport::Close() {
  m_impl->Close();
}

bool RtcPushTransport::PushVideoFrame(
    const std::shared_ptr<I420Frame> &frame) {
  return m_impl->PushVideoFrame(frame);
}

bool RtcPushTransport::PushAudioFrame(
    const std::shared_ptr<AudioFrame> &frame) {
  return m_impl->PushAudioFrame(frame);
}

const std::string &RtcPushTransport::ResourceUrl() const {
  return m_impl->ResourceUrl();
}

} // namespace TRANSPORT
