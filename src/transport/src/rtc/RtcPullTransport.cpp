#include "transport/src/rtc/RtcPullTransport.h"

#include "transport/src/codec/SrsMediaCodec.h"
#include "transport/src/srs/SrsTransportCommon.h"
#include "transport/src/srs/SrsWhipWhepClient.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <sstream>
#include <utility>

#if TRANSPORT_ENABLE_REAL_SRS
#include <nlohmann/json.hpp>
#include <rtc/frameinfo.hpp>
#include <rtc/h264rtpdepacketizer.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/peerconnection.hpp>
#include <rtc/rtpdepacketizer.hpp>
#include <rtc/track.hpp>
#endif

namespace TRANSPORT {

class RtcPullTransport::Impl {
public:
  ~Impl() {
    Close();
  }

  bool Open(const PublishInfo &publish_info,
            const TargetRoomInfo &room_info,
            const std::string &user_id,
            const std::string &endpoint,
            VideoDataCallback video_callback,
            AudioDataCallback audio_callback) {
#if TRANSPORT_ENABLE_REAL_SRS
    //准备运行环境。
    EnsureCurlRuntime();
    EnsureRtcRuntime();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_open) {
      return true;
    }

    //保存参数记录的上下文
    m_publish_info = publish_info;
    m_room_info = room_info;
    m_user_id = user_id;
    m_endpoint = endpoint;
    //video_callback 是一个 funciton可调用对象，不要复制。
    m_video_callback = std::move(video_callback);
    m_audio_callback = std::move(audio_callback);

    //检查解码器是否打开及其异常处理CloseLocked
    if (!m_video_decoder.Open() || !m_audio_decoder.Open(publish_info)) {
      RealSrsDebug("subscriber decoder open failed, user_id=" + user_id);
      CloseLocked();
      return false;
    }

    //注册iceServers，创建peer connection对象
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    m_peer_connection = std::make_shared<rtc::PeerConnection>(config);

    //注册回调函数，用于保存变化后的状态。
    m_peer_connection->onStateChange([this](rtc::PeerConnection::State state) {
      std::lock_guard<std::mutex> lock(m_signal_mutex);
      m_peer_state = state;
    });

    //
    m_peer_connection->onGatheringStateChange(
        [this](rtc::PeerConnection::GatheringState state) {
          if (state != rtc::PeerConnection::GatheringState::Complete) {
            return;
          }

          auto description = m_peer_connection->localDescription();
          {
            std::lock_guard<std::mutex> lock(m_signal_mutex);
            if (description) {
              m_local_sdp = std::string(description.value());
            }
            m_gathering_complete = true;
          }
          m_signal_cv.notify_all();
        });

    if (!SetupTracksLocked()) {
      RealSrsDebug("subscriber track setup failed, user_id=" + user_id);
      CloseLocked();
      return false;
    }

    m_peer_connection->setLocalDescription(rtc::Description::Type::Offer);

    std::string offer_sdp;
    {
      std::unique_lock<std::mutex> signal_lock(m_signal_mutex);
      const bool gathered = m_signal_cv.wait_for(
          signal_lock,
          std::chrono::milliseconds(kGatherTimeoutMs),
          [this]() {
            return m_gathering_complete && !m_local_sdp.empty();
          });
      if (!gathered) {
        RealSrsDebug("WHEP ICE gathering timed out, user_id=" + user_id);
        CloseLocked();
        return false;
      }
      offer_sdp = m_local_sdp;
    }

