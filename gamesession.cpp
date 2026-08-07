#include "gamesession.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QString answerText(const AnswerSubmission &submission)
{
      if (submission.answerType == AnswerType::Select)
      {
            return submission.optionId;
      }
      if (submission.answerType == AnswerType::Point && submission.hasPoint)
      {
            return QStringLiteral("%1,%2")
                  .arg(submission.point.x(), 0, 'f', 6)
                  .arg(submission.point.y(), 0, 'f', 6);
      }
      return submission.answer;
}

bool containsPlayer(const QVector<PlayerState> &players, const PlayerId &id)
{
      return std::any_of(players.cbegin(), players.cend(),
                         [&id](const PlayerState &player)
                         { return player.id == id; });
}
} // namespace

GameSession::GameSession(const Game &game, const GameConfig &config,
                         QObject *parent)
      : QObject(parent), m_game(game), m_config(config),
        m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
      m_config.maxPlayers = std::clamp(m_config.maxPlayers, 1, 5);
      m_timer.setInterval(50);
      connect(&m_timer, &QTimer::timeout, this, &GameSession::tick);
      m_clock.start();
}

bool GameSession::addPlayer(const PlayerState &state)
{
      if (state.id.isEmpty() || hasPlayer(state.id) ||
          m_players.size() >= m_config.maxPlayers ||
          (!state.token.isEmpty() && !playerIdForToken(state.token).isEmpty()))
      {
            return false;
      }
      PlayerState copy = state;
      copy.connected = state.connected;
      copy.ready = state.ready;
      m_players.push_back(copy);
      if (m_questionSequence != 0 &&
          (m_phase == SessionPhase::ReadingQuestion ||
           m_phase == SessionPhase::WaitingForReaction ||
           m_phase == SessionPhase::Answering ||
           m_phase == SessionPhase::ForAllAnswering))
      {
            m_questionStates.insert(copy.id,
                                    {m_questionSequence, false, false, false,
                                     false, 0});
            if (m_phase == SessionPhase::ForAllAnswering && copy.connected &&
                copy.ready)
            {
                  m_forAllExpected.insert(copy.id);
                  m_forAllAttempts.insert(copy.id, {});
            }
      }
      emitPlayersChanged();
      return true;
}

bool GameSession::setPlayerConnected(const PlayerId &playerId, bool connected)
{
      PlayerState *state = player(playerId);
      if (state == nullptr || state->connected == connected)
      {
            return state != nullptr;
      }
      state->connected = connected;
      emitPlayersChanged();
      return true;
}

bool GameSession::setPlayerReady(const PlayerId &playerId, bool ready)
{
      PlayerState *state = player(playerId);
      if (state == nullptr)
      {
            return false;
      }
      state->ready = ready;
      if (ready && m_phase == SessionPhase::ForAllAnswering &&
          m_questionSequence != 0 && !m_forAllExpected.contains(playerId))
      {
            m_forAllExpected.insert(playerId);
            m_forAllAttempts.insert(playerId, {});
      }
      emitPlayersChanged();
      return true;
}

bool GameSession::updatePlayerProfile(const PlayerId &playerId,
                                      const QByteArray &profilePng)
{
      PlayerState *state = player(playerId);
      if (state == nullptr)
      {
            return false;
      }
      state->profilePng = profilePng;
      emitPlayersChanged();
      return true;
}

PlayerId GameSession::playerIdForToken(const PlayerToken &token) const
{
      if (token.isEmpty())
      {
            return {};
      }
      for (const PlayerState &state : m_players)
      {
            if (state.token == token)
            {
                  return state.id;
            }
      }
      return {};
}

bool GameSession::hasPlayer(const PlayerId &playerId) const
{
      return player(playerId) != nullptr;
}

QVector<PlayerState> GameSession::players() const { return m_players; }

const PlayerState *GameSession::player(const PlayerId &playerId) const
{
      for (const PlayerState &state : m_players)
      {
            if (state.id == playerId)
            {
                  return &state;
            }
      }
      return nullptr;
}

PlayerState *GameSession::player(const PlayerId &playerId)
{
      for (PlayerState &state : m_players)
      {
            if (state.id == playerId)
            {
                  return &state;
            }
      }
      return nullptr;
}

unsigned int GameSession::remainingMs() const
{
      if (m_paused)
      {
            return m_remainingMs;
      }
      if (!m_timer.isActive())
      {
            return m_remainingMs;
      }
      const qint64 remaining = m_deadlineMs - m_clock.elapsed();
      return static_cast<unsigned int>(std::max<qint64>(0, remaining));
}

BoardState GameSession::boardState() const
{
      BoardState state;
      state.boardSequence = m_boardSequence;
      state.round = 0;
      if (m_game.rounds.empty())
      {
            return state;
      }
      const Round &round = m_game.rounds.front();
      for (int theme = 0; theme < static_cast<int>(round.themes.size()); ++theme)
      {
            const Theme &currentTheme = round.themes[static_cast<std::size_t>(theme)];
            for (int question = 0;
                 question < static_cast<int>(currentTheme.questions.size());
                 ++question)
            {
                  state.cells.push_back({0, theme, question,
                                         m_usedCells.contains(cellKey(0, theme,
                                                                       question))});
            }
      }
      return state;
}

SessionSnapshot GameSession::snapshot() const
{
      SessionSnapshot state;
      state.snapshotSequence = ++m_snapshotSequence;
      state.sessionId = m_sessionId;
      state.phase = {m_phase,
                     m_phaseSequence,
                     m_questionSequence,
                     m_phaseDuration,
                     remainingMs(),
                     m_phase == SessionPhase::PickingQuestion
                           ? m_currentPicker
                           : m_answerOwner,
                     m_paused};
      state.board = boardState();
      state.currentPicker = m_currentPicker;
      state.answerOwner = m_answerOwner;
      state.players = m_players;
      state.paused = m_paused;
      state.question = m_presentation;
      if (m_phase == SessionPhase::ShowingAnswer ||
          m_phase == SessionPhase::AppealVoting)
      {
            state.reveal = m_lastReveal;
      }
      if (m_phase == SessionPhase::AppealVoting)
      {
            state.appeal = m_appeal;
      }
      return state;
}

std::optional<QuestionPresentation> GameSession::currentPresentation() const
{
      return m_presentation;
}

