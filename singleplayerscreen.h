#ifndef SINGLEPLAYERSCREEN_H
#define SINGLEPLAYERSCREEN_H

#include <QWidget>

namespace Ui {
class SinglePlayerScreen;
}

class SinglePlayerScreen : public QWidget {
  Q_OBJECT

public:
  explicit SinglePlayerScreen(QWidget *parent = nullptr);
  ~SinglePlayerScreen();

private slots:
  void pickPack();
  void createGame();
signals:
  void SingleGameStarted(int Players);

private:
  Ui::SinglePlayerScreen *ui;
  QString GamepackPath;
};

#endif // SINGLEPLAYERSCREEN_H
