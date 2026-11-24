#include "app/AppLog.h"

#include <QDir>
#include <QByteArray>
#include <QMessageLogContext>
#include <QStandardPaths>
#include <QString>

#include <cstdlib>

#include "log/include/Logger.h"

namespace {

LOGGER::LogLevel FromQtLevel(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return LOGGER::LogLevel::kDebug;
  case QtInfoMsg:
    return LOGGER::LogLevel::kInfo;
  case QtWarningMsg:
    return LOGGER::LogLevel::kWarn;
  case QtCriticalMsg:
    return LOGGER::LogLevel::kError;
  case QtFatalMsg:
    return LOGGER::LogLevel::kCritical;
  }
  return LOGGER::LogLevel::kInfo;
}

LOGGER::LogLevel LevelFromEnvironment() {
  const QByteArray value = qgetenv("MEETING_LOG_LEVEL").trimmed().toUpper();
  if (value == "OFF") {
    return LOGGER::LogLevel::kOff;
  }
  if (value == "INFO") {
    return LOGGER::LogLevel::kInfo;
  }
  if (value == "WARN" || value == "WARNING") {
    return LOGGER::LogLevel::kWarn;
  }
  if (value == "ERROR") {
    return LOGGER::LogLevel::kError;
  }
  if (value == "CRITICAL" || value == "FATAL") {
    return LOGGER::LogLevel::kCritical;
  }
  return LOGGER::LogLevel::kDebug;
}

void MessageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &message) {
  LOGGER::Logger::Instance().Log(FromQtLevel(type),
                                 context.file ? context.file : "qt",
                                 context.line,
                                 message.toStdString());
  if (type == QtFatalMsg) {
    std::abort();
  }
}

} // namespace

void AppLog::Install() {
  const QString log_dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
      QStringLiteral("/logs");
  QDir().mkpath(log_dir);

  LOGGER::Logger::Instance().Initialize(LOGGER::LogOutput::kConsoleAndFile,
                                        LevelFromEnvironment(),
                                        log_dir.toStdString());
  qInstallMessageHandler(MessageHandler);
}

void AppLog::Shutdown() {
  qInstallMessageHandler(nullptr);
  LOGGER::Logger::Instance().Shutdown();
}
