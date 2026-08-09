#ifndef GAMESESSION_H
#define GAMESESSION_H

#include "gameconfig.h"
#include "gamecontent.h"
#include "playerstate.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QTimer>

#include <optional>

class GameSession : public QObject
{
      Q_OBJECT

    public:
      explicit GameSession(const Game &game, const GameConfig &config,
                           QObject *parent = nullptr);
      ~GameSession() override = default;

      bool addPlayer(const PlayerState &state);
      bool setPlayerConnected(const PlayerId &playerId, bool connected);
      bool setPlayerReady(const PlayerId &playerId, bool ready);
      bool updatePlayerProfile(const PlayerId &playerId,
                               const QByteArray &profilePng);
      PlayerId playerIdForToken(const PlayerToken &token) const;
      bool hasPlayer(const PlayerId &playerId) const;
      quint64 nextActionId(const PlayerId &playerId) const;
      QVector<PlayerState> players() const;
      const PlayerState *player(const PlayerId &playerId) const;
      PlayerState *player(const PlayerId &playerId);

      const Game &game() const { return m_game; }
      const GameConfig &config() const { return m_config; }
      QString sessionId() const { return m_sessionId; }
      SessionPhase phase() const { return m_phase; }
      quint64 phaseSequence() const { return m_phaseSequence; }
      quint64 questionSequence() const { return m_questionSequence; }
      quint64 boardSequence() const { return m_boardSequence; }
      PlayerId currentPicker() const { return m_currentPicker; }
      PlayerId answerOwner() const { return m_answerOwner; }
      PlayerId secretTarget() const { return m_secretTarget; }
      int roundIndex() const { return m_boardRound; }
      SecretWagerParameters secretWagerParameters() const;
      SecretWagerParameters finalWagerParameters(
            const PlayerId &playerId) const;
      bool isFinalWagerPending(const PlayerId &playerId) const;
      bool isPaused() const { return m_paused; }
      unsigned int remainingMs() const;
      BoardState boardState() const;
      SessionSnapshot snapshot() const;
      std::optional<QuestionPresentation> currentPresentation() const;
      QString secretSelectionMode() const;
      bool hasActiveQuestion() const;
      bool skipToRound(int roundIndex);
      void setReactionDecisionWindowMs(unsigned int durationMs);

    public slots:
      void startGame();
      void publishSnapshot();
      void selectQuestion(PlayerId playerId, int round, int theme,
                          int question, quint64 actionId = 0);
      void selectSecretTarget(PlayerId picker, PlayerId target,
                              quint64 questionSequence, quint64 actionId = 0);
      void submitSecretWager(PlayerId target, int amount,
                             quint64 questionSequence,
                             quint64 actionId = 0);
      void submitReaction(PlayerId playerId, quint64 questionSequence,
                          quint64 phaseSequence, quint64 actionId,
                          unsigned int elapsedMs);
      void submitReaction(PlayerId playerId, quint64 questionSequence,
                          quint64 phaseSequence, quint64 actionId,
                          unsigned int elapsedMs,
                          unsigned int comparisonElapsedMs);
      void submitAnswer(PlayerId playerId, quint64 questionSequence,
                        quint64 phaseSequence, quint64 actionId,
                        const AnswerSubmission &submission);
      void updateAnswerDraft(PlayerId playerId, quint64 questionSequence,
                             quint64 phaseSequence, quint64 actionId,
                             const AnswerSubmission &submission);
      void passQuestion(PlayerId playerId, quint64 questionSequence,
                        quint64 phaseSequence, quint64 actionId);
      void requestPause(PlayerId playerId, bool paused, quint64 actionId);
      void requestPause(PlayerId playerId, bool paused, quint64 actionId,
                        quint64 phaseSequence);
      void requestAppeal(PlayerId playerId, quint64 questionSequence,
                         quint64 actionId);
      void submitAppealVote(PlayerId playerId, quint64 appealId, bool accepted,
                            quint64 actionId);

    signals:
      void phaseStarted(const PhaseState &state);
      void phasePaused(SessionPhase phase, unsigned int remainingMs);
      void pauseChanged(bool paused, const PhaseState &state);
      void boardChanged(const BoardState &state);
      void questionStarted(const QuestionPresentation &presentation);
      void reactionOpened(const ReactionState &state);
      void reactionWinner(PlayerId playerId, unsigned int elapsedMs);
      void answerOwnerChanged(PlayerId playerId, unsigned int durationMs);
      void answerResult(const AnswerResult &result);
      void reactionResumed(unsigned int remainingMs, PlayerId excludedPlayerId);
      void answerRevealed(const AnswerReveal &reveal);
      void forAllProgress(quint64 questionSequence, int received, int expected);
      void forAllResult(const ForAllResult &result);
      void appealOpened(const AppealState &state);
      void appealFinished(const AppealResult &result);
      void roundStarted(int roundIndex, PlayerId picker);
      void pickerChanged(PlayerId playerId);
      void playersChanged(const QVector<PlayerState> &players);
      void snapshotReady(const SessionSnapshot &snapshot);
      void secretTargetsReady(quint64 questionSequence,
                              const QVector<PlayerState> &targets);
      void secretWagerPrompt(PlayerId target,
                             const SecretWagerParameters &parameters);
      void secretReady(quint64 questionSequence, PlayerId target);
      void actionRejected(PlayerId playerId, QString code, QString message);
      void gameFinished();

