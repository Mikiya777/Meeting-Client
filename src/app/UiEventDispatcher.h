#ifndef UI_EVENT_DISPATCHER_H
#define UI_EVENT_DISPATCHER_H

#include <QMetaObject>
#include <QObject>

#include <functional>
#include <utility>

class UiEventDispatcher {
public:
  static void Post(QObject *receiver, std::function<void()> task) {
    if (!receiver || !task) {
      return;
    }

    QMetaObject::invokeMethod(
        receiver, [task = std::move(task)]() mutable { task(); },
        Qt::QueuedConnection);
  }
};

#endif // UI_EVENT_DISPATCHER_H
