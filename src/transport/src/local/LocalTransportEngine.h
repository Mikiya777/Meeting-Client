#ifndef LOCAL_TRANSPORT_ENGINE_H
#define LOCAL_TRANSPORT_ENGINE_H

#include "transport/include/ITransportEngine.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace TRANSPORT {

class LocalTransportEngine final : public ITransportEngine {
public:
  LocalTransportEngine() = default;
  ~LocalTransportEngine() override;

  //生命周期
  bool Initialize(const PublishInfo &publish_info) override;
  void Uninit() override;
  bool IsInitialized() const override;

  //房间信息
  void SetTargetRoomInfo(const TargetRoomInfo &info) override;
  TargetRoomInfo GetTargetRoomInfo() const override;

  //推流
  bool StartPublishVideo() override;
  bool StopPublishVideo() override;
  bool StartPublishAudio() override;
  bool StopPublishAudio() override;

  bool PushVideoFrame(const std::shared_ptr<I420Frame> &frame) override;
  bool PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) override;

  bool IsPublishingVideo() const override;
  bool IsPublishingAudio() const override;

  //拉流
  bool SubscribeUserAV(const std::string &user_id) override;
  bool UnsubscribeUserAV(const std::string &user_id) override;
  bool IsUserSubscribedAV(const std::string &user_id) const override;
  std::vector<std::string> GetSubscribedUsers() const override;

  //回调
  void RegisterVideoCallback(VideoDataCallback callback) override;
  void RegisterAudioCallback(AudioDataCallback callback) override;
  void RegisterConnectionStateCallback(ConnectionStateCallback callback) override;

  //测试辅助
  int GetPushedVideoFrameCount() const;
  int GetPushedAudioFrameCount() const;
  int64_t GetLastPushedVideoTimestampUs() const;
  int64_t GetLastPushedAudioTimestampUs() const;

  void InjectRemoteVideoFrame(const std::string &user_id,
                              const std::shared_ptr<I420Frame> &frame);
  void InjectRemoteAudioFrame(const std::string &user_id,
                              const std::shared_ptr<AudioFrame> &frame);

private:
  void SetState(ConnectionState state);
  bool ShouldDisconnectLocked() const;

private:
  mutable std::mutex m_mutex;
  PublishInfo m_publish_info;
  TargetRoomInfo m_room_info;
  ConnectionState m_state{ConnectionState::kDisconnected};
  bool m_initialized = false;
  bool m_video_publishing = false;
  bool m_audio_publishing = false;
  std::unordered_set<std::string> m_subscribed_users;
  VideoDataCallback m_video_callback;
  AudioDataCallback m_audio_callback;
  ConnectionStateCallback m_connection_state_callback;

  std::atomic<int> m_pushed_video_frame_count{0};
  std::atomic<int> m_pushed_audio_frame_count{0};
  std::atomic<int64_t> m_last_pushed_video_timestamp_us{0};
  std::atomic<int64_t> m_last_pushed_audio_timestamp_us{0};
};

} // namespace TRANSPORT

#endif // LOCAL_TRANSPORT_ENGINE_H
