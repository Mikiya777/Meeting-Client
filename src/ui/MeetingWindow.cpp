#include "ui/MeetingWindow.h"

#include "engine/public/VceEngine.h"
#include "ui/MeetingWindowObserver.h"
#include "ui/widgets/MeetingControlBar.h"
#include "ui/widgets/MeetingSidePanel.h"
#include "ui/widgets/NotificationToast.h"
#include "ui/widgets/VideoGridWidget.h"

#include <QByteArray>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum class HomeIcon { Home, Camera, Join };

QPixmap DrawHomeIcon(HomeIcon icon) {
  const int size = icon == HomeIcon::Home ? 20 : 58;
  QPixmap pixmap(size * 2, size * 2);
  pixmap.setDevicePixelRatio(2);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  if (icon == HomeIcon::Home) {
    QPainterPath outline;
    outline.moveTo(2.5, 8.5);
    outline.lineTo(10, 2);
    outline.lineTo(17.5, 8.5);
    outline.lineTo(17.5, 17.5);
    outline.lineTo(12, 17.5);
    outline.lineTo(12, 12);
    outline.lineTo(8, 12);
    outline.lineTo(8, 17.5);
    outline.lineTo(2.5, 17.5);
    outline.closeSubpath();
    painter.setPen(QPen(QColor("#292e36"), 1.5, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(outline);
  } else {
    const QColor background(icon == HomeIcon::Camera ? "#ff762d" : "#0871e9");
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 17, 17);
    painter.setBrush(Qt::white);
    if (icon == HomeIcon::Camera) {
      painter.drawRoundedRect(QRectF(14, 21, 21, 16), 4, 4);
      QPainterPath lens;
      lens.moveTo(36, 26);
      lens.lineTo(42, 21.5);
      lens.quadTo(44, 20, 44, 23);
      lens.lineTo(44, 35);
      lens.quadTo(44, 38, 42, 36.5);
      lens.lineTo(36, 32);
      lens.closeSubpath();
      painter.drawPath(lens);
    } else {
      painter.drawRoundedRect(QRectF(17, 17, 24, 24), 5, 5);
      painter.setPen(QPen(background, 2, Qt::SolidLine, Qt::RoundCap));
      painter.drawLine(QPointF(24, 29), QPointF(34, 29));
      painter.drawLine(QPointF(29, 24), QPointF(29, 34));
    }
  }

  painter.end();
  return pixmap;
}

} // namespace

MeetingWindow::MeetingWindow(QWidget *parent) : QWidget(parent) {
  SetupUi();
  SetupConnections();
}

void MeetingWindow::OpenMeeting(StartupMode mode) {
  StartupOptions startup_options;
  startup_options.mode = mode;
  if (!ShowStartupDialog(&startup_options)) {
    return;
  }

  ConfigureRuntime(startup_options);
  m_engine = VCE::VceEngine::GetInstance();

  // 先注册观察者，再初始化 Engine，保证 Initialized 事件不会丢失。
  m_engine_observer =
      std::make_shared<MeetingWindowObserver>(this);
  m_engine->AddObserver(m_engine_observer);

  const VCE::Result result = m_engine->Init();
  if (result != VCE::Result::kRet_Success &&
      result != VCE::Result::kRet_AlreadyInitialized) {
    Shutdown();
    m_toast->ShowMessage(tr("Engine initialization failed"));
    return;
  }

  if (!StartMeeting(startup_options)) {
    Shutdown();
    return;
  }

  m_pages->setCurrentWidget(m_room_page);
  RefreshMediaDevices();
  m_toast->ShowMessage(tr("Meeting ready"));
}

MeetingWindow::~MeetingWindow() {
  Shutdown();
}

void MeetingWindow::closeEvent(QCloseEvent *event) {
  Shutdown();
  QWidget::closeEvent(event);
}