    RealSrsDebug("POST WHEP endpoint: " + endpoint);
    const HttpResponse response = SendHttpRequest(
        "POST",
        endpoint,
        {{"Content-Type", "application/sdp"}},
        offer_sdp,
        kWhipTimeoutMs);
    std::string answer_sdp = response.body;
    if (response.status_code < 200 || response.status_code >= 300 ||
        answer_sdp.empty()) {
      std::ostringstream message;
      message << "WHEP failed, status=" << response.status_code
              << ", body=" << response.body;
      RealSrsDebug(message.str());
      if (!SubscribeWithNativeSrsApiLocked(offer_sdp, &answer_sdp)) {
        CloseLocked();
        return false;
      }
    } else {
      m_resource_url = ParseResourceUrl(response.headers);
      RealSrsDebug("WHEP answered, resource=" + m_resource_url);
    }

    m_peer_connection->setRemoteDescription(
        rtc::Description(answer_sdp, rtc::Description::Type::Answer));
    m_open = true;
    return true;
#else
    (void)publish_info;
    (void)room_info;
    (void)user_id;
    (void)endpoint;
    (void)video_callback;
    (void)audio_callback;
    return false;
#endif
  }

  void Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    CloseLocked();
  }

  const std::string &ResourceUrl() const {
    return m_resource_url;
  }

private:
  bool SubscribeWithNativeSrsApiLocked(const std::string &offer_sdp,
                                       std::string *answer_sdp) {
#if TRANSPORT_ENABLE_REAL_SRS
    const std::string native_url = StripUrlQueryAndFragment(
        ReplaceRtcMethodPath(m_endpoint, "whep", "play"));
    const std::string host = ExtractHostFromHttpUrl(m_room_info.pull_server_url);
    if (native_url.empty() || host.empty() || answer_sdp == nullptr) {
      return false;
    }

    const std::string app = ResolveSrsApp(m_room_info.srs_app);
    const std::string stream = MakeStreamName(m_room_info.room_id, m_user_id);

    nlohmann::json request_body = {
        {"api", native_url},
        {"streamurl", "webrtc://" + host + "/" + app + "/" + stream},
        {"sdp", offer_sdp}};

    RealSrsDebug("fallback POST native SRS play endpoint: " + native_url);
    const HttpResponse response = SendHttpRequest(
        "POST",
        native_url,
        {{"Content-Type", "application/json"}},
        request_body.dump(),
        kWhipTimeoutMs);
    if (response.status_code < 200 || response.status_code >= 300 ||
        response.body.empty()) {
      std::ostringstream message;
      message << "native SRS play failed, status=" << response.status_code
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
        message << "native SRS play rejected, code=" << code
                << ", body=" << response.body;
        RealSrsDebug(message.str());
        return false;
      }

      *answer_sdp = sdp;
      m_resource_url.clear();
      RealSrsDebug("native SRS play answered, user_id=" + m_user_id);
      return true;
    } catch (const std::exception &error) {
      RealSrsDebug(std::string("native SRS play response parse failed: ") +
                   error.what());
      return false;
    }
#else
    (void)offer_sdp;
    (void)answer_sdp;
    return false;
#endif
  }

  bool SetupTracksLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
    if (!m_peer_connection) {
      return false;
    }

    rtc::Description::Video video("video",
                                  rtc::Description::Direction::RecvOnly);
    video.addH264Codec(
        kRtpPayloadTypeH264,
        "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1;max-mbps=108000;max-fs=3600");
    m_video_track = m_peer_connection->addTrack(video);
    if (m_video_track) {
      m_video_track->setMediaHandler(
          std::make_shared<rtc::H264RtpDepacketizer>());
      const std::weak_ptr<rtc::Track> track = m_video_track;
      m_video_track->onOpen([track]() {
        if (auto video_track = track.lock()) {
          video_track->requestKeyframe();
        }
      });
      m_video_track->onFrame(
          [this, track, next_request = std::chrono::steady_clock::time_point{}](
              rtc::binary message, rtc::FrameInfo info) mutable {
            const auto timestamp_us = VideoTimestampUs(info.timestamp);
            const auto *data = reinterpret_cast<const uint8_t *>(message.data());
            if (!ProcessVideo(data, message.size(), timestamp_us)) {
              return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_request) {
              if (auto video_track = track.lock(); video_track && video_track->isOpen()) {
                video_track->requestKeyframe();
                next_request = now + std::chrono::milliseconds(500);
              }
            }
          });
    }

    rtc::Description::Audio audio("audio",
                                  rtc::Description::Direction::RecvOnly);
    audio.addOpusCodec(kRtpPayloadTypeOpus);
    m_audio_track = m_peer_connection->addTrack(audio);
    if (m_audio_track) {
      m_audio_track->setMediaHandler(
          std::make_shared<rtc::RtpDepacketizer>(kOpusSampleRate));
      m_audio_track->onFrame([this](rtc::binary message,
                                    rtc::FrameInfo info) {
        const auto timestamp_us = AudioTimestampUs(info.timestamp);
        const auto *data = reinterpret_cast<const uint8_t *>(message.data());
        ProcessAudio(data, message.size(), timestamp_us);
      });
    }

    return m_video_track != nullptr && m_audio_track != nullptr;
