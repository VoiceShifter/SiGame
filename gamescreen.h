#ifndef GAMESCREEN_H
#define GAMESCREEN_H

#include "gamecontent.h"

#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QWidget>
namespace Ui
{
class GameScreen;
}

class GameScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit GameScreen(signed int PlayerCount      = 1,
                          const QString &GamepackPath = QString(),
                          QWidget *parent             = nullptr);
      ~GameScreen();

    private slots:
      void StartTimer();

    private:
      Ui::GameScreen *ui;
      QTimer *m_tickTimer;
      QElapsedTimer *m_globalTimer;
      unsigned int m_globalTimeValue{15000};
      Game m_game;
};

#endif // GAMESCREEN_H
