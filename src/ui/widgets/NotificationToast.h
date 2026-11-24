#ifndef NOTIFICATION_TOAST_H
#define NOTIFICATION_TOAST_H

#include <QLabel>

class NotificationToast : public QLabel {
public:
  explicit NotificationToast(QWidget *parent = nullptr);

  void ShowMessage(const QString &message, int duration_ms = 2200);

private:
  void Reposition();
};

#endif // NOTIFICATION_TOAST_H
