#include "ui/widgets/VideoGridWidget.h"
#include "ui/widgets/VideoTileWidget.h"

#include <QGridLayout>

#include <cmath>

VideoGridWidget::VideoGridWidget(QWidget *parent)
    : QWidget(parent) {
  SetupUi();
}

void VideoGridWidget::SetupUi() {
  setObjectName("videoGridWidget");
  m_grid_layout = new QGridLayout(this);
  m_grid_layout->setContentsMargins(8, 8, 8, 8);
  m_grid_layout->setSpacing(8);
}

bool VideoGridWidget::AddUser(const QString &user_id,
                              const QString &display_name,
                              bool is_local) {
  const QString normalized = user_id.trimmed();
  if (normalized.isEmpty() || m_tiles.contains(normalized)) {
    return false;
  }

  auto *tile = new VideoTileWidget(normalized, display_name, is_local, this);
  tile->SetCameraEnabled(m_camera_states.value(normalized, false));
  tile->SetMicEnabled(m_mic_states.value(normalized, false));

  m_tiles.insert(normalized, tile);
  m_user_order.append(normalized);
  Relayout();
  return true;
}

bool VideoGridWidget::RemoveUser(const QString &username) {
  auto *tile = m_tiles.take(username);
  if (!tile) {
    return false;
  }

  m_grid_layout->removeWidget(tile);
  m_user_order.removeAll(username);
  m_camera_states.remove(username);
  m_mic_states.remove(username);
  tile->ReleaseRenderUser();
  tile->hide();
  tile->deleteLater();
  Relayout();
  return true;
}

void VideoGridWidget::ClearUsers() {
  const auto tiles = m_tiles;
  m_tiles.clear();
  m_user_order.clear();
  m_camera_states.clear();
  m_mic_states.clear();
  for (auto *tile : tiles) {
    m_grid_layout->removeWidget(tile);
    tile->ReleaseRenderUser();
    tile->hide();
    tile->deleteLater();
  }
  Relayout();
}

void VideoGridWidget::RequestUserRender(const QString &username) {
  if (auto *tile = m_tiles.value(username)) {
    tile->update();
  }
}

void VideoGridWidget::SetUserCameraEnabled(const QString &username,
                                           bool enabled) {
  m_camera_states.insert(username, enabled);
  if (auto *tile = m_tiles.value(username)) {
    tile->SetCameraEnabled(enabled);
  }
}

void VideoGridWidget::SetUserMicEnabled(const QString &username, bool enabled) {
  m_mic_states.insert(username, enabled);
  if (auto *tile = m_tiles.value(username)) {
    tile->SetMicEnabled(enabled);
  }
}

int VideoGridWidget::UserCount() const {
  return m_tiles.size();
}

void VideoGridWidget::Relayout() {
  while (QLayoutItem *item = m_grid_layout->takeAt(0)) {
    delete item;
  }

  for (int row = 0; row < m_layout_rows; ++row) {
    m_grid_layout->setRowStretch(row, 0);
  }
  for (int column = 0; column < m_layout_columns; ++column) {
    m_grid_layout->setColumnStretch(column, 0);
  }
  m_layout_rows = 0;
  m_layout_columns = 0;

  const int count = m_tiles.size();
  if (count == 0) {
    return;
  }

  const int columns = count == 2 ? 2 : std::ceil(std::sqrt(count));
  int index = 0;
  for (const auto &username : m_user_order) {
    auto *tile = m_tiles.value(username);
    if (!tile) {
      continue;
    }
    m_grid_layout->addWidget(tile, index / columns, index % columns);
    ++index;
  }

  const int rows = (count + columns - 1) / columns;
  for (int row = 0; row < rows; ++row) {
    m_grid_layout->setRowStretch(row, 1);
  }
  for (int column = 0; column < columns; ++column) {
    m_grid_layout->setColumnStretch(column, 1);
  }
  m_layout_rows = rows;
  m_layout_columns = columns;
}
