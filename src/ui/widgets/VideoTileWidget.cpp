#include "ui/widgets/VideoTileWidget.h"

#include "engine/public/VceEngine.h"

#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QVBoxLayout>

VideoTileWidget::VideoTileWidget(const QString &user_id,
                                 const QString &display_name,
                                 bool is_local,
                                 QWidget *parent)
    : QOpenGLWidget(parent),
      m_user_id(user_id),
      m_display_name(display_name.trimmed().isEmpty() ? user_id
                                                      : display_name.trimmed()),
      m_is_local(is_local),
      m_engine(VCE::VceEngine::GetInstance()) {
  setMinimumSize(160, 90);

  // Keep text on Qt's widget backing store, above the OpenGL framebuffer.
  auto *name_label = new QLabel(m_display_name, this);
  name_label->setTextFormat(Qt::PlainText);
  name_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  name_label->setAttribute(Qt::WA_TransparentForMouseEvents);
  name_label->setStyleSheet(QStringLiteral(
      "color: white; background: transparent; font-size: 12pt; font-weight: bold;"));
  name_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  name_label->setFixedHeight(24);

  auto *overlay_layout = new QVBoxLayout(this);
  overlay_layout->setContentsMargins(12, 0, 12, 16);
  overlay_layout->addStretch();
  overlay_layout->addWidget(name_label);
}

VideoTileWidget::~VideoTileWidget() {
  ReleaseRenderUser();
}

QString VideoTileWidget::UserId() const { return m_user_id; }

QString VideoTileWidget::DisplayName() const { return m_display_name; }

void VideoTileWidget::SetCameraEnabled(bool enabled) {
  m_camera_enabled = enabled;
  update();
}

void VideoTileWidget::SetMicEnabled(bool enabled) {
  m_mic_enabled = enabled;
  update();
}

void VideoTileWidget::ReleaseRenderUser() {
  if (!m_render_user_registered || !m_engine) {
    return;
  }

  makeCurrent();
  m_engine->RemoveRenderUser(m_user_id.toStdString());
  doneCurrent();
  m_render_user_registered = false;
}

void VideoTileWidget::initializeGL() {
  EnsureRenderUser();
}

void VideoTileWidget::resizeGL(int width, int height) {
  if (!EnsureRenderUser()) {
    return;
  }

  m_engine->UpdateUserVideoSize(m_user_id.toStdString(), width, height);
}

void VideoTileWidget::paintGL() {
  bool rendered_video = false;
  if (m_camera_enabled && EnsureRenderUser()) {
    rendered_video =
        m_engine->RenderUser(m_user_id.toStdString()) == VCE::Result::kRet_Success;
  }

  if (!rendered_video) {
    PaintPlaceholder();
  }

  PaintOverlay();
}

bool VideoTileWidget::EnsureRenderUser() {
  if (m_render_user_registered) {
    return true;
  }

  if (!m_engine) {
    return false;
  }

  const VCE::Result result =
      m_engine->AddRenderUser(m_user_id.toStdString(), m_is_local);
  m_render_user_registered = result == VCE::Result::kRet_Success ||
                             result == VCE::Result::kRet_AlreadyInitialized;
  return m_render_user_registered;
}

void VideoTileWidget::PaintPlaceholder() {
  QPainter painter(this);
  painter.fillRect(rect(), m_camera_enabled ? QColor("#25354a")
                                            : QColor("#2d333b"));

  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#475467"));

  const int avatar_size = qMin(width(), height()) / 3;
  const QRect avatar_rect((width() - avatar_size) / 2,
                          (height() - avatar_size) / 2 - 10, avatar_size,
                          avatar_size);
  painter.drawEllipse(avatar_rect);
}

void VideoTileWidget::PaintOverlay() {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  const QRect footer_rect(0, height() - 52, width(), 52);
  QLinearGradient gradient(footer_rect.topLeft(), footer_rect.bottomLeft());
  gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
  gradient.setColorAt(1.0, QColor(0, 0, 0, 170));
  painter.fillRect(footer_rect, gradient);

  const int indicator_size = 12;
  const QRect indicator_rect(width() - indicator_size - 14,
                             14,
                             indicator_size,
                             indicator_size);
  painter.setPen(QPen(QColor(255, 255, 255, 190), 1));
  painter.setBrush(QColor(m_mic_enabled ? "#32d583" : "#f04438"));
  painter.drawEllipse(indicator_rect);
}