void MeetingWindow::Shutdown() {
  if (!m_engine) {
    return;
  }

  m_meeting_active = false;
  m_engine_observer->Detach();
  m_engine->RemoveObserver(m_engine_observer);
  m_video_grid->ClearUsers();

  if (!m_engine->GetCurrentMeetingInfo().meeting_id.empty()) {
    const auto result = m_engine->LeaveMeeting();
    if (result != VCE::Result::kRet_Success) {
      qWarning() << "Leave meeting failed during shutdown:" << static_cast<int>(result);
    }
  }
  m_engine_observer.reset();
  m_engine.reset();
  VCE::VceEngine::ReleaseInstance();
}

void MeetingWindow::ShowHomePage() {
  Shutdown();
  m_side_panel->ClearMembers();
  m_control_bar->SetMicrophoneEnabled(false);
  m_control_bar->SetCameraEnabled(false);
  m_control_bar->SetBeautyEnabled(false);
  m_toast->hide();
  m_pages->setCurrentWidget(m_home_page);
}

void MeetingWindow::SetupUi() {
  setObjectName(QStringLiteral("meetingWindow"));
  setWindowTitle(tr("Video Meeting"));
  setMinimumSize(760, 520);

  auto *window_layout = new QVBoxLayout(this);
  window_layout->setContentsMargins(0, 0, 0, 0);
  m_pages = new QStackedWidget(this);
  window_layout->addWidget(m_pages);

  m_home_page = CreateHomePage();
  m_pages->addWidget(m_home_page);
  m_room_page = new QWidget(m_pages);
  m_pages->addWidget(m_room_page);

  auto *main_layout = new QVBoxLayout(m_room_page);
  main_layout->setContentsMargins(12, 12, 12, 12);
  main_layout->setSpacing(8);

  auto *top_bar = new QFrame(m_room_page);
  top_bar->setObjectName(QStringLiteral("topBar"));
  top_bar->setMinimumHeight(68);

  auto *top_layout = new QHBoxLayout(top_bar);
  top_layout->setContentsMargins(18, 10, 18, 10);
  auto *title = new QLabel(tr("Meeting Room"), top_bar);
  title->setObjectName(QStringLiteral("meetingTitle"));
  m_meeting_meta = new QLabel(tr("Meeting ID: preparing"), top_bar);
  m_meeting_meta->setObjectName(QStringLiteral("meetingMeta"));
  m_connection_state = new QLabel(tr("Initializing"), top_bar);
  m_connection_state->setObjectName(QStringLiteral("meetingMeta"));

  top_layout->addWidget(title);
  top_layout->addSpacing(16);
  top_layout->addWidget(m_meeting_meta);
  top_layout->addStretch();
  top_layout->addWidget(m_connection_state);

  auto *center_splitter = new QSplitter(Qt::Horizontal, m_room_page);
  center_splitter->setChildrenCollapsible(false);
  m_video_grid = new VideoGridWidget(center_splitter);
  m_side_panel = new MeetingSidePanel(center_splitter);
  m_side_panel->setMinimumWidth(240);
  m_side_panel->setMaximumWidth(420);
  center_splitter->addWidget(m_video_grid);
  center_splitter->addWidget(m_side_panel);
  center_splitter->setStretchFactor(0, 1);
  center_splitter->setStretchFactor(1, 0);
  center_splitter->setSizes({900, 300});

  m_control_bar = new MeetingControlBar(m_room_page);
  m_toast = new NotificationToast(this);

  main_layout->addWidget(top_bar);
  main_layout->addWidget(center_splitter, 1);
  main_layout->addWidget(m_control_bar);
}

