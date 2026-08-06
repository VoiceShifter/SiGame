#ifndef GAMESCREEN_H
#define GAMESCREEN_H

#include "gamecontent.h"

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

class QInputDialog;
class QLabel;
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
            const QString &Nickname                   = QString(),
            int AnswerDuration                        = 5,
            int QuestionDuration                      = 5,
            int QuestionPickDuration                  = 15,
            int AnswerWaitDuration                    = 5,
            QWidget *parent                           = nullptr);
      ~GameScreen();

    signals:
      void questionSelected(int themeIndex, int questionIndex);
      void incorrectAnswerSubmitted(int playerIndex, const QString &answer);
      void forAllQuestionSelected(int roundIndex, int themeIndex,
                                  int questionIndex);
      void secretPublicPriceQuestionSelected(
            int roundIndex, int themeIndex, int questionIndex,
            const QString &selectionMode, int minimumPrice, int maximumPrice,
            int priceStep, const QString &theme);

    protected:
      bool eventFilter(QObject *watched, QEvent *event) override;
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

      struct Player
      {
            QString name;
            int balance{};
            bool hasPassed{};
            QLabel *balanceLabel{};
      };

      void startPhaseTimer(GamePhase phase, unsigned int durationMs);
      void setProgressBarColor(GamePhase phase);
      void updateTimerProgress();
      void handlePhaseTimeout();
      void showAnswer();
      void returnToBoard();
      const Question &currentQuestion() const;
      void pickRandomQuestion();
      void displayContent(const QString &text, MediaType mediaType,
                          const QString &mediaPath);
      void fitDisplayedPixmap();
      void fitAnswerOptionsTable();
      void startReactionFlash();
      void stopReactionFlash();
      void openAnswerInput();
      void openTextAnswerDialog();
      void enableSelectAnswerInput();
      void enablePointAnswerInput();
      void handleSubmittedAnswer(const QString &answer);
      void handleAnswerDeclined();
      void applyAnswerResult(bool isCorrect, const QString &submittedAnswer);
      void updateBalanceLabel(Player &player);
      void populateAnswerOptions(const Question &question);
      void clearAnswerOptions();
      void highlightSelectAnswers(const Question &question);
      void resetAnswerInputState();
      bool parsePointAnswer(const QString &value, QPointF *point,
                            double *aspectRatio) const;

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
      std::vector<Player> m_players;
      QPointer<QInputDialog> m_answerDialog;
      QString m_submittedAnswer;
      std::optional<QPointF> m_correctPoint;
      std::optional<QPointF> m_submittedPoint;
      double m_correctPointAspectRatio{1.0};
      QRect m_displayedPixmapRect;
      bool m_pointInputEnabled{};
      GamePhase m_phase{GamePhase::PickingQuestion};
      int m_currentThemeIndex{-1};
      int m_currentQuestionIndex{-1};
      unsigned int m_phaseDuration{};
      bool m_answerResultApplied{};
      QPixmap m_displayedPixmap;
      QString m_questionFrameStyleSheet;
      int m_flashStep{};
};

#endif // GAMESCREEN_H
