#include "ui/MeetingWindow.h"
#include "ui/MeetingWindowObserver.h"
#include "ui/widgets/VideoGridWidget.h"
#include "ui/widgets/VideoTileWidget.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

namespace {

bool Check(bool ok, const char *message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return ok;
}

bool HasLabel(const MeetingWindow &window, const QString &text) {
  for (const auto *label : window.findChildren<QLabel *>()) {
    if (label->text() == text) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  auto window = std::make_unique<MeetingWindow>();
  window->resize(1200, 800);
  window->show();
  QApplication::processEvents();

  auto *panel = window->findChild<QWidget *>(QStringLiteral("homePanel"));
  auto *content = window->findChild<QWidget *>(QStringLiteral("homeContent"));
  auto *clock = window->findChild<QLabel *>(QStringLiteral("homeClock"));
  auto *date = window->findChild<QLabel *>(QStringLiteral("homeDate"));
  auto *create = window->findChild<QToolButton *>(QStringLiteral("homeCreateButton"));
  auto *join = window->findChild<QToolButton *>(QStringLiteral("homeJoinButton"));
  if (!Check(panel && content && clock && date && create && join && panel->isVisible(),
             "startup opens the meeting home page")) {
    return 1;
  }

  bool passed = Check(clock->isVisible() && !clock->text().isEmpty() &&
                          date->isVisible() && !date->text().isEmpty(),
                      "home page displays the current time and date");
  for (const auto size : {QSize(1200, 800), QSize(800, 560)}) {
    window->resize(size);
    QApplication::processEvents();
    const QPoint center = content->mapTo(panel, content->rect().center());
    passed &= Check(std::abs(center.x() - panel->rect().center().x()) <= 2 &&
                        center.y() > panel->height() / 3 &&
                        center.y() < panel->height() / 2,
                    "actions stay centered in the white panel when resized");
  }

  const auto check_setup_dialog = [&](QToolButton *button, bool joining) {
    bool matched = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dialog) {
        return;
      }
      for (const auto *field : dialog->findChildren<QLineEdit *>()) {
        if (field->placeholderText() == QStringLiteral("Meeting ID")) {
          matched = field->isVisible() == joining;
        }
      }
      dialog->reject();
    });
    button->click();
    return matched && panel->isVisible();
  };
  passed &= Check(check_setup_dialog(create, false), "create opens setup without a meeting ID field");
  passed &= Check(check_setup_dialog(join, true), "join opens setup with a meeting ID field");
  MeetingWindowObserver observer(window.get());

  std::thread worker([&] { observer.OnEngineStateChanged(VCE::EngineState::kES_Initialized); });
  worker.join();
  passed &= Check(!HasLabel(*window, QStringLiteral("Engine ready")), "worker event is queued");
  QCoreApplication::sendPostedEvents(window.get(), QEvent::MetaCall);
  passed &= Check(HasLabel(*window, QStringLiteral("Engine ready")), "queued worker event updates UI");

  observer.OnEngineStateChanged(VCE::EngineState::kES_ShuttingDown);
  observer.Detach();
  observer.OnEngineStateChanged(VCE::EngineState::kES_Stopped);
  QCoreApplication::sendPostedEvents(window.get(), QEvent::MetaCall);
  passed &= Check(HasLabel(*window, QStringLiteral("Engine ready")),
                  "detach suppresses both pending and future events");

  auto *grid = window->findChild<VideoGridWidget *>();
  if (!Check(grid && grid->AddUser(QStringLiteral("remote"), QStringLiteral("Remote")),
             "add a video tile")) {
    return 1;
  }
  auto *tile = grid->findChild<VideoTileWidget *>();
  if (!Check(tile != nullptr, "video tile is created")) {
    return 1;
  }
  tile->show();
  passed &= Check(!tile->isHidden(), "tile starts unhidden");
  passed &= Check(grid->RemoveUser(QStringLiteral("remote")) && tile->isHidden(),
                  "removed tile hides before deferred deletion");
  passed &= Check(grid->AddUser(QStringLiteral("remote"), QStringLiteral("Remote")),
                  "same user can rejoin before the old tile is deleted");
  grid->ClearUsers();
  for (const auto *child : grid->findChildren<VideoTileWidget *>()) {
    passed &= Check(child->isHidden(), "cleared tile hides before deferred deletion");
  }

  window->close();
  window.reset();
  std::thread late_worker([&] { observer.OnRenderVideoFrameReady("remote"); });
  late_worker.join();
  return passed ? 0 : 1;
}
