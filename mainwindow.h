#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "gamescreen.h"
#include "multiplayerhostscreen.h"
#include "multiplayerjoinscreen.h"
#include "singleplayerscreen.h"
#include <QMainWindow>
#include <QStackedWidget>
#include <QString>
class QMessageBox;

namespace Ui
{
class MainWindow;
}

class MainWindow : public QMainWindow
{
      Q_OBJECT

    public:
      explicit MainWindow(QWidget *parent = nullptr);
      ~MainWindow();

    private:
      Ui::MainWindow *ui;
      QStackedWidget *stack;
      SinglePlayerScreen *singleScreen{};
      GameScreen *gameScreen{};
      MultiplayerHostScreen *hostScreen{};
      MultiplayerJoinScreen *joinScreen{};
      QMessageBox *exitPopup;

    private slots:
      void loadSingleSettings();
      void loadSingleGame(int PlayersCount, const QString &GamepackPath,
                          const QString &ProfilePicturePath,
                          const QString &Nickname, int answerDuration,
                          int questionDuration,
                          int questionPickDuration, int answerWaitDuration);
      void loadSettings();
      void loadMultiplayer();
      void loadJoinSettings();
      void loadHostGame(MultiplayerHost *host, const QString &packPath);
      void loadClientGame(MultiplayerClient *client, const QString &packPath);
      void showExitPopup();
};

#endif // MAINWINDOW_H
