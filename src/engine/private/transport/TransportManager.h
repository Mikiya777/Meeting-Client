#ifndef TRANSPORT_MANAGER_H
#define TRANSPORT_MANAGER_H

#include "engine/private/transport/ITransportManager.h"
#include "transport/include/ITransportEngine.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace VCE {

class TransportManager final : public ITransportManager {
public:
  TransportManager() = default;
  ~TransportManager() override;

  Result Init(const TransportPublishConfig &config) override;
  Result Uninit() override;

  Result SetRoomInfo(const TransportRoomInfo &info) override;
  TransportRoomInfo GetRoomInfo() const override;

  Result StartPublishCameraVideo() override;
  Result StopPublishCameraVideo() override;

  Result StartPublishAudio() override;
  Result StopPublishAudio() override;

  Result PushVideoFrame(const std::shared_ptr<I420Frame> &frame) override;
  Result PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) override;

  Result SubscribeUserAV(const std::string &user_id) override;
  Result UnsubscribeUserAV(const std::string &user_id) override;
  Result SubscribeUsersAV(const std::vector<std::string> &user_ids) override;
  Result UnsubscribeUsersAV(const std::vector<std::string> &user_ids) override;

  bool IsPublishingCameraVideo() const override;
  bool IsPublishingAudio() const override;
  bool IsUserSubscribedAV(const std::string &user_id) const override;
  std::vector<std::string> GetSubscribedUsers() const override;

  void SetTransportDataCallback(
      const std::shared_ptr<ITransportDataCallback> &callback) override;

  size_t GetQueuedVideoFrameCount() const;
  size_t GetQueuedAudioFrameCount() const;
  int GetSentVideoFrameCount() const;
  int GetSentAudioFrameCount() const;
  int GetDroppedVideoFrameCount() const;
  int GetDroppedAudioFrameCount() const;
  int64_t GetLastSentVideoTimestampUs() const;
  int64_t GetLastSentAudioTimestampUs() const;
  TransportState GetTransportState() const;

  void InjectRemoteVideoFrame(const std::string &user_id,
                              const std::shared_ptr<I420Frame> &frame);
  void InjectRemoteAudioFrame(const std::string &user_id,
                              const std::shared_ptr<AudioFrame> &frame);

private:
  void StartConsumerThreads();
  void StopConsumerThreads();
  void VideoConsumerThread();
  void AudioConsumerThread();

  Result EnqueueVideoFrame(const std::shared_ptr<I420Frame> &frame);
  Result EnqueueAudioFrame(const std::shared_ptr<AudioFrame> &frame);

  void SendVideoFrame(const std::shared_ptr<I420Frame> &frame);
  void SendAudioFrame(const std::shared_ptr<AudioFrame> &frame);

  std::shared_ptr<TRANSPORT::I420Frame> ConvertToTransportVideoFrame(
      const std::shared_ptr<I420Frame> &frame) const;
  std::shared_ptr<TRANSPORT::AudioFrame> ConvertToTransportAudioFrame(
      const std::shared_ptr<AudioFrame> &frame) const;
  std::shared_ptr<I420Frame> ConvertToVceVideoFrame(
      const std::shared_ptr<TRANSPORT::I420Frame> &frame) const;
  std::shared_ptr<AudioFrame> ConvertToVceAudioFrame(
      const std::shared_ptr<TRANSPORT::AudioFrame> &frame) const;
  static TransportState ConvertTransportState(TRANSPORT::ConnectionState state);

  void SetState(TransportState state);
  void OnEngineVideoFrame(const std::string &user_id,
                          const std::shared_ptr<TRANSPORT::I420Frame> &frame);
  void OnEngineAudioFrame(const std::string &user_id,
                          const std::shared_ptr<TRANSPORT::AudioFrame> &frame);
  void OnEngineConnectionStateChanged(TRANSPORT::ConnectionState state);
  std::shared_ptr<ITransportDataCallback> GetCallback() const;

private:
  std::unique_ptr<TRANSPORT::ITransportEngine> m_transport_engine;

  mutable std::mutex m_state_mutex;
  TransportPublishConfig m_publish_config;
  TransportRoomInfo m_room_info;
  TransportState m_state{TransportState::kDisconnected};
  bool m_initialized = false;
  bool m_camera_video_publishing = false;
  bool m_audio_publishing = false;
  std::unordered_set<std::string> m_subscribed_users;

  mutable std::mutex m_callback_mutex;
  std::weak_ptr<ITransportDataCallback> m_callback;

  mutable std::mutex m_video_mutex;
  std::queue<std::shared_ptr<I420Frame>> m_video_queue;
  std::condition_variable m_video_condition;

  mutable std::mutex m_audio_mutex;
  std::queue<std::shared_ptr<AudioFrame>> m_audio_queue;
  std::condition_variable m_audio_condition;

  // Read with the stream's queue mutex held. Change with both mutexes held
  // so neither consumer can miss the stop notification.
  bool m_running = false;
  std::thread m_video_consumer_thread;
  std::thread m_audio_consumer_thread;

  std::atomic<int> m_sent_video_frame_count{0};
  std::atomic<int> m_sent_audio_frame_count{0};
  std::atomic<int> m_dropped_video_frame_count{0};
  std::atomic<int> m_dropped_audio_frame_count{0};
  std::atomic<int64_t> m_last_sent_video_timestamp_us{0};
  std::atomic<int64_t> m_last_sent_audio_timestamp_us{0};
};

} // namespace VCE

#endif // TRANSPORT_MANAGER_H
