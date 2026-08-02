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
                          int AnswerDuration          = 5,
                          int QuestionDuration        = 5,
                          int QuestionPickDuration    = 15,
                          int AnswerWaitDuration      = 5,
                          QWidget *parent             = nullptr);
      ~GameScreen();

    private slots:
      void StartTimer();

    private:
      Ui::GameScreen *ui;
      QTimer *m_tickTimer;
      QElapsedTimer *m_globalTimer;
      unsigned int m_answerDuration;
      unsigned int m_questionDuration;
      unsigned int m_questionPickDuration;
      unsigned int m_answerWaitDuration;
      Game m_game;
};

#endif // GAMESCREEN_H