QString GameSession::secretSelectionMode() const
{
      const Question *question = currentQuestion();
      if (question == nullptr || !question->secretParameters.has_value())
      {
            return {};
      }
      return question->secretParameters->selectionMode;
}

SecretWagerParameters GameSession::secretWagerParameters() const
{
      SecretWagerParameters parameters;
      const Question *question = currentQuestion();
      if (question != nullptr && question->secretParameters.has_value())
      {
            parameters.minimum = question->secretParameters->price.minimum;
            parameters.maximum = question->secretParameters->price.maximum;
            parameters.step = question->secretParameters->price.step;
            parameters.theme = question->secretParameters->theme;
      }
      return parameters;
}

bool GameSession::hasActiveQuestion() const
{
      return currentQuestion() != nullptr && m_questionSequence != 0;
}

void GameSession::publishSnapshot()
{
      emit snapshotReady(snapshot());
}

void GameSession::startGame()
{
      if (m_started)
      {
            return;
      }
      if (std::none_of(m_players.cbegin(), m_players.cend(),
                       [](const PlayerState &state)
                       { return state.connected && state.ready; }))
      {
            return;
      }
      m_started = true;
      beginPicking();
}

void GameSession::selectQuestion(PlayerId playerId, int round, int theme,
                                 int question, quint64 actionId)
{
      if (m_phase != SessionPhase::PickingQuestion)
      {
            reject(playerId, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("A question cannot be selected now"));
            return;
      }
      if (playerId != m_currentPicker || !isConnected(playerId))
      {
            reject(playerId, QStringLiteral("NOT_YOUR_TURN"),
                   QStringLiteral("Only the current picker may select a question"));
            return;
      }
      if (!isQuestionAvailable(round, theme, question))
      {
            reject(playerId, QStringLiteral("QUESTION_UNAVAILABLE"),
                   QStringLiteral("The selected question is unavailable"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }

      m_usedCells.insert(cellKey(round, theme, question));
      ++m_boardSequence;
      ++m_questionSequence;
      m_currentRound = round;
      m_currentTheme = theme;
      m_currentQuestion = question;
      m_answerOwner.clear();
      m_secretTarget.clear();
      m_nextPicker.clear();
      m_secretWager = 0;
      m_presentation.reset();
      m_lastReveal.reset();
      resetQuestionState();
      emitBoardChanged();

      const Question *selected = currentQuestion();
      if (selected == nullptr)
      {
            reject(playerId, QStringLiteral("INTERNAL_ERROR"),
                   QStringLiteral("The selected question is unavailable"));
            return;
      }
      if (selected->type == QuestionType::SecretPublicPrice)
      {
            m_presentation.reset();
            QVector<PlayerState> targets;
            const QString selectionMode = selected->secretParameters.has_value()
                                                ? selected->secretParameters->selectionMode
                                                : QString();
            for (const PlayerState &state : m_players)
            {
                  if (!isConnected(state.id) ||
                      (selectionMode.compare(QStringLiteral("exceptCurrent"),
                                             Qt::CaseInsensitive) == 0 &&
                       state.id == playerId))
                  {
                        continue;
                  }
                  targets.push_back(state);
            }
            startPhase(SessionPhase::SecretTargetSelection,
                       m_config.questionPickDurationMs, playerId);
            emit secretTargetsReady(m_questionSequence, targets);
            return;
      }

      m_presentation = makePresentation();
      emit questionStarted(*m_presentation);
      startPhase(SessionPhase::ReadingQuestion, m_config.questionDurationMs);
}

void GameSession::selectSecretTarget(PlayerId picker, PlayerId target,
                                     quint64 questionSequence,
                                     quint64 actionId)
{
      if (m_phase != SessionPhase::SecretTargetSelection)
      {
            reject(picker, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("A secret target cannot be selected now"));
            return;
      }
      if (!requireSequence(picker, questionSequence, m_phaseSequence))
      {
            reject(picker, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The secret question sequence is stale"));
            return;
      }
      if (picker != m_currentPicker || !isConnected(picker))
      {
            reject(picker, QStringLiteral("NOT_YOUR_TURN"),
                   QStringLiteral("Only the picker may choose a target"));
            return;
      }
      const Question *question = currentQuestion();
      if (question == nullptr || !isConnected(target) || target == picker)
      {
            reject(picker, QStringLiteral("INVALID_TARGET"),
                   QStringLiteral("The selected target is invalid"));
            return;
      }
      if (question->secretParameters.has_value() &&
          question->secretParameters->selectionMode.compare(
                QStringLiteral("exceptCurrent"), Qt::CaseInsensitive) == 0 &&
          target == picker)
      {
            reject(picker, QStringLiteral("INVALID_TARGET"),
                   QStringLiteral("The picker cannot be the target"));
            return;
      }
      if (!acceptAction(picker, actionId))
      {
            return;
      }
      m_secretTarget = target;
      SecretWagerParameters parameters;
      if (question->secretParameters.has_value())
      {
            parameters.minimum = question->secretParameters->price.minimum;
            parameters.maximum = question->secretParameters->price.maximum;
            parameters.step = question->secretParameters->price.step;
            parameters.theme = question->secretParameters->theme;
      }
      startPhase(SessionPhase::SecretWager, questionAnswerDuration(), target);
      emit secretWagerPrompt(target, parameters);
}

void GameSession::submitSecretWager(PlayerId target, int amount,
                                    quint64 questionSequence,
                                    quint64 actionId)
{
      if (m_phase != SessionPhase::SecretWager || target != m_secretTarget)
      {
            reject(target, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("A wager cannot be submitted now"));
            return;
      }
      if (!requireSequence(target, questionSequence, m_phaseSequence))
      {
            reject(target, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The secret question sequence is stale"));
            return;
      }
      const Question *question = currentQuestion();
      if (question == nullptr || !question->secretParameters.has_value())
      {
            reject(target, QStringLiteral("INVALID_WAGER"),
                   QStringLiteral("The question has no wager parameters"));
            return;
      }
      const NumberSet &range = question->secretParameters->price;
      if (range.minimum == 0 && range.maximum == 0 && range.step == 0)
      {
            qInfo() << "Secret question permits only a zero wager";
      }
      const bool inRange = amount >= range.minimum && amount <= range.maximum;
      const bool validStep =
            (range.step == 0 && range.minimum == 0 && range.maximum == 0) ||
            (range.step > 0 && (amount - range.minimum) % range.step == 0);
      if (!inRange || !validStep)
      {
            reject(target, QStringLiteral("INVALID_WAGER"),
                   QStringLiteral("The wager is outside the allowed range"));
            return;
      }
      if (!acceptAction(target, actionId))
      {
            return;
      }
      m_secretWager = amount;
      m_presentation = makePresentation();
      m_answerOwner = target;
      if (m_presentation.has_value())
      {
            m_presentation->answerOwner = target;
      }
      emit secretReady(m_questionSequence, target);
      if (m_presentation.has_value())
      {
            emit questionStarted(*m_presentation);
      }
      startPhase(SessionPhase::ReadingQuestion, m_config.questionDurationMs,
                 target);
}

void GameSession::submitReaction(PlayerId playerId, quint64 questionSequence,
                                 quint64 phaseSequence, quint64 actionId,
                                 unsigned int elapsedMs)
{
      submitReaction(playerId, questionSequence, phaseSequence, actionId,
                     elapsedMs, elapsedMs);
}

void GameSession::submitReaction(PlayerId playerId, quint64 questionSequence,
                                 quint64 phaseSequence, quint64 actionId,
                                 unsigned int elapsedMs,
                                 unsigned int comparisonElapsedMs)
{
      if (m_phase != SessionPhase::WaitingForReaction)
      {
            reject(playerId, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("The reaction window is closed"));
            return;
      }
      if (!requireSequence(playerId, questionSequence, phaseSequence))
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The reaction sequence is stale"));
            return;
      }
      if (!isConnected(playerId) || !isEligible(playerId))
      {
            reject(playerId, QStringLiteral("PLAYER_INELIGIBLE"),
                   QStringLiteral("This player cannot react to the question"));
            return;
      }
      if (elapsedMs > m_config.answerWaitDurationMs)
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The reaction duration is too large"));
            return;
      }
      if (m_reactionClaims.contains(playerId))
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("This player already claimed the reaction"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      m_reactionClaims.insert(playerId,
                              {elapsedMs, comparisonElapsedMs, actionId});
      const bool allEligiblePlayersClaimed = std::all_of(
            m_players.cbegin(), m_players.cend(),
            [this](const PlayerState &state)
            {
                  return !isConnected(state.id) || !isEligible(state.id) ||
                         m_reactionClaims.contains(state.id);
            });
      if (allEligiblePlayersClaimed)
      {
            decideReactionWinner();
      }
}

void GameSession::submitAnswer(PlayerId playerId, quint64 questionSequence,
                               quint64 phaseSequence, quint64 actionId,
                               const AnswerSubmission &submission)
{
      if (m_phase != SessionPhase::Answering &&
          m_phase != SessionPhase::ForAllAnswering)
      {
            reject(playerId, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("An answer cannot be submitted now"));
            return;
      }
      if (!requireSequence(playerId, questionSequence, phaseSequence))
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The answer sequence is stale"));
            return;
      }
      if (!isConnected(playerId))
      {
            reject(playerId, QStringLiteral("PLAYER_INELIGIBLE"),
                   QStringLiteral("This player is disconnected"));
            return;
      }
      if (m_phase == SessionPhase::Answering && playerId != m_answerOwner)
      {
            reject(playerId, QStringLiteral("NOT_YOUR_TURN"),
                   QStringLiteral("Only the answer owner may answer"));
            return;
      }
      if (m_phase == SessionPhase::ForAllAnswering &&
          (!m_forAllExpected.contains(playerId) ||
           m_forAllAttempts.value(playerId).submitted))
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("This player already answered"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      submitAnswerInternal(playerId, submission, false);
}

void GameSession::updateAnswerDraft(
      PlayerId playerId, quint64 questionSequence, quint64 phaseSequence,
      quint64 actionId, const AnswerSubmission &submission)
{
      if (m_phase != SessionPhase::Answering &&
          m_phase != SessionPhase::ForAllAnswering)
      {
            reject(playerId, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("An answer draft cannot be updated now"));
            return;
      }
      if (!requireSequence(playerId, questionSequence, phaseSequence))
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The answer draft sequence is stale"));
            return;
      }
      const Question *question = currentQuestion();
      const bool allowed =
            m_phase == SessionPhase::Answering
                  ? playerId == m_answerOwner && isConnected(playerId)
                  : isConnected(playerId) &&
                          m_forAllExpected.contains(playerId) &&
                          !m_forAllAttempts.value(playerId).submitted;
      if (!allowed || question == nullptr ||
          !validateSubmission(*question, submission))
      {
            reject(playerId, QStringLiteral("PLAYER_INELIGIBLE"),
                   QStringLiteral("This player cannot update the answer draft"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      m_answerDrafts.insert(playerId, submission);
}

void GameSession::passQuestion(PlayerId playerId, quint64 questionSequence,
                               quint64 phaseSequence, quint64 actionId)
{
      if (m_phase != SessionPhase::ReadingQuestion &&
          m_phase != SessionPhase::WaitingForReaction &&
          m_phase != SessionPhase::Answering &&
          m_phase != SessionPhase::ForAllAnswering)
      {
            reject(playerId, QStringLiteral("WRONG_PHASE"),
                   QStringLiteral("Pass is not available now"));
            return;
      }
      if (!requireSequence(playerId, questionSequence, phaseSequence))
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The question sequence is stale"));
            return;
      }
      if (!isConnected(playerId))
      {
            reject(playerId, QStringLiteral("PLAYER_INELIGIBLE"),
                   QStringLiteral("This player is disconnected"));
            return;
      }
      if (m_questionStates.value(playerId).passed)
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("This player already passed"));
            return;
      }
      if (!isEligible(playerId))
      {
            reject(playerId, QStringLiteral("PLAYER_INELIGIBLE"),
                   QStringLiteral("This player has already acted"));
            return;
      }
      if (m_phase == SessionPhase::Answering && playerId == m_answerOwner)
      {
            reject(playerId, QStringLiteral("NOT_YOUR_TURN"),
                   QStringLiteral("The answer owner cannot pass"));
            return;
      }
      if (m_phase == SessionPhase::ForAllAnswering)
      {
            if (!acceptAction(playerId, actionId))
            {
                  return;
            }
            PlayerQuestionState &state = m_questionStates[playerId];
            state.submitted = true;
            state.passed = true;
            if (PlayerState *playerState = player(playerId))
            {
                  playerState->hasPassed = true;
                  playerState->hasAnsweredForAll = true;
            }
            ForAllAttempt noAnswer;
            noAnswer.submitted = true;
            m_forAllAttempts.insert(playerId, noAnswer);
            emitPlayersChanged();
            int received = 0;
            for (const PlayerId &id : m_forAllExpected)
            {
                  if (m_forAllAttempts.value(id).submitted)
                  {
                        ++received;
                  }
            }
            emit forAllProgress(m_questionSequence, received,
                                m_forAllExpected.size());
            if (allForAllAnswersReceived())
            {
                  finishForAll();
            }
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      m_questionStates[playerId].passed = true;
      if (PlayerState *playerState = player(playerId))
      {
            playerState->hasPassed = true;
      }
      emitPlayersChanged();
      if (!hasRemainingEligiblePlayers())
      {
            revealAnswer();
      }
}

void GameSession::requestPause(PlayerId playerId, bool paused,
                               quint64 actionId)
{
      requestPause(playerId, paused, actionId, m_phaseSequence);
}

void GameSession::requestPause(PlayerId playerId, bool paused,
                               quint64 actionId, quint64 phaseSequence)
{
      if (m_phase != SessionPhase::PickingQuestion &&
          m_phase != SessionPhase::ReadingQuestion)
      {
            reject(playerId, QStringLiteral("PAUSE_NOT_ALLOWED"),
                   QStringLiteral("Pause is not allowed in this phase"));
            return;
      }
      if (phaseSequence != m_phaseSequence)
      {
            reject(playerId, QStringLiteral("STALE_SEQUENCE"),
                   QStringLiteral("The phase sequence is stale"));
            return;
      }
      if (!isConnected(playerId) || !acceptAction(playerId, actionId))
      {
            if (isConnected(playerId))
            {
                  reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                         QStringLiteral("The action was already received"));
            }
            return;
      }
      if (paused == m_paused)
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("The session is already in that pause state"));
            return;
      }
      if (paused)
      {
            m_remainingMs = remainingMs();
            m_deadlineMs = m_clock.elapsed() + m_remainingMs;
            m_paused = true;
            m_timer.stop();
            PhaseState state{m_phase,       m_phaseSequence,
                             m_questionSequence, m_phaseDuration,
                             m_remainingMs,
                             m_phase == SessionPhase::PickingQuestion
                                   ? m_currentPicker
                                   : m_answerOwner,
                             true};
            emit phasePaused(m_phase, m_remainingMs);
            emit pauseChanged(true, state);
      }
      else
      {
            const unsigned int remaining = m_remainingMs;
            startPhase(m_phase, remaining,
                       m_phase == SessionPhase::PickingQuestion
                             ? m_currentPicker
                             : m_answerOwner);
            emit pauseChanged(false,
                              {m_phase,
                               m_phaseSequence,
                               m_questionSequence,
                               m_phaseDuration,
                               m_remainingMs,
                               m_phase == SessionPhase::PickingQuestion
                                     ? m_currentPicker
                                     : m_answerOwner,
                               false});
      }
}