QWidget *MeetingWindow::CreateHomePage() {
  auto *page = new QFrame(m_pages);
  page->setObjectName(QStringLiteral("meetingHomePage"));
  auto *body_layout = new QHBoxLayout(page);
  body_layout->setContentsMargins(8, 8, 8, 8);
  body_layout->setSpacing(8);

  auto *navigation = new QFrame(page);
  navigation->setObjectName(QStringLiteral("homeNavigation"));
  navigation->setFixedSize(72, 56);
  auto *navigation_layout = new QVBoxLayout(navigation);
  navigation_layout->setContentsMargins(0, 6, 0, 6);
  navigation_layout->setSpacing(2);
  auto *home_symbol = new QLabel(navigation);
  home_symbol->setPixmap(DrawHomeIcon(HomeIcon::Home));
  auto *home_text = new QLabel(tr("Home"), navigation);
  navigation_layout->addWidget(home_symbol, 0, Qt::AlignHCenter);
  navigation_layout->addWidget(home_text, 0, Qt::AlignHCenter);
  body_layout->addWidget(navigation, 0, Qt::AlignTop);

  auto *panel = new QFrame(page);
  panel->setObjectName(QStringLiteral("homePanel"));
  body_layout->addWidget(panel, 1);

  auto *panel_layout = new QVBoxLayout(panel);
  panel_layout->setContentsMargins(20, 16, 20, 16);
  panel_layout->setSpacing(0);
  panel_layout->addStretch(4);

  auto *content = new QWidget(panel);
  content->setObjectName(QStringLiteral("homeContent"));
  auto *content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(0, 0, 0, 0);
  content_layout->setSpacing(0);

  auto *clock = new QLabel(content);
  clock->setObjectName(QStringLiteral("homeClock"));
  clock->setAlignment(Qt::AlignCenter);
  auto *date = new QLabel(content);
  date->setObjectName(QStringLiteral("homeDate"));
  date->setAlignment(Qt::AlignCenter);
  content_layout->addWidget(clock);
  content_layout->addSpacing(6);
  content_layout->addWidget(date);
  content_layout->addSpacing(32);

  auto *actions = new QHBoxLayout;
  actions->setSpacing(16);
  const auto make_action = [&](const QString &label, const QString &name,
                               HomeIcon icon, StartupMode mode) {
    auto *button = new QToolButton(content);
    button->setObjectName(name);
    button->setText(label);
    button->setIcon(QIcon(DrawHomeIcon(icon)));
    button->setIconSize(QSize(58, 58));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setFixedSize(122, 104);
    button->setFocusPolicy(Qt::StrongFocus);
    connect(button, &QToolButton::clicked, this, [this, mode] { OpenMeeting(mode); });
    actions->addWidget(button);
  };
  make_action(tr("New Meeting"), QStringLiteral("homeCreateButton"),
              HomeIcon::Camera, StartupMode::Create);
  make_action(tr("Join Meeting"), QStringLiteral("homeJoinButton"),
              HomeIcon::Join, StartupMode::Join);
  content_layout->addLayout(actions);

  panel_layout->addWidget(content, 0, Qt::AlignHCenter);
  panel_layout->addStretch(6);

  const auto update_clock = [clock, date] {
    const auto now = QDateTime::currentDateTime();
    const QLocale locale;
    clock->setText(now.time().toString(QStringLiteral("HH:mm")));
    const auto date_format = locale.language() == QLocale::Chinese
                                 ? QStringLiteral("M月d日 dddd")
                                 : QStringLiteral("MMMM d, dddd");
    date->setText(locale.toString(now.date(), date_format));
  };
  auto *timer = new QTimer(page);
  connect(timer, &QTimer::timeout, page, update_clock);
  update_clock();
  timer->start(1000);

  return page;
}

