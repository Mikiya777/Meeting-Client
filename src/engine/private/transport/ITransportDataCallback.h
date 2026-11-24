#ifndef ITRANSPORTDATACALLBACK_H
#define ITRANSPORTDATACALLBACK_H

#include "engine/public/VceTypes.h"

#include <memory>
#include <string>
namespace VCE {
//回调函数接口
class ITransportDataCallback {
public:
  virtual ~ITransportDataCallback() = default;
  //On：当……发生时
  //谁传来的音视频，具体什么音视频 （参数列表）
  virtual void OnTransportVideoFrame(const std::string &user_id,
                                     const std::shared_ptr<I420Frame> &frame) = 0;
  virtual void OnTransportAudioFrame(const std::string &user_id,
                                     const std::shared_ptr<AudioFrame> &frame) = 0;
  virtual void OnTransportConnectionStateChanged(TransportState state) = 0;
};
} // namespace VCE
#endif //ITRANSPORTDATACALLBACK_H
