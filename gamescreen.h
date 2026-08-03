#ifndef GAMESCREEN_H
#define GAMESCREEN_H

#include "gamecontent.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

class QResizeEvent;

class QPropertyAnimation;

namespace Ui
{
class GameScreen;
}

class GameScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit GameScreen(
            signed int PlayerCount                    = 1,
            const QString &GamepackPath               = QString(),
            const QString &ProfilePicturePath         = QString(),
            int AnswerDuration                        = 5,
            int QuestionDuration                      = 5,
            int QuestionPickDuration                  = 15,
            int AnswerWaitDuration                    = 5,
            QWidget *parent                           = nullptr);
      ~GameScreen();

    signals:
      void questionSelected(int themeIndex, int questionIndex);

    protected:
      void resizeEvent(QResizeEvent *event) override;

    private slots:
      void showQuestion(int themeIndex, int questionIndex);

    private:
      enum class GamePhase
      {
            PickingQuestion,
            ReadingQuestion,
            WaitingForReaction,
            Answering,
            ShowingAnswer
      };

      void startPhaseTimer(GamePhase phase, unsigned int durationMs);
      void updateTimerProgress();
      void handlePhaseTimeout();
      void showAnswer();
      void returnToBoard();
      void pickRandomQuestion();
      void displayContent(const QString &text, MediaType mediaType,
                          const QString &mediaPath);
      void fitDisplayedPixmap();
      void startReactionFlash();
      void stopReactionFlash();

      static constexpr unsigned int AnswerRevealDuration{5000U};

      Ui::GameScreen *ui;
      QTimer *m_tickTimer;
      QElapsedTimer *m_globalTimer;
      QPropertyAnimation *m_progressAnimation;
      QTimer *m_flashTimer;
      unsigned int m_answerDuration;
      unsigned int m_questionDuration;
      unsigned int m_questionPickDuration;
      unsigned int m_answerWaitDuration;
      QString m_gamepackPath;
      Game m_game;
      GamePhase m_phase{GamePhase::PickingQuestion};
      int m_currentThemeIndex{-1};
      int m_currentQuestionIndex{-1};
      unsigned int m_phaseDuration{};
      QPixmap m_displayedPixmap;
      QString m_questionFrameStyleSheet;
      int m_flashStep{};
};

#endif // GAMESCREEN_H
