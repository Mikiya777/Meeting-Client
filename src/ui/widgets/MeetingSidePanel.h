#ifndef MEETING_SIDE_PANEL_H
#define MEETING_SIDE_PANEL_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;

class MeetingSidePanel : public QWidget {
  Q_OBJECT

public:
  explicit MeetingSidePanel(QWidget *parent = nullptr);

  void ShowMembers();
  void ShowChat();
  void ToggleMembers();
  void ToggleChat();
  void AddMember(const QString &user_id, const QString &display_name);
  void RemoveMember(const QString &user_id);
  void ClearMembers();

private:
  void SetupUi();
  QWidget *CreateMembersPage();
  QWidget *CreateChatPage();
  void SetCurrentPage(int index, const QString &title);

  QStackedWidget *m_stacked_widget = nullptr;
  QLabel *m_title_label = nullptr;
  QListWidget *m_members_list = nullptr;
  QListWidget *m_chat_list = nullptr;
  QLineEdit *m_chat_input = nullptr;
};

#endif // MEETING_SIDE_PANEL_H
