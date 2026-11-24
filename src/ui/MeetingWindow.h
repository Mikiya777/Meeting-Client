#ifndef MEETING_WINDOW_H
#define MEETING_WINDOW_H

#include "engine/public/VceTypes.h"

#include <QWidget>

#include <memory>

class QCloseEvent;
class QLabel;
class QStackedWidget;
class MeetingWindowObserver;
class MeetingControlBar;
class MeetingSidePanel;
class NotificationToast;
class VideoGridWidget;

namespace VCE {
class VceEngine;
} // namespace VCE

class MeetingWindow : public QWidget {
  Q_OBJECT

public:
  explicit MeetingWindow(QWidget *parent = nullptr);
  ~MeetingWindow() override;

  // 这些方法只由 MeetingWindowObserver 在 Qt 主线程中调用。
  void HandleEngineStateChanged(VCE::EngineState state);
  void HandleMicrophoneStateChanged(bool enabled);
  void HandleCameraStateChanged(bool enabled);
  void HandleTransportConnectionStateChanged(bool connected);
  void HandleRenderVideoFrameReady(const QString &user_id);
  void HandleMeetingUserJoined(const QString &user_id,
                               const QString &username);
  void HandleMeetingUserLeft(const QString &user_id);
  void HandleMeetingEnded();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  enum class StartupMode {
    Create,
    Join,
  };

  struct StartupOptions {
    StartupMode mode = StartupMode::Create;
    QString username;
    QString server_address;
    QString meeting_id;
  };

  void SetupUi();
  QWidget *CreateHomePage();
  void SetupConnections();
  void OpenMeeting(StartupMode mode);
  void ShowHomePage();
  bool ShowStartupDialog(StartupOptions *options);
  void ConfigureRuntime(const StartupOptions &options);
  bool StartMeeting(const StartupOptions &options);
  void ApplyMeetingInfoToUi(const VCE::MeetingInfo &meeting_info);
  void RefreshMediaDevices();
  void ConfirmLeave();
  void Shutdown();

  QStackedWidget *m_pages = nullptr;
  QWidget *m_home_page = nullptr;
  QWidget *m_room_page = nullptr;
  VideoGridWidget *m_video_grid = nullptr;
  MeetingSidePanel *m_side_panel = nullptr;
  MeetingControlBar *m_control_bar = nullptr;
  NotificationToast *m_toast = nullptr;
  QLabel *m_meeting_meta = nullptr;
  QLabel *m_connection_state = nullptr;
  QString m_local_user_id;
  bool m_meeting_active = false;

  // 窗口持有 Engine 和观察者；Engine 内部只保存观察者的 weak_ptr。
  std::shared_ptr<VCE::VceEngine> m_engine;
  std::shared_ptr<MeetingWindowObserver> m_engine_observer;
};

#endif // MEETING_WINDOW_H