void GameSession::requestAppeal(PlayerId playerId, quint64 questionSequence,
                                quint64 actionId)
{
      if (m_phase != SessionPhase::ShowingAnswer ||
          questionSequence != m_questionSequence)
      {
            reject(playerId, QStringLiteral("APPEAL_NOT_ALLOWED"),
                   QStringLiteral("An appeal cannot be requested now"));
            return;
      }
      const PlayerState *state = player(playerId);
      if (state == nullptr || !state->connected || !state->mayAppeal ||
          m_appeal.has_value())
      {
            reject(playerId, QStringLiteral("APPEAL_NOT_ALLOWED"),
                   QStringLiteral("This player cannot appeal the answer"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      AppealState appeal;
      appeal.appealId = ++m_appealSequence;
      appeal.questionSequence = m_questionSequence;
      appeal.appellant = playerId;
      appeal.submitted = m_submittedAnswers.value(playerId);
      appeal.durationMs = m_config.appealDurationMs;
      for (const PlayerState &candidate : m_players)
      {
            if (candidate.connected && candidate.id != playerId)
            {
                  appeal.voters.push_back(candidate.id);
            }
      }
      m_appeal = appeal;
      m_appealVoters.clear();
      for (const PlayerId &voter : appeal.voters)
      {
            m_appealVoters.insert(voter);
      }
      m_votedAppeal.clear();
      startPhase(SessionPhase::AppealVoting, m_config.appealDurationMs,
                 playerId);
      emit appealOpened(*m_appeal);
      if (m_appealVoters.isEmpty())
      {
            finishAppeal(true);
      }
}

void GameSession::submitAppealVote(PlayerId playerId, quint64 appealId,
                                   bool accepted, quint64 actionId)
{
      if (m_phase != SessionPhase::AppealVoting || !m_appeal.has_value() ||
          m_appeal->appealId != appealId ||
          !m_appealVoters.contains(playerId))
      {
            reject(playerId, QStringLiteral("APPEAL_NOT_ALLOWED"),
                   QStringLiteral("This player is not a required voter"));
            return;
      }
      if (m_votedAppeal.contains(playerId))
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("This player already voted"));
            return;
      }
      if (!acceptAction(playerId, actionId))
      {
            return;
      }
      m_votedAppeal.insert(playerId, true);
      m_appeal->votes.insert(playerId, accepted);
      if (!accepted)
      {
            finishAppeal(false);
            return;
      }
      if (m_votedAppeal.size() == m_appealVoters.size())
      {
            finishAppeal(true);
      }
}

void GameSession::tick()
{
      if (m_paused || !m_timer.isActive())
      {
            return;
      }
      const qint64 remaining = m_deadlineMs - m_clock.elapsed();
      if (remaining > 0)
      {
            return;
      }
      m_timer.stop();
      handleTimeout();
}

bool GameSession::acceptAction(const PlayerId &playerId, quint64 actionId)
{
      if (actionId == 0)
      {
            return true;
      }
      const quint64 previous = m_lastActionIds.value(playerId, 0);
      if (actionId <= previous)
      {
            reject(playerId, QStringLiteral("DUPLICATE_ACTION"),
                   QStringLiteral("The action ID was already received"));
            return false;
      }
      m_lastActionIds.insert(playerId, actionId);
      return true;
}

bool GameSession::requireSequence(const PlayerId &, quint64 questionSequence,
                                  quint64 phaseSequence) const
{
      return questionSequence == m_questionSequence &&
             phaseSequence == m_phaseSequence;
}

bool GameSession::isConnected(const PlayerId &playerId) const
{
      const PlayerState *state = player(playerId);
      return state != nullptr && state->connected && state->ready;
}

bool GameSession::isEligible(const PlayerId &playerId) const
{
      const PlayerState *state = player(playerId);
      if (state == nullptr || !state->connected)
      {
            return false;
      }
      const PlayerQuestionState questionState =
            m_questionStates.value(playerId);
      return !questionState.passed && !questionState.answeredIncorrectly;
}

bool GameSession::hasRemainingEligiblePlayers() const
{
      return std::any_of(m_players.cbegin(), m_players.cend(),
                         [this](const PlayerState &state)
                         { return isEligible(state.id); });
}

bool GameSession::isQuestionAvailable(int round, int theme, int question) const
{
      if (round != 0 || round < 0 || theme < 0 || question < 0 ||
          m_game.rounds.empty())
      {
            return false;
      }
      const Round &currentRound = m_game.rounds.front();
      if (theme >= static_cast<int>(currentRound.themes.size()))
      {
            return false;
      }
      const Theme &currentTheme = currentRound.themes[static_cast<std::size_t>(theme)];
      if (question >= static_cast<int>(currentTheme.questions.size()))
      {
            return false;
      }
      return !m_usedCells.contains(cellKey(round, theme, question));
}

void GameSession::reject(const PlayerId &playerId, const QString &code,
                         const QString &message)
{
      emit actionRejected(playerId, code, message);
}

void GameSession::startPhase(SessionPhase phase, unsigned int durationMs,
                             const PlayerId &owner)
{
      m_phase = phase;
      m_phaseDuration = durationMs;
      m_remainingMs = durationMs;
      m_deadlineMs = m_clock.elapsed() + durationMs;
      m_paused = false;
      ++m_phaseSequence;
      const PhaseState state{m_phase,
                             m_phaseSequence,
                             m_questionSequence,
                             durationMs,
                             durationMs,
                             owner,
                             false};
      if (durationMs == 0)
      {
            m_timer.stop();
      }
      else
      {
            m_timer.start();
      }
      emit phaseStarted(state);
}

void GameSession::handleTimeout()
{
      switch (m_phase)
      {
      case SessionPhase::Lobby:
            break;
      case SessionPhase::PickingQuestion:
            selectRandomQuestion();
            break;
      case SessionPhase::SecretTargetSelection:
      {
            QVector<PlayerState> targets;
            for (const PlayerState &state : m_players)
            {
                  if (isConnected(state.id) && state.id != m_currentPicker)
                  {
                        targets.push_back(state);
                  }
            }
            if (!targets.isEmpty())
            {
                  const int index = QRandomGenerator::global()->bounded(
                        targets.size());
                  selectSecretTarget(m_currentPicker,
                                     targets[static_cast<std::size_t>(index)].id,
                                     m_questionSequence);
            }
            else
            {
                  beginNextQuestion();
            }
            break;
      }
      case SessionPhase::SecretWager:
      {
            const Question *question = currentQuestion();
            if (question != nullptr && question->secretParameters.has_value())
            {
                  submitSecretWager(m_secretTarget,
                                    question->secretParameters->price.minimum,
                                    m_questionSequence);
            }
            else
            {
                  revealAnswer();
            }
            break;
      }
      case SessionPhase::ReadingQuestion:
            if (currentQuestion() != nullptr &&
                currentQuestion()->type == QuestionType::ForAll)
            {
                  beginForAllAnswering();
            }
            else if (currentQuestion() != nullptr &&
                     currentQuestion()->type == QuestionType::SecretPublicPrice)
            {
                  m_answerOwner = m_secretTarget;
                  startPhase(SessionPhase::Answering, questionAnswerDuration(),
                             m_answerOwner);
                  emit answerOwnerChanged(m_answerOwner, questionAnswerDuration());
            }
            else
            {
                  beginReaction();
            }
            break;
      case SessionPhase::WaitingForReaction:
            decideReactionWinner();
            break;
      case SessionPhase::Answering:
            if (!m_answerOwner.isEmpty())
            {
                  const auto draft = m_answerDrafts.constFind(m_answerOwner);
                  if (draft == m_answerDrafts.cend())
                  {
                        submitAnswerInternal(m_answerOwner, {}, true);
                  }
                  else
                  {
                        submitAnswerInternal(m_answerOwner, draft.value(),
                                             false);
                  }
            }
            else
            {
                  revealAnswer();
            }
            break;
      case SessionPhase::ForAllAnswering:
            finishForAll();
            break;
      case SessionPhase::ShowingAnswer:
            beginNextQuestion();
            break;
      case SessionPhase::AppealVoting:
            finishAppeal(false);
            break;
      case SessionPhase::Finished:
            break;
      }
}

void GameSession::beginPicking()
{
      if (m_game.rounds.empty() || boardState().cells.isEmpty())
      {
            m_phase = SessionPhase::Finished;
            emit gameFinished();
            return;
      }
      if (!isConnected(m_currentPicker))
      {
            m_currentPicker = chooseRandomConnectedPlayer();
      }
      if (m_currentPicker.isEmpty())
      {
            return;
      }
      for (PlayerState &state : m_players)
      {
            state.isPicker = state.id == m_currentPicker;
      }
      emitPlayersChanged();
      emit pickerChanged(m_currentPicker);
      emitBoardChanged();
      startPhase(SessionPhase::PickingQuestion, m_config.questionPickDurationMs,
                 m_currentPicker);
}

void GameSession::selectRandomQuestion()
{
      if (!isConnected(m_currentPicker))
      {
            m_currentPicker = chooseRandomConnectedPlayer();
      }
      QVector<QPair<int, int>> available;
      if (!m_game.rounds.empty())
      {
            const Round &round = m_game.rounds.front();
            for (int theme = 0; theme < static_cast<int>(round.themes.size()); ++theme)
            {
                  const Theme &currentTheme = round.themes[static_cast<std::size_t>(theme)];
                  for (int question = 0;
                       question < static_cast<int>(currentTheme.questions.size());
                       ++question)
                  {
                        if (isQuestionAvailable(0, theme, question))
                        {
                              available.push_back({theme, question});
                        }
                  }
            }
      }
      if (available.isEmpty() || m_currentPicker.isEmpty())
      {
            beginNextQuestion();
            return;
      }
      const int index = QRandomGenerator::global()->bounded(available.size());
      selectQuestion(m_currentPicker, 0, available[index].first,
                     available[index].second);
}

void GameSession::beginReaction()
{
      if (!hasRemainingEligiblePlayers())
      {
            revealAnswer();
            return;
      }
      m_reactionClaims.clear();
      startPhase(SessionPhase::WaitingForReaction, m_config.answerWaitDurationMs);
      m_reactionDeadlineMs = m_deadlineMs;
      emit reactionOpened({m_questionSequence, m_phaseSequence,
                           m_config.answerWaitDurationMs,
                           m_config.answerWaitDurationMs});
}

void GameSession::decideReactionWinner()
{
      if (m_reactionClaims.isEmpty())
      {
            revealAnswer();
            return;
      }
      PlayerId winner;
      ReactionClaim selected;
      bool haveSelected = false;
      for (auto iterator = m_reactionClaims.cbegin();
           iterator != m_reactionClaims.cend(); ++iterator)
      {
            if (!isEligible(iterator.key()))
            {
                  continue;
            }
            if (!haveSelected || iterator.value().scoreMs < selected.scoreMs)
            {
                  winner = iterator.key();
                  selected = iterator.value();
                  haveSelected = true;
            }
      }
      if (!haveSelected)
      {
            revealAnswer();
            return;
      }
      m_answerOwner = winner;
      emit reactionWinner(winner, selected.scoreMs);
      startPhase(SessionPhase::Answering, questionAnswerDuration(), winner);
      emit answerOwnerChanged(winner, questionAnswerDuration());
}

void GameSession::beginForAllAnswering()
{
      m_forAllExpected.clear();
      m_forAllAttempts.clear();
      for (const PlayerState &state : m_players)
      {
            if (state.connected)
            {
                  m_forAllExpected.insert(state.id);
                  m_forAllAttempts.insert(state.id, {});
            }
      }
      if (m_forAllExpected.isEmpty())
      {
            revealAnswer();
            return;
      }
      startPhase(SessionPhase::ForAllAnswering, questionAnswerDuration());
}

void GameSession::finishForAll()
{
      const Question *question = currentQuestion();
      if (question == nullptr)
      {
            revealAnswer();
            return;
      }
      ForAllResult result;
      result.questionSequence = m_questionSequence;
      QVector<PlayerId> correctPlayers;
      for (const PlayerState &candidate : m_players)
      {
            const PlayerId &id = candidate.id;
            if (!m_forAllExpected.contains(id))
            {
                  continue;
            }
            ForAllAttempt attempt = m_forAllAttempts.value(id);
            if (!attempt.submitted && m_answerDrafts.contains(id))
            {
                  attempt.submission = m_answerDrafts.value(id);
                  attempt.correct =
                        isCorrectSubmission(*question, attempt.submission);
                  attempt.submitted = true;
            }
            const bool correct = attempt.submitted && attempt.correct;
            const int amount = question->price;
            PlayerState *state = player(id);
            if (state == nullptr)
            {
                  continue;
            }
            if (correct)
            {
                  state->balance += amount;
                  correctPlayers.push_back(id);
                  m_correctForCurrentQuestion.insert(id, true);
            }
            else
            {
                  state->balance -= amount;
                  m_wrongAmounts.insert(id, -amount);
                  state->mayAppeal = true;
                  state->answeredIncorrectly = true;
                  m_questionStates[id].answeredIncorrectly = true;
                  m_questionStates[id].appealable = true;
            }
            m_submittedAnswers.insert(id, answerText(attempt.submission));
            result.results.push_back({m_questionSequence,
                                      id,
                                      correct,
                                      correct ? amount : -amount,
                                      state->balance,
                                      question->answerType,
                                      answerText(attempt.submission),
                                      0,
                                      false});
      }
      if (!correctPlayers.isEmpty())
      {
            result.nextPicker = correctPlayers[QRandomGenerator::global()->bounded(
                  correctPlayers.size())];
            m_nextPicker = result.nextPicker;
      }
      emitPlayersChanged();
      emit forAllResult(result);
      revealAnswer();
}

void GameSession::submitAnswerInternal(const PlayerId &playerId,
                                       const AnswerSubmission &submission,
                                       bool timedOut)
{
      const Question *question = currentQuestion();
      if (question == nullptr)
      {
            return;
      }
      const bool correct = !timedOut && isCorrectSubmission(*question, submission);
      if (m_phase == SessionPhase::ForAllAnswering)
      {
            ForAllAttempt attempt;
            attempt.submission = submission;
            attempt.correct = correct;
            attempt.submitted = true;
            m_forAllAttempts.insert(playerId, attempt);
            m_questionStates[playerId].submitted = true;
            m_questionStates[playerId].effectiveAmount = question->price;
            if (PlayerState *state = player(playerId))
            {
                  state->hasAnsweredForAll = true;
            }
            emitPlayersChanged();
            int received = 0;
            for (const PlayerId &id : m_forAllExpected)
            {
                  if (m_forAllAttempts.value(id).submitted)
                  {
                        ++received;
                  }
            }
            emit forAllProgress(m_questionSequence, received,
                                m_forAllExpected.size());
            if (allForAllAnswersReceived())
            {
                  finishForAll();
            }
            return;
      }
      applyNormalAnswer(playerId, submission, correct, timedOut);
}

void GameSession::applyNormalAnswer(const PlayerId &playerId,
                                    const AnswerSubmission &submission,
                                    bool correct, bool timedOut)
{
      const Question *question = currentQuestion();
      PlayerState *state = player(playerId);
      if (question == nullptr || state == nullptr)
      {
            return;
      }
      const int amount = question->type == QuestionType::SecretPublicPrice
                               ? m_secretWager
                               : question->price;
      const QString submitted = timedOut ? QString() : answerText(submission);
      m_submittedAnswers.insert(playerId, submitted);
      AnswerResult result;
      result.questionSequence = m_questionSequence;
      result.playerId = playerId;
      result.correct = correct;
      result.amount = correct ? amount : -amount;
      result.answerKind = question->answerType;
      result.submitted = submitted;
      result.remainingReactionMs =
            m_reactionDeadlineMs > 0
                  ? static_cast<unsigned int>(std::max<qint64>(
                          0, m_reactionDeadlineMs - m_clock.elapsed()))
                  : 0U;

      if (correct)
      {
            state->balance += amount;
            state->mayAppeal = false;
            m_correctForCurrentQuestion.insert(playerId, true);
            m_questionStates[playerId].effectiveAmount = amount;
            m_nextPicker = playerId;
            result.balance = state->balance;
            result.retryAllowed = false;
            emitPlayersChanged();
            emit answerResult(result);
            revealAnswer();
            return;
      }

      state->balance -= amount;
      state->answeredIncorrectly = true;
      state->mayAppeal = true;
      m_questionStates[playerId].answeredIncorrectly = true;
      m_questionStates[playerId].appealable = true;
      m_questionStates[playerId].effectiveAmount = amount;
      m_wrongAmounts.insert(playerId, -amount);
      result.balance = state->balance;
      const bool secret = question->type == QuestionType::SecretPublicPrice;
      const unsigned int remaining = result.remainingReactionMs;
      result.retryAllowed = !secret && remaining > 0 && hasRemainingEligiblePlayers();
      emitPlayersChanged();
      emit answerResult(result);
      m_answerOwner.clear();
      emit answerOwnerChanged({}, 0);
      if (result.retryAllowed)
      {
            startPhase(SessionPhase::WaitingForReaction, remaining);
            emit reactionResumed(remaining, playerId);
      }
      else
      {
            revealAnswer();
      }
}

void GameSession::revealAnswer()
{
      if (m_presentation.has_value() &&
          (m_phase == SessionPhase::ShowingAnswer ||
           m_phase == SessionPhase::AppealVoting))
      {
            return;
      }
      if (currentQuestion() == nullptr)
      {
            beginNextQuestion();
            return;
      }
      chooseNextPickerIfNeeded();
      const PlayerId revealedAnswerOwner = m_answerOwner;
      m_answerOwner.clear();
      AnswerReveal reveal = makeReveal();
      reveal.answerOwner = revealedAnswerOwner;
      m_lastReveal = reveal;
      emit answerRevealed(reveal);
      startPhase(SessionPhase::ShowingAnswer, m_config.answerRevealDurationMs,
                 reveal.nextPicker);
}

void GameSession::beginNextQuestion()
{
      m_timer.stop();
      for (PlayerState &state : m_players)
      {
            state.hasPassed = false;
            state.answeredIncorrectly = false;
            state.hasAnsweredForAll = false;
            state.mayAppeal = false;
            state.isPicker = state.id == m_nextPicker;
      }
      if (!m_nextPicker.isEmpty())
      {
            m_currentPicker = m_nextPicker;
      }
      m_nextPicker.clear();
      m_answerOwner.clear();
      m_presentation.reset();
      m_lastReveal.reset();
      m_questionStates.clear();
      m_wrongAmounts.clear();
      m_answerDrafts.clear();
      m_submittedAnswers.clear();
      m_correctForCurrentQuestion.clear();
      m_appeal.reset();
      m_appealVoters.clear();
      m_votedAppeal.clear();
      m_currentRound = -1;
      m_currentTheme = -1;
      m_currentQuestion = -1;
      emitPlayersChanged();
      const BoardState board = boardState();
      if (board.cells.isEmpty() ||
          std::none_of(board.cells.cbegin(), board.cells.cend(),
                       [](const BoardCell &cell) { return !cell.used; }))
      {
            m_phase = SessionPhase::Finished;
            m_remainingMs = 0;
            emit gameFinished();
            return;
      }
      beginPicking();
}

void GameSession::finishAppeal(bool accepted)
{
      if (!m_appeal.has_value())
      {
            return;
      }
      const AppealState appeal = *m_appeal;
      AppealResult result;
      result.appealId = appeal.appealId;
      result.questionSequence = appeal.questionSequence;
      result.accepted = accepted;
      result.appellant = appeal.appellant;
      const int wrongAmount = m_wrongAmounts.value(appeal.appellant, 0);
      PlayerState *appellant = player(appeal.appellant);
      if (accepted && appellant != nullptr)
      {
            result.correction = -2 * wrongAmount;
            appellant->balance += result.correction;
            appellant->mayAppeal = false;
            m_nextPicker = appeal.appellant;
      }
      else
      {
            result.correction = 0;
      }
      result.balance = appellant == nullptr ? 0 : appellant->balance;
      if (m_nextPicker.isEmpty())
      {
            chooseNextPickerIfNeeded();
      }
      result.nextPicker = m_nextPicker;
      emitPlayersChanged();
      emit appealFinished(result);
      m_appeal.reset();
      beginNextQuestion();
}

void GameSession::resetQuestionState()
{
      m_questionStates.clear();
      m_wrongAmounts.clear();
      m_answerDrafts.clear();
      m_submittedAnswers.clear();
      m_correctForCurrentQuestion.clear();
      for (PlayerState &state : m_players)
      {
            state.hasPassed = false;
            state.answeredIncorrectly = false;
            state.hasAnsweredForAll = false;
            state.mayAppeal = false;
            m_questionStates.insert(state.id,
                                    {m_questionSequence, false, false, false,
                                     false, 0});
      }
}

void GameSession::chooseNextPickerIfNeeded()
{
      if (!m_nextPicker.isEmpty() && isConnected(m_nextPicker))
      {
            return;
      }
      m_nextPicker = chooseRandomConnectedPlayer();
}

PlayerId GameSession::chooseRandomConnectedPlayer() const
{
      QVector<PlayerId> candidates;
      for (const PlayerState &state : m_players)
      {
            if (state.connected)
            {
                  candidates.push_back(state.id);
            }
      }
      if (candidates.isEmpty())
      {
            return {};
      }
      return candidates[QRandomGenerator::global()->bounded(candidates.size())];
}

Question *GameSession::currentQuestion()
{
      if (m_currentRound < 0 || m_currentTheme < 0 || m_currentQuestion < 0 ||
          m_game.rounds.empty())
      {
            return nullptr;
      }
      Round &round = m_game.rounds[static_cast<std::size_t>(m_currentRound)];
      if (m_currentTheme >= static_cast<int>(round.themes.size()))
      {
            return nullptr;
      }
      Theme &theme = round.themes[static_cast<std::size_t>(m_currentTheme)];
      if (m_currentQuestion >= static_cast<int>(theme.questions.size()))
      {
            return nullptr;
      }
      return &theme.questions[static_cast<std::size_t>(m_currentQuestion)];
}

const Question *GameSession::currentQuestion() const
{
      return const_cast<GameSession *>(this)->currentQuestion();
}

QuestionPresentation GameSession::makePresentation() const
{
      QuestionPresentation presentation;
      const Question *question = currentQuestion();
      if (question == nullptr || m_game.rounds.empty())
      {
            return presentation;
      }
      const Round &round = m_game.rounds[static_cast<std::size_t>(m_currentRound)];
      const Theme &theme = round.themes[static_cast<std::size_t>(m_currentTheme)];
      presentation.questionSequence = m_questionSequence;
      presentation.round = m_currentRound;
      presentation.theme = m_currentTheme;
      presentation.question = m_currentQuestion;
      presentation.themeName = theme.name;
      presentation.price = question->price;
      presentation.questionType = question->type;
      presentation.answerType = question->answerType;
      presentation.text = question->text;
      presentation.mediaType = question->mediaType;
      presentation.mediaPath = question->mediaPath;
      presentation.answerDurationMs = questionAnswerDuration();
      presentation.answerOwner = question->type == QuestionType::SecretPublicPrice
                                       ? m_secretTarget
                                       : QString();
      for (const AnswerOption &option : question->answerOptions)
      {
            presentation.answerOptions.push_back(option);
      }
      return presentation;
}

AnswerReveal GameSession::makeReveal() const
{
      AnswerReveal reveal;
      reveal.questionSequence = m_questionSequence;
      reveal.answerOwner = m_answerOwner;
      reveal.nextPicker = m_nextPicker;
      const Question *question = currentQuestion();
      if (question == nullptr)
      {
            return reveal;
      }
      for (const QString &answer : question->rightAnswers)
      {
            if (question->answerType == AnswerType::Point)
            {
                  QPointF point;
                  double aspectRatio{1.0};
                  if (parsePointAnswer(answer, &point, &aspectRatio))
                  {
                        continue;
                  }
            }
            reveal.rightAnswers.push_back(answer);
      }
      reveal.answerMediaType = question->answerMediaType;
      reveal.answerMediaPath = question->answerMediaPath;
      return reveal;
}

bool GameSession::validateSubmission(const Question &question,
                                     const AnswerSubmission &submission) const
{
      if (submission.answerType != question.answerType &&
          !(question.answerType == AnswerType::Unknown &&
            submission.answerType == AnswerType::Text))
      {
            return false;
      }
      return true;
}

bool GameSession::parsePointAnswer(const QString &value, QPointF *point,
                                   double *aspectRatio) const
{
      const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
      if ((parts.size() != 2 && parts.size() != 3) || point == nullptr ||
          aspectRatio == nullptr)
      {
            return false;
      }
      bool xValid = false;
      bool yValid = false;
      const double x = parts[0].trimmed().toDouble(&xValid);
      const double y = parts[1].trimmed().toDouble(&yValid);
      if (!xValid || !yValid || !std::isfinite(x) || !std::isfinite(y) ||
          x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
      {
            return false;
      }
      double ratio = 1.0;
      if (parts.size() == 3)
      {
            bool ratioValid = false;
            const double candidate = parts[2].trimmed().toDouble(&ratioValid);
            if (!ratioValid || !std::isfinite(candidate))
            {
                  return false;
            }
            if (candidate > 0.0)
            {
                  ratio = candidate;
            }
      }
      *point = QPointF(x, y);
      *aspectRatio = ratio;
      return true;
}

bool GameSession::isCorrectSubmission(const Question &question,
                                      const AnswerSubmission &submission) const
{
      if (!validateSubmission(question, submission))
      {
            return false;
      }
      if (question.answerType == AnswerType::Point)
      {
            if (!submission.hasPoint)
            {
                  return false;
            }
            QPointF correctPoint;
            double aspectRatio = 1.0;
            bool found = false;
            for (const QString &answer : question.rightAnswers)
            {
                  if (parsePointAnswer(answer, &correctPoint, &aspectRatio))
                  {
                        found = true;
                        break;
                  }
            }
            if (!found || submission.point.x() < 0.0 ||
                submission.point.x() > 1.0 || submission.point.y() < 0.0 ||
                submission.point.y() > 1.0)
            {
                  return false;
            }
            const double dx =
                  (submission.point.x() - correctPoint.x()) * aspectRatio;
            const double dy = submission.point.y() - correctPoint.y();
            return std::hypot(dx, dy) <=
                   std::max(0.02, question.answerDeviation);
      }
      if (question.answerType == AnswerType::Select)
      {
            const QString submitted = submission.optionId.trimmed();
            return std::any_of(
                  question.rightAnswers.cbegin(), question.rightAnswers.cend(),
                  [&submitted](const QString &right)
                  { return submitted.compare(right.trimmed(),
                                              Qt::CaseInsensitive) == 0; });
      }
      const QString submitted = submission.answer.trimmed();
      return std::any_of(
            question.rightAnswers.cbegin(), question.rightAnswers.cend(),
            [&submitted](const QString &right)
            { return submitted.compare(right.trimmed(), Qt::CaseInsensitive) == 0; });
}

unsigned int GameSession::questionAnswerDuration() const
{
      const Question *question = currentQuestion();
      if (question != nullptr && question->answerDuration > 0)
      {
            return clampDuration(question->answerDuration * 1000ULL);
      }
      return m_config.answerDurationMs;
}

unsigned int GameSession::clampDuration(quint64 value) const
{
      return static_cast<unsigned int>(std::min<quint64>(
            value, std::numeric_limits<unsigned int>::max()));
}

void GameSession::emitPlayersChanged() { emit playersChanged(m_players); }

void GameSession::emitBoardChanged() { emit boardChanged(boardState()); }

QString GameSession::cellKey(int round, int theme, int question) const
{
      return QStringLiteral("%1:%2:%3").arg(round).arg(theme).arg(question);
}

bool GameSession::allForAllAnswersReceived() const
{
      if (m_forAllExpected.isEmpty())
      {
            return true;
      }
      for (const PlayerId &id : m_forAllExpected)
      {
            if (!m_forAllAttempts.value(id).submitted)
            {
                  return false;
            }
      }
      return true;
}
