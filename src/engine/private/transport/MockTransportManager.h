#ifndef MOCKTRANSPORTMANAGER_H
#define MOCKTRANSPORTMANAGER_H

#include "engine/private/transport/ITransportManager.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace VCE {
class MockTransportManager final : public ITransportManager {
public:
  //实体生命控制
  MockTransportManager() = default;
  virtual ~MockTransportManager() override = default;

  //接口方法
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

  void SetTransportDataCallback(const std::shared_ptr<ITransportDataCallback> &callback) override;

  //测试用
  int GetPushedVideoFrameCount() const;
  int GetPushedAudioFrameCount() const;

  //专用方法
  void SimulateRemoteVideoFrame(const std::string &user_id,
                                const std::shared_ptr<I420Frame> &frame);
  void SimulateRemoteAudioFrame(const std::string &user_id,
                                const std::shared_ptr<AudioFrame> &frame);
  void SimulateConnectionStateChanged(TransportState state);

private:
  TransportPublishConfig m_publish_config;
  TransportRoomInfo m_room_info;
  bool m_initialized = false;
  bool m_camera_video_publishing = false;
  bool m_audio_publishing = false;
  int m_pushed_video_frame_count = 0;
  int m_pushed_audio_frame_count = 0;
  std::unordered_set<std::string> m_subscribed_users;
  std::weak_ptr<ITransportDataCallback> m_callback;
  //在const专有函数里使用锁
  mutable std::mutex m_mutex;
};
} // namespace VCE
#endif //MOCKTRANSPORTMANAGER_H
