#include "ui/widgets/NotificationToast.h"

#include <QTimer>

NotificationToast::NotificationToast(QWidget *parent) : QLabel(parent) {
  setObjectName(QStringLiteral("notificationToast"));
  setAlignment(Qt::AlignCenter);
  hide();
}

void NotificationToast::ShowMessage(const QString &message, int duration_ms) {
  setText(message);
  adjustSize();
  Reposition();
  raise();
  show();
  QTimer::singleShot(duration_ms, this, &QWidget::hide);
}

void NotificationToast::Reposition() {
  if (!parentWidget()) {
    return;
  }

  constexpr int margin = 24;
  move(parentWidget()->width() - width() - margin,
       parentWidget()->height() - height() - margin);
}
