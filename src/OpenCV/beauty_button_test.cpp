#include "ui/widgets/MeetingControlBar.h"
#include <QApplication>
#include <QPushButton>

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  MeetingControlBar bar;
  auto *button = bar.findChild<QPushButton *>(QStringLiteral("beautyButton"));
  if (!button || button->isChecked()) return 1;
  int changes = 0;
  bool enabled = false;
  QObject::connect(&bar, &MeetingControlBar::BeautyToggled, [&](bool value) {
    ++changes;
    enabled = value;
  });
  button->click();
  if (changes != 1 || !enabled || !button->isChecked()) return 2;
  button->click();
  if (changes != 2 || enabled || button->isChecked()) return 3;
  bar.SetBeautyEnabled(true);
  if (changes != 2 || !button->isChecked()) return 4;
  return 0;
}
