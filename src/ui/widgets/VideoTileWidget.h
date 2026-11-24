#ifndef VIDEO_TILE_WIDGET_H
#define VIDEO_TILE_WIDGET_H

#include <QOpenGLWidget>

#include <memory>

namespace VCE {
class VceEngine;
} // namespace VCE

class VideoTileWidget : public QOpenGLWidget {
  Q_OBJECT

public:
  explicit VideoTileWidget(const QString &user_id,
                           const QString &display_name,
                           bool is_local,
                           QWidget *parent = nullptr);
  ~VideoTileWidget() override;

  QString UserId() const;
  QString DisplayName() const;
  void SetCameraEnabled(bool enabled);
  void SetMicEnabled(bool enabled);
  void ReleaseRenderUser();

protected:
  void initializeGL() override;
  void resizeGL(int width, int height) override;
  void paintGL() override;

private:
  bool EnsureRenderUser();
  void PaintPlaceholder();
  void PaintOverlay();

  QString m_user_id;
  QString m_display_name;
  bool m_is_local = false;
  bool m_camera_enabled = false;
  bool m_mic_enabled = false;
  bool m_render_user_registered = false;
  std::shared_ptr<VCE::VceEngine> m_engine;
};

#endif // VIDEO_TILE_WIDGET_H