#else
    return false;
#endif
  }

  int64_t VideoTimestampUs(uint32_t rtp_timestamp) {
#if TRANSPORT_ENABLE_REAL_SRS
    return RtpTimestampUs(rtp_timestamp,
                          rtc::H264RtpPacketizer::ClockRate,
                          &m_video_time_mutex,
                          &m_video_timestamp_initialized,
                          &m_video_base_rtp,
                          &m_video_base_time_us);
#else
    (void)rtp_timestamp;
    return 0;
#endif
  }

  int64_t AudioTimestampUs(uint32_t rtp_timestamp) {
    return RtpTimestampUs(rtp_timestamp,
                          kOpusSampleRate,
                          &m_audio_time_mutex,
                          &m_audio_timestamp_initialized,
                          &m_audio_base_rtp,
                          &m_audio_base_time_us);
  }

  static int64_t RtpTimestampUs(uint32_t rtp_timestamp,
                                uint32_t clock_rate,
                                std::mutex *mutex,
                                bool *initialized,
                                uint32_t *base_rtp,
                                int64_t *base_time_us) {
    const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();

    std::lock_guard<std::mutex> lock(*mutex);
    if (!*initialized) {
      *initialized = true;
      *base_rtp = rtp_timestamp;
      *base_time_us = now_us;
      return now_us;
    }

    const int32_t delta = static_cast<int32_t>(rtp_timestamp - *base_rtp);
    return *base_time_us +
           static_cast<int64_t>(delta) * 1000000LL /
               static_cast<int64_t>(clock_rate);
  }

  // Returns true when the receiver is waiting for a clean IDR.
  bool ProcessVideo(const uint8_t *data, size_t size, int64_t timestamp_us) {
    if (m_logged_video_packet_count < 3) {
      ++m_logged_video_packet_count;
      RealSrsDebug("remote H264 packet received, user_id=" + m_user_id +
                   ", bytes=" + std::to_string(size));
    }

    std::shared_ptr<I420Frame> frame;
    if (!m_video_decoder.Decode(data, size, timestamp_us, &frame)) {
      if (m_logged_video_decode_fail_count < 5) {
        ++m_logged_video_decode_fail_count;
        RealSrsDebug("remote H264 decode waiting, user_id=" + m_user_id +
                     ", bytes=" + std::to_string(size) +
                     ", needs_keyframe=" +
                     (m_video_decoder.NeedsKeyframe() ? "true" : "false"));
      }
      return m_video_decoder.NeedsKeyframe();
    }

    frame->user_id = m_user_id;
    if (!m_logged_first_video_frame) {
      m_logged_first_video_frame = true;
      RealSrsDebug("first remote video frame decoded, user_id=" +
                   m_user_id);
    }
    if (m_video_callback) {
      m_video_callback(m_user_id, frame);
    }
    return false;
  }

  void ProcessAudio(const uint8_t *data, size_t size, int64_t timestamp_us) {
    std::shared_ptr<AudioFrame> frame;
    if (!m_audio_decoder.Decode(data, size, timestamp_us, &frame)) {
      return;
    }

    frame->user_id = m_user_id;
    if (!m_logged_first_audio_frame) {
      m_logged_first_audio_frame = true;
      RealSrsDebug("first remote audio frame decoded, user_id=" +
                   m_user_id);
    }
    if (m_audio_callback) {
      m_audio_callback(m_user_id, frame);
    }
  }

  void CloseLocked() {
#if TRANSPORT_ENABLE_REAL_SRS
    if (!m_resource_url.empty() && !m_endpoint.empty()) {
      std::string delete_url = m_resource_url;
      if (!StartsWithHttpScheme(delete_url)) {
        const std::string base = TrimTrailingSlash(m_room_info.pull_server_url);
        if (!base.empty() && delete_url.front() == '/') {
          delete_url = base + delete_url;
        }
      }
      if (StartsWithHttpScheme(delete_url)) {
        SendHttpRequest("DELETE", delete_url, {}, "", kWhipTimeoutMs);
      }
    }

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

    m_video_decoder.Close();
    m_audio_decoder.Close();
    m_resource_url.clear();
    m_endpoint.clear();
    m_local_sdp.clear();
    m_gathering_complete = false;
    m_video_timestamp_initialized = false;
    m_audio_timestamp_initialized = false;
    m_logged_first_video_frame = false;
    m_logged_first_audio_frame = false;
    m_logged_video_packet_count = 0;
    m_logged_video_decode_fail_count = 0;
    m_open = false;
  }