void MeetingWindow::SetupConnections() {
  connect(m_control_bar, &MeetingControlBar::MembersClicked, m_side_panel,
          &MeetingSidePanel::ToggleMembers);
  connect(m_control_bar, &MeetingControlBar::ChatClicked, m_side_panel,
          &MeetingSidePanel::ToggleChat);

  connect(m_control_bar, &MeetingControlBar::MicToggled, this,
          [this](bool enabled) {
            if (!m_engine) {
              return;
            }
            // 点击只向 Engine 发出请求。最终 UI 状态由 Observer 回调确认。
            const VCE::Result result =
                enabled ? m_engine->OpenMicrophone()
                        : m_engine->CloseMicrophone();

            if (result != VCE::Result::kRet_Success) {
              // Engine 拒绝操作时恢复点击前的按钮状态。
              m_control_bar->SetMicrophoneEnabled(!enabled);
              m_toast->ShowMessage(
                  tr("Microphone operation failed"));
            }
          });

  connect(m_control_bar, &MeetingControlBar::CameraToggled, this,
          [this](bool enabled) {
            if (!m_engine) {
              return;
            }
            const VCE::Result result =
                enabled ? m_engine->OpenCamera()
                        : m_engine->CloseCamera();

            if (result != VCE::Result::kRet_Success) {
              m_control_bar->SetCameraEnabled(!enabled);
              m_toast->ShowMessage(tr("Camera operation failed"));
            }
          });

  connect(m_control_bar, &MeetingControlBar::MicrophoneDeviceSelected, this,
          [this](const QString &device_id) {
            if (!m_engine) {
              return;
            }
            const auto result =
                m_engine->SetMicrophoneDevice(device_id.toStdString());
            if (result == VCE::Result::kRet_Success) {
              m_toast->ShowMessage(tr("Microphone device changed"));
            } else {
              m_toast->ShowMessage(tr("Microphone device change failed"));
            }
            RefreshMediaDevices();
          });

  connect(m_control_bar, &MeetingControlBar::CameraDeviceSelected, this,
          [this](const QString &device_id) {
            if (!m_engine) {
              return;
            }
            const auto result = m_engine->SetCameraDevice(device_id.toStdString());
            if (result == VCE::Result::kRet_Success) {
              m_toast->ShowMessage(tr("Camera device changed"));
            } else {
              m_toast->ShowMessage(tr("Camera device change failed"));
            }
            RefreshMediaDevices();
          });

  connect(m_control_bar, &MeetingControlBar::BeautyToggled, this,
          [this](bool enabled) {
            if (!m_engine ||
                m_engine->SetBeautyEnabled(enabled) != VCE::Result::kRet_Success) {
              m_control_bar->SetBeautyEnabled(!enabled);
              m_toast->ShowMessage(tr("Beauty operation failed"));
              return;
            }
            m_toast->ShowMessage(enabled ? tr("Beauty enabled")
                                         : tr("Beauty disabled"));
          });

  connect(m_control_bar, &MeetingControlBar::LeaveClicked, this,
          &MeetingWindow::ConfirmLeave);
}

bool MeetingWindow::ShowStartupDialog(StartupOptions *options) {
  if (!options) {
    return false;
  }

  QDialog dialog(this);
  const bool joining = options->mode == StartupMode::Join;
  dialog.setWindowTitle(joining ? tr("Join Meeting") : tr("New Meeting"));
  dialog.setMinimumWidth(360);
  dialog.setModal(true);

  auto *layout = new QVBoxLayout(&dialog);
  auto *form = new QFormLayout();

  const QString default_username =
      QStringLiteral("user-%1").arg(QCoreApplication::applicationPid());
  auto *username_edit = new QLineEdit(default_username, &dialog);
  auto *server_edit = new QLineEdit(
      QString::fromUtf8(qgetenv("MEETING_SERVER_ADDRESS")),
      &dialog);
  if (server_edit->text().trimmed().isEmpty()) {
    server_edit->setText(QStringLiteral("127.0.0.1:50051"));
  }

  auto *meeting_id_edit = new QLineEdit(&dialog);
  meeting_id_edit->setPlaceholderText(tr("Meeting ID"));

  form->addRow(tr("Username"), username_edit);
  form->addRow(tr("Meeting server"), server_edit);
  if (joining) {
    form->addRow(tr("Meeting ID"), meeting_id_edit);
  } else {
    meeting_id_edit->hide();
  }

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Start"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));

  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
    if (username_edit->text().trimmed().isEmpty()) {
      QMessageBox::warning(&dialog, tr("Missing username"),
                           tr("Please enter a username."));
      return;
    }

    if (server_edit->text().trimmed().isEmpty()) {
      QMessageBox::warning(&dialog, tr("Missing server"),
                           tr("Please enter a meeting server address."));
      return;
    }

    if (joining && meeting_id_edit->text().trimmed().isEmpty()) {
      QMessageBox::warning(&dialog, tr("Missing meeting ID"),
                           tr("Please enter the meeting ID to join."));
      return;
    }

    dialog.accept();
  });

  layout->addLayout(form);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }

  options->username = username_edit->text().trimmed();
  options->server_address = server_edit->text().trimmed();
  options->meeting_id = meeting_id_edit->text().trimmed();
  return true;
}

