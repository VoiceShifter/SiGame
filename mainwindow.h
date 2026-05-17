#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "gamescreen.h"
#include "singleplayerscreen.h"
#include <QMainWindow>
#include <QStackedWidget>
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private:
  Ui::MainWindow *ui;
  QStackedWidget *stack;
  SinglePlayerScreen *singleScreen;
  GameScreen *gameScreen;

private slots:
  void loadSingleSettings();
      void loadSingleGame(int PlayersCount);
  void loadSettings();
  void loadMultiplayer();
};

#endif // MAINWINDOW_H
