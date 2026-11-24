#include "ui/MeetingWindowObserver.h"

#include "app/UiEventDispatcher.h"
#include "ui/MeetingWindow.h"

#include <mutex>
#include <utility>

struct MeetingWindowObserver::DispatchState {
  explicit DispatchState(MeetingWindow *receiver) : window(receiver) {}

  std::mutex mutex;
  MeetingWindow *window;
};

MeetingWindowObserver::MeetingWindowObserver(MeetingWindow *window)
    : m_dispatch(std::make_shared<DispatchState>(window)) {}

void MeetingWindowObserver::Detach() {
  std::lock_guard<std::mutex> lock(m_dispatch->mutex);
  m_dispatch->window = nullptr;
}

void MeetingWindowObserver::Post(std::function<void(MeetingWindow &)> task) {
  const auto dispatch = m_dispatch;
  std::lock_guard<std::mutex> lock(dispatch->mutex);
  if (!dispatch->window) {
    return;
  }

  // Posting and detaching are ordered by the same mutex. The queued task
  // rechecks attachment on the window thread before touching the UI.
  UiEventDispatcher::Post(dispatch->window, [dispatch, task = std::move(task)] {
    MeetingWindow *window = nullptr;
    {
      std::lock_guard<std::mutex> lock(dispatch->mutex);
      window = dispatch->window;
    }
    if (window) {
      task(*window);
    }
  });
}

void MeetingWindowObserver::OnEngineStateChanged(VCE::EngineState state) {
  Post([state](MeetingWindow &window) { window.HandleEngineStateChanged(state); });
}

void MeetingWindowObserver::OnMicrophoneStateChanged(bool enabled) {
  Post([enabled](MeetingWindow &window) { window.HandleMicrophoneStateChanged(enabled); });
}

void MeetingWindowObserver::OnCameraStateChanged(bool enabled) {
  Post([enabled](MeetingWindow &window) { window.HandleCameraStateChanged(enabled); });
}

void MeetingWindowObserver::OnTransportConnectionStateChanged(bool connected) {
  Post([connected](MeetingWindow &window) {
    window.HandleTransportConnectionStateChanged(connected);
  });
}

void MeetingWindowObserver::OnRenderVideoFrameReady(const std::string &user_id) {
  Post([id = QString::fromStdString(user_id)](MeetingWindow &window) {
    window.HandleRenderVideoFrameReady(id);
  });
}

void MeetingWindowObserver::OnMeetingUserJoined(const std::string &user_id,
                                                 const std::string &username) {
  Post([id = QString::fromStdString(user_id),
        name = QString::fromStdString(username)](MeetingWindow &window) {
    window.HandleMeetingUserJoined(id, name);
  });
}

void MeetingWindowObserver::OnMeetingUserLeft(const std::string &user_id) {
  Post([id = QString::fromStdString(user_id)](MeetingWindow &window) {
    window.HandleMeetingUserLeft(id);
  });
}

void MeetingWindowObserver::OnMeetingEnded() {
  Post([](MeetingWindow &window) { window.HandleMeetingEnded(); });
}
