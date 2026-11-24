#include "ui/MeetingWindow.h"

#include "app/AppLog.h"
#include "app/SkinManager.h"

#include <QApplication>
#include <QDebug>
#include <QLocale>
#include <QSurfaceFormat>
#include <QTranslator>

int main(int argc, char *argv[]) {
  QSurfaceFormat gl_format;
  gl_format.setVersion(3, 3);
  gl_format.setProfile(QSurfaceFormat::CoreProfile);
  gl_format.setDepthBufferSize(24);
  gl_format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(gl_format);

#if defined(Q_OS_WIN) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#endif
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("MeetingClient"));
  QApplication::setOrganizationName(QStringLiteral("MeetingClient"));

  AppLog::Install();
  if (!SkinManager::ApplyDefault(app)) {
    qWarning() << "Failed to load the default skin";
  }

  QTranslator translator;
  const QString locale = QLocale::system().name();
  if (translator.load(QStringLiteral(":/i18n/%1.qm").arg(locale))) {
    app.installTranslator(&translator);
  }

  int result = 0;
  {
    MeetingWindow window;
    window.resize(1200, 800);
    window.show();

    result = app.exec();
  }

  AppLog::Shutdown();
  return result;
}
