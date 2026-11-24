#ifndef VIDEO_GRID_WIDGET_H
#define VIDEO_GRID_WIDGET_H

#include <QHash>
#include <QStringList>
#include <QWidget>

class QGridLayout;
class VideoTileWidget;

class VideoGridWidget : public QWidget {
  Q_OBJECT

public:
  explicit VideoGridWidget(QWidget *parent = nullptr);

  bool AddUser(const QString &user_id,
               const QString &display_name,
               bool is_local = false);
  bool RemoveUser(const QString &user_id);
  void ClearUsers();
  void RequestUserRender(const QString &username);
  void SetUserCameraEnabled(const QString &username, bool enabled);
  void SetUserMicEnabled(const QString &username, bool enabled);
  int UserCount() const;

private:
  void SetupUi();
  void Relayout();

  QGridLayout *m_grid_layout = nullptr;
  QHash<QString, VideoTileWidget *> m_tiles;
  QHash<QString, bool> m_camera_states;
  QHash<QString, bool> m_mic_states;
  QStringList m_user_order;
  int m_layout_rows = 0;
  int m_layout_columns = 0;
};

#endif // VIDEO_GRID_WIDGET_H
