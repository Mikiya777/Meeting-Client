#ifndef ITRANSPORTMANAGER_H
#define ITRANSPORTMANAGER_H

#include <memory>
#include <string>
#include <vector>
#include "engine/private/transport/ITransportDataCallback.h"
#include "engine/public/VceTypes.h"

namespace VCE {
class ITransportManager {
public:
  virtual ~ITransportManager() = default;

  virtual Result Init(const TransportPublishConfig &config) = 0;
  virtual Result Uninit() = 0;

  virtual Result SetRoomInfo(const TransportRoomInfo &info) = 0;
  virtual TransportRoomInfo GetRoomInfo() const = 0;

  virtual Result StartPublishCameraVideo() = 0;
  virtual Result StopPublishCameraVideo() = 0;

  virtual Result StartPublishAudio() = 0;
  virtual Result StopPublishAudio() = 0;

  virtual Result PushVideoFrame(const std::shared_ptr<I420Frame> &frame) = 0;
  virtual Result PushAudioFrame(const std::shared_ptr<AudioFrame> &frame) = 0;

  virtual Result SubscribeUserAV(const std::string &user_id) = 0;
  virtual Result UnsubscribeUserAV(const std::string &user_id) = 0;
  virtual Result SubscribeUsersAV(const std::vector<std::string> &user_ids) = 0;
  virtual Result UnsubscribeUsersAV(const std::vector<std::string> &user_ids) = 0;

  virtual bool IsPublishingCameraVideo() const = 0;
  virtual bool IsPublishingAudio() const = 0;
  virtual bool IsUserSubscribedAV(const std::string &user_id) const = 0;
  virtual std::vector<std::string> GetSubscribedUsers() const = 0;

  virtual void SetTransportDataCallback(const std::shared_ptr<ITransportDataCallback> &callback) = 0;
};
} // namespace VCE

#endif //ITRANSPORTMANAGER_H
