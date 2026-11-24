#include "ui/widgets/MeetingControlBar.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

MeetingControlBar::MeetingControlBar(QWidget *parent)
    : QWidget(parent) {
  SetupUi();
}

void MeetingControlBar::SetMicrophoneEnabled(bool enabled) {
  // 更新按钮时阻止 toggled 信号，避免：
  // Engine 回调 -> 修改按钮 -> 再次调用 Engine 的循环。
  const QSignalBlocker blocker(m_mic_button);

  m_mic_button->setChecked(enabled);
  m_mic_button->setText(enabled ? tr("Mic On") : tr("Mic Off"));
}

void MeetingControlBar::SetCameraEnabled(bool enabled) {
  const QSignalBlocker blocker(m_camera_button);

  m_camera_button->setChecked(enabled);
  m_camera_button->setText(enabled ? tr("Camera On")
                                   : tr("Camera Off"));
}

void MeetingControlBar::SetBeautyEnabled(bool enabled) {
  const QSignalBlocker blocker(m_beauty_button);
  m_beauty_button->setChecked(enabled);
  m_beauty_button->setText(enabled ? tr("Beauty On") : tr("Beauty Off"));
}

void MeetingControlBar::SetMicrophoneDevices(
    const std::vector<VCE::MediaDeviceInfo> &devices) {
  m_mic_device_menu->clear();
  QString selected_name;
  for (const auto &device : devices) {
    auto *action = m_mic_device_menu->addAction(
        QString::fromStdString(device.device_name));
    action->setCheckable(true);
    action->setChecked(device.selected);
    action->setData(QString::fromStdString(device.device_id));
    if (device.selected) {
      selected_name = QString::fromStdString(device.device_name);
    }
    connect(action, &QAction::triggered, this, [this, action]() {
      emit MicrophoneDeviceSelected(action->data().toString());
    });
  }
  if (devices.empty()) {
    auto *action = m_mic_device_menu->addAction(tr("No microphone found"));
    action->setEnabled(false);
  }
  m_mic_device_button->setEnabled(!devices.empty());
  m_mic_device_button->setToolTip(
      selected_name.isEmpty()
          ? tr("Select microphone")
          : tr("Current microphone: %1").arg(selected_name));
}

void MeetingControlBar::SetCameraDevices(
    const std::vector<VCE::MediaDeviceInfo> &devices) {
  m_camera_device_menu->clear();
  QString selected_name;
  for (const auto &device : devices) {
    auto *action = m_camera_device_menu->addAction(
        QString::fromStdString(device.device_name));
    action->setCheckable(true);
    action->setChecked(device.selected);
    action->setData(QString::fromStdString(device.device_id));
    if (device.selected) {
      selected_name = QString::fromStdString(device.device_name);
    }
    connect(action, &QAction::triggered, this, [this, action]() {
      emit CameraDeviceSelected(action->data().toString());
    });
  }
  if (devices.empty()) {
    auto *action = m_camera_device_menu->addAction(tr("No camera found"));
    action->setEnabled(false);
  }
  m_camera_device_button->setEnabled(!devices.empty());
  m_camera_device_button->setToolTip(
      selected_name.isEmpty()
          ? tr("Select camera")
          : tr("Current camera: %1").arg(selected_name));
}

void MeetingControlBar::SetupUi() {
  setObjectName(QStringLiteral("meetingControlBar"));
  setFixedHeight(80);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(20, 12, 20, 12);
  layout->setSpacing(10);
  layout->addStretch();

  m_mic_button = new QPushButton(tr("Mic Off"), this);
  m_camera_button = new QPushButton(tr("Camera Off"), this);
  m_mic_device_button = new QToolButton(this);
  m_camera_device_button = new QToolButton(this);
  m_mic_device_menu = new QMenu(m_mic_device_button);
  m_camera_device_menu = new QMenu(m_camera_device_button);
  auto *leave_button = new QPushButton(tr("Leave"), this);
  auto *members_button = new QPushButton(tr("Members"), this);
  auto *chat_button = new QPushButton(tr("Chat"), this);

  m_mic_button->setCheckable(true);
  m_camera_button->setCheckable(true);
  m_beauty_button = new QPushButton(tr("Beauty Off"), this);
  m_beauty_button->setObjectName(QStringLiteral("beautyButton"));
  m_beauty_button->setCheckable(true);
  m_beauty_button->setToolTip(tr("Light smoothing for preview and outgoing video"));
  m_mic_device_button->setText(tr("Mic device"));
  m_camera_device_button->setText(tr("Camera device"));
  m_mic_device_button->setPopupMode(QToolButton::InstantPopup);
  m_camera_device_button->setPopupMode(QToolButton::InstantPopup);
  m_mic_device_button->setMenu(m_mic_device_menu);
  m_camera_device_button->setMenu(m_camera_device_menu);
  m_mic_device_button->setEnabled(false);
  m_camera_device_button->setEnabled(false);
  leave_button->setObjectName(QStringLiteral("leaveButton"));

  layout->addWidget(m_mic_button);
  layout->addWidget(m_mic_device_button);
  layout->addWidget(m_camera_button);
  layout->addWidget(m_camera_device_button);
  layout->addWidget(m_beauty_button);
  layout->addWidget(leave_button);
  layout->addWidget(members_button);
  layout->addWidget(chat_button);

  layout->addStretch();

  connect(m_mic_button, &QPushButton::toggled, this,
          [this](bool checked) {
            m_mic_button->setText(checked ? tr("Mic On") : tr("Mic Off"));
            emit MicToggled(checked);
          });

  connect(m_camera_button, &QPushButton::toggled, this,
          [this](bool checked) {
            m_camera_button->setText(checked ? tr("Camera On")
                                             : tr("Camera Off"));
            emit CameraToggled(checked);
          });

  connect(m_beauty_button, &QPushButton::toggled, this,
          [this](bool checked) {
            SetBeautyEnabled(checked);
            emit BeautyToggled(checked);
          });

  connect(leave_button, &QPushButton::clicked,
          this, &MeetingControlBar::LeaveClicked);

  connect(members_button, &QPushButton::clicked,
          this, &MeetingControlBar::MembersClicked);

  connect(chat_button, &QPushButton::clicked,
          this, &MeetingControlBar::ChatClicked);
}
