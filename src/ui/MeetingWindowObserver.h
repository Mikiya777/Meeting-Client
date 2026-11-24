#ifndef MEETING_WINDOW_OBSERVER_H
#define MEETING_WINDOW_OBSERVER_H

#include "engine/public/IMeetingEventObserver.h"

#include <functional>
#include <memory>

#include <string>

class MeetingWindow;

// Engine 是纯 C++ 模块，不应该直接依赖 QWidget。
// 这个桥接类负责把 Engine 回调转换为 MeetingWindow 能处理的 UI 事件。
class MeetingWindowObserver final
    : public VCE::IMeetingEventObserver {
public:
  explicit MeetingWindowObserver(MeetingWindow *window);

  // Called on the window thread before its teardown.
  void Detach();

  void OnEngineStateChanged(VCE::EngineState state) override;
  void OnMicrophoneStateChanged(bool enabled) override;
  void OnCameraStateChanged(bool enabled) override;
  void OnTransportConnectionStateChanged(bool connected) override;
  void OnRenderVideoFrameReady(const std::string &user_id) override;
  void OnMeetingUserJoined(const std::string &user_id,
                           const std::string &username) override;
  void OnMeetingUserLeft(const std::string &user_id) override;
  void OnMeetingEnded() override;

private:
  struct DispatchState;
  void Post(std::function<void(MeetingWindow &)> task);
  const std::shared_ptr<DispatchState> m_dispatch;
};

#endif // MEETING_WINDOW_OBSERVER_H