void MeetingWindow::ConfigureRuntime(const StartupOptions &options) {
  qputenv("MEETING_SERVICE_MANAGER", QByteArrayLiteral("grpc"));
  qputenv("MEETING_TRANSPORT_ENGINE", QByteArrayLiteral("srs"));
  qputenv("MEETING_SRS_ENABLE_REAL_WEBRTC", QByteArrayLiteral("1"));
  qputenv("MEETING_SRS_DEBUG", QByteArrayLiteral("1"));
  qputenv("MEETING_SERVER_ADDRESS",
          options.server_address.toUtf8());
}

bool MeetingWindow::StartMeeting(const StartupOptions &options) {
  VCE::ServiceUserInfo user_info;
  const std::string username = options.username.toStdString();
  const std::string password = "demo-password";

  auto result = m_engine->RegisterUser(username, password, 0, &user_info);
  if (result == VCE::Result::kRet_Success) {
    result = m_engine->Login(username, password, 0, &user_info);
  } else {
    // 真实服务端会持久化用户；第二次启动 demo 时，注册可能因为用户名已存在失败。
    result = m_engine->Login(username, password, 0, &user_info);
  }

  if (result != VCE::Result::kRet_Success) {
    m_toast->ShowMessage(tr("User login failed"));
    return false;
  }

  VCE::MeetingInfo meeting_info;
  if (options.mode == StartupMode::Create) {
    result = m_engine->CreateMeeting(
        "Demo Meeting", "Local service route for transport integration",
        QDateTime::currentSecsSinceEpoch(), &meeting_info);
  } else {
    result = m_engine->JoinMeeting(options.meeting_id.toStdString(),
                                   &meeting_info);
  }

  if (result != VCE::Result::kRet_Success) {
    m_toast->ShowMessage(options.mode == StartupMode::Create
                             ? tr("Meeting creation failed")
                             : tr("Meeting join failed"));
    return false;
  }

  ApplyMeetingInfoToUi(meeting_info);
  m_meeting_active = true;
  m_connection_state->setText(tr("Meeting ready"));
  return true;
}

void MeetingWindow::ApplyMeetingInfoToUi(
    const VCE::MeetingInfo &meeting_info) {
  const auto current_user = m_engine->GetCurrentUserInfo();
  if (!current_user.user_id.empty()) {
    m_local_user_id = QString::fromStdString(current_user.user_id);
  }

  if (m_meeting_meta) {
    m_meeting_meta->setText(
        tr("Meeting ID: %1")
            .arg(QString::fromStdString(meeting_info.meeting_id)));
  }

  m_video_grid->ClearUsers();
  m_side_panel->ClearMembers();

  for (const auto &participant : meeting_info.participants) {
    const QString user_id = QString::fromStdString(participant.user_id);
    const QString display_name = participant.username.empty()
                                     ? user_id
                                     : QString::fromStdString(participant.username);
    const bool is_local = participant.user_id == current_user.user_id;
    m_video_grid->AddUser(user_id, display_name, is_local);
    m_side_panel->AddMember(user_id, display_name);
  }
}

void MeetingWindow::RefreshMediaDevices() {
  if (!m_engine || !m_control_bar) {
    return;
  }

  std::vector<VCE::MediaDeviceInfo> microphones;
  if (m_engine->GetMicrophoneDevices(&microphones) ==
      VCE::Result::kRet_Success) {
    m_control_bar->SetMicrophoneDevices(microphones);
  } else {
    m_control_bar->SetMicrophoneDevices({});
  }

  std::vector<VCE::MediaDeviceInfo> cameras;
  if (m_engine->GetCameraDevices(&cameras) == VCE::Result::kRet_Success) {
    m_control_bar->SetCameraDevices(cameras);
  } else {
    m_control_bar->SetCameraDevices({});
  }
}

