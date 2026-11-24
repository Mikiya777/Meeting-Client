#ifndef I_MEETING_EVENT_OBSERVER_H
#define I_MEETING_EVENT_OBSERVER_H

#include "engine/public/VceTypes.h"

#include <string>

namespace VCE {

// UI 或其他上层对象通过实现该接口接收 Engine 事件。
// Engine 只认识这个抽象接口，不依赖具体 Qt 窗口。
class IMeetingEventObserver {
public:
  virtual ~IMeetingEventObserver() = default;

  // Engine 生命周期发生变化。
  virtual void OnEngineStateChanged(EngineState state) = 0;

  // 本地麦克风状态发生变化。
  virtual void OnMicrophoneStateChanged(bool enabled) = 0;

  // 本地摄像头状态发生变化。
  virtual void OnCameraStateChanged(bool enabled) = 0;

  // 传输模块状态发生变化
  virtual void OnTransportConnectionStateChanged(bool connected) = 0;

  // 某个用户有新视频帧进入渲染缓存。
  virtual void OnRenderVideoFrameReady(const std::string &user_id) = 0;

  //会议成员加入
  virtual void OnMeetingUserJoined(const std::string &user_id,
                                   const std::string &username) = 0;

  //会议成员退出
  virtual void OnMeetingUserLeft(const std::string &user_id) = 0;

  //会议终止
  virtual void OnMeetingEnded() = 0;
};

} // namespace VCE

#endif // I_MEETING_EVENT_OBSERVER_H