private:
  std::mutex m_mutex;
  std::mutex m_signal_mutex;
  std::condition_variable m_signal_cv;
  PublishInfo m_publish_info;
  TargetRoomInfo m_room_info;
  std::string m_user_id;
  std::string m_endpoint;
  std::string m_resource_url;
#if TRANSPORT_ENABLE_REAL_SRS
  std::shared_ptr<rtc::PeerConnection> m_peer_connection;
  std::shared_ptr<rtc::Track> m_video_track;
  std::shared_ptr<rtc::Track> m_audio_track;
  rtc::PeerConnection::State m_peer_state{rtc::PeerConnection::State::New};
#endif
  H264Decoder m_video_decoder;
  OpusFloatDecoder m_audio_decoder;
  VideoDataCallback m_video_callback;
  AudioDataCallback m_audio_callback;
  bool m_gathering_complete = false;
  bool m_logged_first_video_frame = false;
  bool m_logged_first_audio_frame = false;
  int m_logged_video_packet_count = 0;
  int m_logged_video_decode_fail_count = 0;
  bool m_open = false;
  std::string m_local_sdp;

  std::mutex m_video_time_mutex;
  bool m_video_timestamp_initialized = false;
  uint32_t m_video_base_rtp = 0;
  int64_t m_video_base_time_us = 0;

  std::mutex m_audio_time_mutex;
  bool m_audio_timestamp_initialized = false;
  uint32_t m_audio_base_rtp = 0;
  int64_t m_audio_base_time_us = 0;
};

RtcPullTransport::RtcPullTransport()
    : m_impl(std::make_unique<Impl>()) {}

RtcPullTransport::~RtcPullTransport() = default;

bool RtcPullTransport::Open(const PublishInfo &publish_info,
                            const TargetRoomInfo &room_info,
                            const std::string &user_id,
                            const std::string &endpoint,
                            VideoDataCallback video_callback,
                            AudioDataCallback audio_callback) {
  return m_impl->Open(publish_info,
                      room_info,
                      user_id,
                      endpoint,
                      std::move(video_callback),
                      std::move(audio_callback));
}

void RtcPullTransport::Close() {
  m_impl->Close();
}

const std::string &RtcPullTransport::ResourceUrl() const {
  return m_impl->ResourceUrl();
}

} // namespace TRANSPORT
