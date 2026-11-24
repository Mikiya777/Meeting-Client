#ifndef MEETING_CONTROL_BAR_H
#define MEETING_CONTROL_BAR_H

#include "engine/public/VceTypes.h"

#include <QWidget>

#include <vector>

class QMenu;
class QPushButton;
class QToolButton;

class MeetingControlBar : public QWidget {
  Q_OBJECT

public:
  explicit MeetingControlBar(QWidget *parent = nullptr);

  void SetMicrophoneEnabled(bool enabled);
  void SetCameraEnabled(bool enabled);
  void SetBeautyEnabled(bool enabled);
  void SetMicrophoneDevices(const std::vector<VCE::MediaDeviceInfo> &devices);
  void SetCameraDevices(const std::vector<VCE::MediaDeviceInfo> &devices);

signals:
  void MicToggled(bool enabled);
  void CameraToggled(bool enabled);
  void BeautyToggled(bool enabled);
  void MicrophoneDeviceSelected(const QString &device_id);
  void CameraDeviceSelected(const QString &device_id);
  void LeaveClicked();
  void MembersClicked();
  void ChatClicked();

private:
  void SetupUi();

  QPushButton *m_mic_button = nullptr;
  QPushButton *m_camera_button = nullptr;
  QPushButton *m_beauty_button = nullptr;
  QToolButton *m_mic_device_button = nullptr;
  QToolButton *m_camera_device_button = nullptr;
  QMenu *m_mic_device_menu = nullptr;
  QMenu *m_camera_device_menu = nullptr;
};

#endif // MEETING_CONTROL_BAR_H
