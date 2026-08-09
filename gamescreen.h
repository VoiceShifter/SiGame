#ifndef GAMESCREEN_H
#define GAMESCREEN_H

#include "gamecontent.h"
#include "playerstate.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <optional>
#include <vector>

class MultiplayerClient;
class MultiplayerHost;
class QAudioOutput;
class QInputDialog;
class QLabel;
class QMediaPlayer;
class QResizeEvent;
class QPropertyAnimation;
class QTableWidget;
class QVideoWidget;

namespace Ui
{
class GameScreen;
}

enum class GameScreenMode
{
      SinglePlayer,
      MultiplayerHost,
      MultiplayerClient
};

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
      explicit GameScreen(
            signed int PlayerCount, const QString &GamepackPath,
            const QString &ProfilePicturePath, const QString &Nickname,
            int AnswerDuration, int QuestionDuration, int QuestionPickDuration,
            int AnswerWaitDuration, GameScreenMode mode,
            QWidget *parent = nullptr);
      ~GameScreen();

      void bindHost(MultiplayerHost *host);
      void bindClient(MultiplayerClient *client);
      void setLocalPlayerId(const PlayerId &playerId);
      void setPlayerStates(const QVector<PlayerState> &players);
      void setPicker(const PlayerId &playerId);
      void setAnswerOwner(const PlayerId &playerId);
      void setQuestionPermissions(bool canAnswer, bool canPass);
      void setAppealPermission(bool allowed);
      void setPausePermission(bool allowed);
      bool skipToRound(int roundIndex);

    public slots:
      void applyPlayers(const QVector<PlayerState> &players);
      void applyBoard(const BoardState &board);
      void applyPhase(const PhaseState &phase);
      void applyQuestion(const QuestionPresentation &question);
      void applyAnswerOwner(const PlayerId &playerId);
      void applyAnswerResult(const AnswerResult &result);
      void applyReveal(const AnswerReveal &reveal);
      void applyForAllResult(const ForAllResult &result);
      void applyAppeal(const AppealState &appeal);
      void applyAppealResult(const AppealResult &result);
      void applyPause(bool paused, SessionPhase phase,
                      unsigned int remainingMs);
      void applySnapshot(const SessionSnapshot &snapshot);
      void applySecretTargets(const QVector<PlayerState> &targets);
      void applyWagerPrompt(const SecretWagerParameters &parameters);

    signals:
      void questionSelected(int themeIndex, int questionIndex);
      void questionPickRequested(int roundIndex, int themeIndex,
                                 int questionIndex);
      void secretTargetRequested(PlayerId targetId);
      void secretWagerSubmitted(int amount);
      void reactionClaimRequested(unsigned int elapsedMs);
      void answerDraftChanged(const QString &answer);
      void answerSubmitted(const AnswerSubmission &submission);
      void passRequested();
      void pauseRequested(bool paused);
      void appealRequested();
      void appealVoteSubmitted(bool accepted);
      void returnToMenuRequested();
      void incorrectAnswerSubmitted(PlayerId playerId, const QString &answer);
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
      using GamePhase = SessionPhase;

      enum class PlayerGlow
      {
            None,
            Reaction,
            Correct,
            Incorrect
      };

      struct Player
      {
            QString name;
            int balance{};
            bool hasPassed{};
            QLabel *avatarLabel{};
            QLabel *balanceLabel{};
            QLabel *answerBubble{};
            PlayerGlow glow{PlayerGlow::None};
            int correctAnswers{};
            int wrongAnswers{};
      };

      void startPhaseTimer(GamePhase phase, unsigned int durationMs);
      void setProgressBarColor(GamePhase phase);
      void updateTimerProgress();
      void handlePhaseTimeout();
      void pauseSinglePlayer();
      void resumeSinglePlayer();
      void showAnswer();
      void returnToBoard();
      void buildBoard(int roundIndex);
      void setupRoundIntroPage();
      void showRoundIntro(int roundIndex, unsigned int durationMs,
                          unsigned int remainingMs);
      void startRoundIntroScroll(unsigned int durationMs,
                                 unsigned int remainingMs);
      void finishSingleRoundIntro();
      unsigned int roundIntroDuration(int roundIndex) const;
      bool isFinalRound(int roundIndex) const;
      bool hasAvailableQuestions() const;
      void advanceSinglePlayerRound();
      void eliminateSingleFinalTheme(int themeIndex);
      bool beginSingleFinalWagersIfReady();
      void beginSingleFinalWagers(int themeIndex, int questionIndex);
      void promptSingleFinalWager();
      void submitSingleFinalWager(int amount);
      void showSingleFinalQuestion();
      void beginSingleFinalAnswers();
      void promptSingleFinalAnswer();
      void finishSingleFinalAnswer(bool correct);
      int singlePlayerWagerLimit(int playerIndex) const;
      const Question &currentQuestion() const;
      void pickRandomQuestion();
      void displayContent(const QString &text, MediaType mediaType,
                          const QString &mediaPath,
                          unsigned int mediaDurationMs);
      void finishMediaDisplay();
      void stopMediaPlayback();
      void pauseMediaPlayback();
      void resumeMediaPlayback();
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
      void applyAuthoritativeAnswerResult(const AnswerResult &result);
      void updateBalanceLabel(Player &player);
      void populateAnswerOptions(const Question &question);
      void clearAnswerOptions();
      void highlightSelectAnswers(const Question &question);
      void resetAnswerInputState();
      bool parsePointAnswer(const QString &value, QPointF *point,
                            double *aspectRatio) const;
      void applyNetworkQuestion(const QuestionPresentation &presentation);
      void applyNetworkBoard(const BoardState &board);
      void setupAppealPage();
      void setupGameFinishedPage();
      void showGameFinished();
      void fitAppealPixmaps();
      QString displayAnswerText(const QString &answer) const;
      void submitAppealVote(bool accepted);
      void applyReactionWinner(const PlayerId &playerId);
      QLabel *createAnswerBubble();
      void showNetworkAnswerBubble(const AnswerResult &result);
      void positionAnswerBubbles();
      void clearAnswerBubbles();
      void applyPlayerGlow(QLabel *avatar, PlayerGlow glow,
                           bool connected = true);
      void setSinglePlayerGlow(PlayerGlow glow, bool clearAfterDelay);
      void rebuildNetworkPlayerCards();
      void clearNetworkPlayerCards();
      void setNetworkControls();
      void connectHostSignals(MultiplayerHost *host);
      void connectClientSignals(MultiplayerClient *client);
      void emitNetworkAnswer(const AnswerSubmission &submission);
      quint64 nextLocalActionId();
      void setNetworkPhaseTimer(const PhaseState &phase);
      static void deleteLayoutItems(QLayout *layout);

      static constexpr unsigned int AnswerRevealDuration{5000U};
      static constexpr unsigned int PlayerResultGlowDuration{3000U};

      Ui::GameScreen *ui;
      QTimer *m_tickTimer;
      QElapsedTimer *m_globalTimer;
      QPropertyAnimation *m_progressAnimation;
      QPropertyAnimation *m_roundIntroAnimation;
      QTimer *m_flashTimer;
      QTimer *m_mediaDurationTimer;
      QMediaPlayer *m_mediaPlayer{};
      QAudioOutput *m_audioOutput{};
      QVideoWidget *m_videoWidget{};
      QLabel *m_boardStatusLabel{};
      QElapsedTimer m_reactionElapsedTimer;
      QElapsedTimer m_mediaDurationElapsedTimer;
      unsigned int m_mediaRemainingMs{};
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
      bool m_mediaPausedByGame{};
      bool m_mediaDurationPaused{};
      MediaType m_activeMediaType{MediaType::None};
      SessionPhase m_phase{SessionPhase::PickingQuestion};
      int m_boardRoundIndex{-1};
      int m_currentThemeIndex{-1};
      int m_currentQuestionIndex{-1};
      int m_singleFinalEliminatorIndex{};
      int m_singleFinalWagerPlayerIndex{};
      int m_singleFinalAnswerPlayerIndex{};
      unsigned int m_phaseDuration{};
      unsigned int m_singlePlayerRemainingMs{};
      bool m_answerResultApplied{};
      bool m_singleFinalQuestionActive{};
      bool m_singlePlayerPaused{};
      bool m_singlePlayerTimerWasActive{};
      bool m_singlePlayerFlashWasActive{};
      bool m_singlePlayerAnswerWasEnabled{};
      bool m_singlePlayerPassWasEnabled{};
      bool m_singlePlayerAnswerDialogWasVisible{};
      QPointer<QWidget> m_pageBeforePause;
      std::vector<int> m_singleFinalWagers;
      std::vector<bool> m_singleFinalCorrect;
      QPixmap m_displayedPixmap;
      QPixmap m_appealQuestionPixmap;
      QPixmap m_appealCorrectAnswerPixmap;
      QString m_questionFrameStyleSheet;
      int m_flashStep{};

      GameScreenMode m_mode{GameScreenMode::SinglePlayer};
      PlayerId m_localPlayerId;
      PlayerId m_pickerId;
      PlayerId m_answerOwnerId;
      QVector<PlayerState> m_networkPlayers;
      QVector<PlayerId> m_networkCardOrder;
      QHash<PlayerId, QLabel *> m_networkAvatarLabels;
      QHash<PlayerId, QLabel *> m_networkNameLabels;
      QHash<PlayerId, QLabel *> m_networkBalanceLabels;
      QHash<PlayerId, QLabel *> m_networkAnswerLabels;
      QHash<PlayerId, QByteArray> m_networkCardProfiles;
      QHash<PlayerId, PlayerGlow> m_playerGlows;
      BoardState m_networkBoard;
      PhaseState m_networkPhase;
      std::optional<Question> m_networkQuestion;
      quint64 m_networkQuestionSequence{};
      quint64 m_networkPhaseSequence{};
      quint64 m_localActionId{1};
      bool m_networkAnswerSubmitted{};
      bool m_networkAnswerInputOpened{};
      bool m_networkReactionClaimed{};
      bool m_networkPaused{};
      bool m_canPause{};
      bool m_canAnswer{};
      bool m_canPass{};
      bool m_canAppeal{};
      bool m_secretTargetSelection{};
      bool m_forAllAnswering{};
      bool m_appealVoteSubmitted{};
      PlayerId m_appealAppellant;
      quint64 m_appealId{};
      QWidget *m_roundIntroPage{};
      QWidget *m_roundIntroViewport{};
      QLabel *m_roundIntroTitleLabel{};
      QLabel *m_roundIntroTopicLabel{};
      QWidget *m_appealPage{};
      QLabel *m_appealQuestionLabel{};
      QLabel *m_appealQuestionMediaLabel{};
      QLabel *m_appealSubmittedHeadingLabel{};
      QLabel *m_appealSubmittedLabel{};
      QLabel *m_appealCorrectAnswerLabel{};
      QLabel *m_appealCorrectAnswerMediaLabel{};
      QWidget *m_gameFinishedPage{};
      QTableWidget *m_gameFinishedTable{};
      MultiplayerHost *m_host{};
      MultiplayerClient *m_client{};
};

#endif // GAMESCREEN_H
