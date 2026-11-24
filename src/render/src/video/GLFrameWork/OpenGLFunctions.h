#pragma once

#include <QOpenGLFunctions_3_3_Core>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QOpenGLContext>
#else
#include <QOpenGLVersionFunctionsFactory>
#endif

namespace RENDER {

inline QOpenGLFunctions_3_3_Core *CurrentOpenGLFunctions() {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  auto *context = QOpenGLContext::currentContext();
  return context ? context->versionFunctions<QOpenGLFunctions_3_3_Core>() : nullptr;
#else
  return QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>();
#endif
}

} // namespace RENDER