    private slots:
      void tick();

    private:
      struct ReactionClaim
      {
            unsigned int measuredMs{};
            unsigned int scoreMs{};
            quint64 actionId{};
      };

      struct ForAllAttempt
      {
            AnswerSubmission submission;
            bool correct{};
            bool submitted{};
            int amount{};
      };

      bool acceptAction(const PlayerId &playerId, quint64 actionId);
      bool requireSequence(const PlayerId &playerId, quint64 questionSequence,
                           quint64 phaseSequence) const;
      bool isConnected(const PlayerId &playerId) const;
      bool isEligible(const PlayerId &playerId) const;
      bool hasRemainingEligiblePlayers() const;
      bool isQuestionAvailable(int round, int theme, int question) const;
      void reject(const PlayerId &playerId, const QString &code,
                  const QString &message);
      void startPhase(SessionPhase phase, unsigned int durationMs,
                      const PlayerId &owner = {});
      void handleTimeout();
      void beginPicking();
      unsigned int roundIntroDuration() const;
      void selectRandomQuestion();
      void eliminateFinalTheme(const PlayerId &playerId, int theme,
                               quint64 actionId);
      void beginFinalWagering(int theme, int question);
      void startFinalQuestion();
      void advanceRoundOrFinish();
      void beginReaction();
      void decideReactionWinner();
      void beginForAllAnswering();
      void finishForAll();
      void submitAnswerInternal(const PlayerId &playerId,
                                const AnswerSubmission &submission,
                                bool timedOut);
      void applyNormalAnswer(const PlayerId &playerId,
                             const AnswerSubmission &submission,
                             bool correct, bool timedOut);
      void revealAnswer();
      void beginNextQuestion();
      void finishAppeal(bool accepted);
      void resetQuestionState();
      void chooseNextPickerIfNeeded();
      PlayerId chooseRandomConnectedPlayer() const;
      PlayerId nextConnectedPlayer(const PlayerId &playerId) const;
      bool currentRoundIsFinal() const;
      bool currentQuestionIsFinal() const;
      int remainingFinalThemeCount() const;
      QPair<int, int> remainingFinalQuestion() const;
      int finalWagerLimit(const PlayerId &playerId) const;
      Question *currentQuestion();
      const Question *currentQuestion() const;
      QuestionPresentation makePresentation() const;
      AnswerReveal makeReveal() const;
      bool validateSubmission(const Question &question,
                              const AnswerSubmission &submission) const;
      bool parsePointAnswer(const QString &value, QPointF *point,
                            double *aspectRatio) const;
      bool isCorrectSubmission(const Question &question,
                               const AnswerSubmission &submission) const;
      unsigned int questionReadingDuration() const;
      unsigned int questionAnswerDuration() const;
      unsigned int answerRevealDuration() const;
      unsigned int clampDuration(quint64 value) const;
      void emitPlayersChanged();
      void emitBoardChanged();
      QString cellKey(int round, int theme, int question) const;
      bool allForAllAnswersReceived() const;

      Game m_game;
      GameConfig m_config;
      QVector<PlayerState> m_players;
      QString m_sessionId;
      QTimer m_timer;
      QElapsedTimer m_clock;
      qint64 m_deadlineMs{};
      unsigned int m_remainingReactionMs{};
      unsigned int m_phaseDuration{};
      unsigned int m_remainingMs{};
      unsigned int m_reactionDecisionWindowMs{250U};
      bool m_paused{};
      bool m_started{};
      SessionPhase m_phase{SessionPhase::Lobby};
      quint64 m_phaseSequence{};
      quint64 m_questionSequence{};
      quint64 m_boardSequence{};
      mutable quint64 m_snapshotSequence{};
      quint64 m_appealSequence{};
      QSet<QString> m_usedCells;
      QHash<PlayerId, PlayerQuestionState> m_questionStates;
      QHash<PlayerId, quint64> m_lastActionIds;
      QHash<PlayerId, ReactionClaim> m_reactionClaims;
      QSet<PlayerId> m_forAllExpected;
      QHash<PlayerId, ForAllAttempt> m_forAllAttempts;
      QSet<PlayerId> m_finalWagerExpected;
      QHash<PlayerId, int> m_finalWagers;
      QHash<PlayerId, int> m_wrongAmounts;
      QHash<PlayerId, AnswerSubmission> m_answerDrafts;
      QHash<PlayerId, QString> m_submittedAnswers;
      QHash<PlayerId, bool> m_correctForCurrentQuestion;
      QHash<PlayerId, bool> m_votedAppeal;
      QSet<PlayerId> m_appealVoters;
      std::optional<AppealState> m_appeal;
      std::optional<QuestionPresentation> m_presentation;
      std::optional<AnswerReveal> m_lastReveal;
      PlayerId m_currentPicker;
      PlayerId m_answerOwner;
      PlayerId m_nextPicker;
      PlayerId m_secretTarget;
      int m_boardRound{};
      int m_announcedRound{-1};
      int m_currentRound{-1};
      int m_currentTheme{-1};
      int m_currentQuestion{-1};
      int m_secretWager{};
      bool m_finalQuestionActive{};
};

#endif // GAMESESSION_H
