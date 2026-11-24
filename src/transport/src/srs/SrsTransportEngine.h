#ifndef SRS_TRANSPORT_ENGINE_H
#define SRS_TRANSPORT_ENGINE_H

#include "transport/include/ITransportEngine.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace TRANSPORT {

class RtcPullTransport;
class RtcPushTransport;

class SrsTransportEngine final : public ITransportEngine {
public:
  SrsTransportEngine();
  ~SrsTransportEngine() override;

  bool Initialize(const PublishInfo &publish_info) override;
  void Uninit() override;
  bool IsInitialized() const override;

  void SetTargetRoomInfo(const TargetRoomInfo &info) override;
  TargetRoomInfo GetTargetRoomInfo() const override;

  bool StartPublishVideo() override;
  bool StopPublishVideo() override;
  bool StartPublishAudio() override;
  bool StopPublishAudio() override;

  bool PushVideoFrame(const std::shared_ptr<I420Frame> &frame) override;
  bool PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) override;

  bool IsPublishingVideo() const override;
  bool IsPublishingAudio() const override;

  bool SubscribeUserAV(const std::string &user_id) override;
  bool UnsubscribeUserAV(const std::string &user_id) override;
  bool IsUserSubscribedAV(const std::string &user_id) const override;
  std::vector<std::string> GetSubscribedUsers() const override;

  void RegisterVideoCallback(VideoDataCallback callback) override;
  void RegisterAudioCallback(AudioDataCallback callback) override;
  void RegisterConnectionStateCallback(ConnectionStateCallback callback) override;

  std::string GetLocalStreamName() const;
  std::string GetPublishEndpoint() const;
  std::string GetPullEndpoint(const std::string &user_id) const;
  int GetPushedVideoFrameCount() const;
  int GetPushedAudioFrameCount() const;
  int64_t GetLastPushedVideoTimestampUs() const;
  int64_t GetLastPushedAudioTimestampUs() const;

private:
  struct PullSession {
    std::string user_id;
    std::string endpoint;
    std::string resource_url;
  };

  bool HasValidRoomInfoLocked() const;
  bool HasAnyActiveSessionLocked() const;
  bool EnsurePublishSessionLocked();
  void ClosePublishSessionLocked();
  bool RealWebRtcEnabledLocked() const;
  std::string BuildPublishEndpointLocked() const;
  std::string BuildPullEndpointLocked(const std::string &user_id) const;
  ConnectionStateCallback SetStateLocked(ConnectionState state);
  void SetState(ConnectionState state);

private:
  mutable std::mutex m_mutex;
  PublishInfo m_publish_info;
  TargetRoomInfo m_room_info;
  ConnectionState m_state{ConnectionState::kDisconnected};
  bool m_initialized = false;
  bool m_video_publishing = false;
  bool m_audio_publishing = false;
  std::string m_publish_endpoint;
  std::string m_publish_resource_url;
  std::unique_ptr<RtcPushTransport> m_real_publisher;
  std::unordered_map<std::string, std::unique_ptr<RtcPullTransport>>
      m_real_subscribers;
  std::unordered_map<std::string, PullSession> m_pull_sessions;
  VideoDataCallback m_video_callback;
  AudioDataCallback m_audio_callback;
  ConnectionStateCallback m_connection_state_callback;

  std::atomic<int> m_pushed_video_frame_count{0};
  std::atomic<int> m_pushed_audio_frame_count{0};
  std::atomic<int64_t> m_last_pushed_video_timestamp_us{0};
  std::atomic<int64_t> m_last_pushed_audio_timestamp_us{0};
};

} // namespace TRANSPORT

#endif // SRS_TRANSPORT_ENGINE_H