void MeetingWindow::ConfirmLeave() {
  const auto answer = QMessageBox::question(
      this, tr("Leave meeting?"),
      tr("Are you sure you want to leave this meeting?"),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer == QMessageBox::Yes) {
    ShowHomePage();
  }
}

void MeetingWindow::HandleEngineStateChanged(
    VCE::EngineState state) {
  switch (state) {
    case VCE::EngineState::kES_Created:
      m_connection_state->setText(tr("Created"));
      break;
    case VCE::EngineState::kES_Initialized:
      if (!m_meeting_active) {
        m_connection_state->setText(tr("Engine ready"));
      }
      break;
    case VCE::EngineState::kES_ShuttingDown:
      m_connection_state->setText(tr("Shutting down"));
      break;
    case VCE::EngineState::kES_Stopped:
      m_connection_state->setText(tr("Stopped"));
      break;
  }

  qInfo() << "Engine state changed:" << static_cast<int>(state);
}

void MeetingWindow::HandleMicrophoneStateChanged(bool enabled) {
  m_control_bar->SetMicrophoneEnabled(enabled);
  m_video_grid->SetUserMicEnabled(m_local_user_id, enabled);
  m_toast->ShowMessage(enabled ? tr("Microphone enabled")
                               : tr("Microphone disabled"));
}

void MeetingWindow::HandleCameraStateChanged(bool enabled) {
  m_control_bar->SetCameraEnabled(enabled);
  m_video_grid->SetUserCameraEnabled(m_local_user_id, enabled);
  m_toast->ShowMessage(enabled ? tr("Camera enabled")
                               : tr("Camera disabled"));
}

void MeetingWindow::HandleTransportConnectionStateChanged(
    bool connected) {
  if (!m_meeting_active) {
    return;
  }
  m_connection_state->setText(
      connected ? tr("Transport connected")
                : tr("Transport disconnected"));
  m_toast->ShowMessage(
      connected ? tr("Transport connected")
                : tr("Transport disconnected"));
}

void MeetingWindow::HandleRenderVideoFrameReady(const QString &user_id) {
  if (!m_meeting_active) {
    return;
  }
  // Local camera state comes from the camera control event, not a queued frame.
  if (user_id != m_local_user_id) {
    m_video_grid->SetUserCameraEnabled(user_id, true);
  }
  m_video_grid->RequestUserRender(user_id);
}

void MeetingWindow::HandleMeetingUserJoined(const QString &user_id,
                                            const QString &username) {
  if (!m_meeting_active) {
    return;
  }
  const QString normalized_user_id = user_id.trimmed();
  if (normalized_user_id.isEmpty()) {
    return;
  }

  const QString display_name = username.trimmed().isEmpty()
                                   ? normalized_user_id
                                   : username.trimmed();
  m_video_grid->AddUser(normalized_user_id, display_name);
  m_side_panel->AddMember(normalized_user_id, display_name);
  m_toast->ShowMessage(tr("%1 joined").arg(display_name));
}

void MeetingWindow::HandleMeetingUserLeft(const QString &user_id) {
  if (!m_meeting_active) {
    return;
  }
  const QString username = user_id.trimmed();
  if (username.isEmpty()) {
    return;
  }

  m_video_grid->RemoveUser(username);
  m_side_panel->RemoveMember(username);
  m_toast->ShowMessage(tr("%1 left").arg(username));
}

void MeetingWindow::HandleMeetingEnded() {
  if (!m_meeting_active) {
    return;
  }
  m_meeting_active = false;
  m_connection_state->setText(tr("Meeting ended"));
  m_video_grid->ClearUsers();
  m_side_panel->ClearMembers();
  m_toast->ShowMessage(tr("Meeting ended"));
}
