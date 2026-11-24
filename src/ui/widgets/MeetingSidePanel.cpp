#include "ui/widgets/MeetingSidePanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

MeetingSidePanel::MeetingSidePanel(QWidget *parent)
    : QWidget(parent) {
  SetupUi();
}

void MeetingSidePanel::SetupUi() {
  setObjectName(QStringLiteral("meetingSidePanel"));
  auto *main_layout = new QVBoxLayout(this);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);

  auto *header = new QFrame(this);
  header->setObjectName(QStringLiteral("sideHeader"));
  auto *header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(14, 10, 8, 10);

  m_title_label = new QLabel(header);
  m_title_label->setObjectName(QStringLiteral("sideTitle"));
  auto *close_button = new QPushButton(QStringLiteral("×"), header);
  close_button->setFixedSize(30, 30);
  close_button->setToolTip(tr("Close"));

  header_layout->addWidget(m_title_label);
  header_layout->addStretch();
  header_layout->addWidget(close_button);

  m_stacked_widget = new QStackedWidget(this);
  m_stacked_widget->addWidget(CreateMembersPage());
  m_stacked_widget->addWidget(CreateChatPage());

  main_layout->addWidget(header);
  main_layout->addWidget(m_stacked_widget);

  connect(close_button, &QPushButton::clicked, this, &QWidget::hide);
  ShowMembers();
}

QWidget *MeetingSidePanel::CreateMembersPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);
  m_members_list = new QListWidget(page);
  layout->addWidget(m_members_list);

  return page;
}

QWidget *MeetingSidePanel::CreateChatPage() {
  auto *page = new QWidget(this);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(12, 12, 12, 12);

  m_chat_list = new QListWidget(page);
  auto *input_layout = new QHBoxLayout;
  m_chat_input = new QLineEdit(page);
  m_chat_input->setPlaceholderText(tr("Type a message"));
  auto *send_button = new QPushButton(tr("Send"), page);

  input_layout->addWidget(m_chat_input, 1);
  input_layout->addWidget(send_button);
  layout->addWidget(m_chat_list, 1);
  layout->addLayout(input_layout);

  const auto send_message = [this]() {
    const QString message = m_chat_input->text().trimmed();
    if (message.isEmpty()) {
      return;
    }
    m_chat_list->addItem(tr("Me: %1").arg(message));
    m_chat_input->clear();
  };

  connect(send_button, &QPushButton::clicked, this, send_message);
  connect(m_chat_input, &QLineEdit::returnPressed, this, send_message);

  return page;
}

void MeetingSidePanel::ShowMembers() {
  SetCurrentPage(0, tr("Members"));
}

void MeetingSidePanel::ShowChat() {
  SetCurrentPage(1, tr("Chat"));
}

void MeetingSidePanel::ToggleMembers() {
  if (isVisible() && m_stacked_widget->currentIndex() == 0) {
    hide();
    return;
  }
  ShowMembers();
}

void MeetingSidePanel::ToggleChat() {
  if (isVisible() && m_stacked_widget->currentIndex() == 1) {
    hide();
    return;
  }
  ShowChat();
}

void MeetingSidePanel::AddMember(const QString &user_id,
                                 const QString &display_name) {
  if (user_id.trimmed().isEmpty()) {
    return;
  }

  for (int index = 0; index < m_members_list->count(); ++index) {
    if (m_members_list->item(index)->data(Qt::UserRole).toString() == user_id) {
      return;
    }
  }

  const QString visible_name = display_name.trimmed().isEmpty()
                                   ? user_id
                                   : display_name.trimmed();
  auto *item = new QListWidgetItem(visible_name, m_members_list);
  item->setData(Qt::UserRole, user_id);
}

void MeetingSidePanel::RemoveMember(const QString &user_id) {
  for (int index = m_members_list->count() - 1; index >= 0; --index) {
    if (m_members_list->item(index)->data(Qt::UserRole).toString() == user_id) {
      delete m_members_list->takeItem(index);
    }
  }
}

void MeetingSidePanel::ClearMembers() {
  m_members_list->clear();
}

void MeetingSidePanel::SetCurrentPage(int index, const QString &title) {
  m_stacked_widget->setCurrentIndex(index);
  m_title_label->setText(title);
  show();
}
