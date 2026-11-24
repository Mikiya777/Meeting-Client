#ifndef I_TRANSPORT_ENGINE_H
#define I_TRANSPORT_ENGINE_H

#include "transport/include/TransportDefine.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace TRANSPORT {

using VideoDataCallback =
    std::function<void(const std::string &user_id,
                       const std::shared_ptr<I420Frame> &frame)>;
using AudioDataCallback =
    std::function<void(const std::string &user_id,
                       const std::shared_ptr<AudioFrame> &frame)>;
using ConnectionStateCallback = std::function<void(ConnectionState state)>;

class ITransportEngine {
public:
  virtual ~ITransportEngine() = default;

  //静态工厂函数
  static std::unique_ptr<ITransportEngine> Create();

  //生命周期
  virtual bool Initialize(const PublishInfo &publish_info) = 0;
  virtual void Uninit() = 0;
  virtual bool IsInitialized() const = 0;

  //会议室配置
  virtual void SetTargetRoomInfo(const TargetRoomInfo &info) = 0;
  virtual TargetRoomInfo GetTargetRoomInfo() const = 0;

  //推流
  virtual bool StartPublishVideo() = 0;
  virtual bool StopPublishVideo() = 0;
  virtual bool StartPublishAudio() = 0;
  virtual bool StopPublishAudio() = 0;

  virtual bool PushVideoFrame(const std::shared_ptr<I420Frame> &frame) = 0;
  virtual bool PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) = 0;

  virtual bool IsPublishingVideo() const = 0;
  virtual bool IsPublishingAudio() const = 0;

  //拉流
  virtual bool SubscribeUserAV(const std::string &user_id) = 0;
  virtual bool UnsubscribeUserAV(const std::string &user_id) = 0;
  virtual bool IsUserSubscribedAV(const std::string &user_id) const = 0;
  virtual std::vector<std::string> GetSubscribedUsers() const = 0;

  //回调
  virtual void RegisterVideoCallback(VideoDataCallback callback) = 0;
  virtual void RegisterAudioCallback(AudioDataCallback callback) = 0;
  virtual void RegisterConnectionStateCallback(ConnectionStateCallback callback) = 0;
};

} // namespace TRANSPORT

#endif // I_TRANSPORT_ENGINE_H
