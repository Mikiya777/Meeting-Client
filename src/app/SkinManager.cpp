#include "app/SkinManager.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

bool SkinManager::ApplyDefault(QApplication &application) {
  QFile file(QStringLiteral(":/styles/styles/default.qss"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }

  QTextStream stream(&file);
  application.setStyleSheet(stream.readAll());
  return true;
}
