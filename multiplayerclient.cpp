#include "multiplayerclient.h"

#include "multiplayerprotocol.h"

#include <QCryptographicHash>
#include <QImage>
#include <QLocale>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
QString number(quint64 value) { return QString::number(value); }
QString number(qint64 value) { return QString::number(value); }
QString number(unsigned int value) { return QString::number(value); }
QString number(int value) { return QString::number(value); }
QString boolValue(bool value) { return value ? QStringLiteral("1") : QStringLiteral("0"); }
}

MultiplayerClient::MultiplayerClient(QObject *parent)
      : QObject(parent), m_connection(new MultiplayerConnection(this))
{
      connect(m_connection, &MultiplayerConnection::connected, this,
              &MultiplayerClient::handleConnected);
      connect(m_connection, &MultiplayerConnection::lineReceived, this,
              &MultiplayerClient::handleLine);
      connect(m_connection, &MultiplayerConnection::disconnected, this,
              &MultiplayerClient::handleDisconnected);
      connect(m_connection, &MultiplayerConnection::transportError, this,
              &MultiplayerClient::handleTransportError);
}

MultiplayerClient::~MultiplayerClient() { disconnectFromHost(); }

void MultiplayerClient::connectToHost(const QHostAddress &address, quint16 port,
                                      const GameConfig &localConfig,
                                      const PlayerIdentity &identity)
{
      m_address = address;
      m_port = port;
      m_config = localConfig;
      m_identity = identity;
      m_roster.clear();
      m_profileCache.clear();
      if (m_identity.token.isEmpty())
      {
            m_identity = PlayerIdentity::load();
      }
      m_identity.profilePng = normalizedProfilePng(m_identity.profilePng);
      m_profileTransferId = m_identity.profilePng.isEmpty()
                                 ? QStringLiteral("none")
                                 : QUuid::createUuid().toString(
                                       QUuid::WithoutBraces);
      m_waitingForWelcome = true;
      m_connection->connectToHost(m_address, m_port);
}

void MultiplayerClient::disconnectFromHost()
{
      if (m_connection != nullptr)
      {
            m_connection->close();
      }
}

void MultiplayerClient::reconnect()
{
      if (m_address.isNull())
      {
            return;
      }
      m_waitingForWelcome = true;
      m_connection->connectToHost(m_address, m_port);
}

bool MultiplayerClient::isConnected() const
{
      return m_connection != nullptr && m_connection->isConnected();
}

void MultiplayerClient::selectQuestion(int roundIndex, int themeIndex,
                                       int questionIndex)
{
      sendCommand(QStringLiteral("SELECT_QUESTION"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("phaseSeq"), number(m_lastPhaseSequence)},
                   {QStringLiteral("round"), number(roundIndex)},
                   {QStringLiteral("theme"), number(themeIndex)},
                   {QStringLiteral("question"), number(questionIndex)}});
}

void MultiplayerClient::submitReaction(quint64 questionSequence,
                                       quint64 phaseSequence,
                                       unsigned int elapsedMs)
{
      sendCommand(QStringLiteral("REACTION_CLAIM"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)},
                   {QStringLiteral("phaseSeq"), number(phaseSequence)},
                   {QStringLiteral("elapsedMs"), number(elapsedMs)}});
}

void MultiplayerClient::submitAnswer(quint64 questionSequence,
                                     quint64 phaseSequence,
                                     const AnswerSubmission &submission)
{
      QMap<QString, QString> fields{
            {QStringLiteral("requestId"), requestId()},
            {QStringLiteral("actionId"), number(actionId())},
            {QStringLiteral("phaseSeq"), number(phaseSequence)},
            {QStringLiteral("questionSeq"), number(questionSequence)},
            {QStringLiteral("answerType"),
             MultiplayerProtocol::answerTypeName(
                   static_cast<int>(submission.answerType))}};
      if (!submission.mode.isEmpty())
      {
            fields.insert(QStringLiteral("mode"), submission.mode);
      }
      switch (submission.answerType)
      {
      case AnswerType::Text:
            fields.insert(QStringLiteral("answer"), submission.answer);
            break;
      case AnswerType::Select:
            fields.insert(QStringLiteral("optionId"), submission.optionId);
            break;
      case AnswerType::Point:
            fields.insert(QStringLiteral("x"),
                          QLocale::c().toString(submission.point.x(), 'f', 6));
            fields.insert(QStringLiteral("y"),
                          QLocale::c().toString(submission.point.y(), 'f', 6));
            break;
      case AnswerType::Unknown:
            fields.insert(QStringLiteral("answer"), submission.answer);
            break;
      }
      sendCommand(QStringLiteral("ANSWER_SUBMIT"), fields);
}

void MultiplayerClient::updateAnswerDraft(quint64 questionSequence,
                                          quint64 phaseSequence,
                                          const QString &answer)
{
      sendCommand(QStringLiteral("ANSWER_DRAFT"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)},
                   {QStringLiteral("phaseSeq"), number(phaseSequence)},
                   {QStringLiteral("answer"), answer}});
}

void MultiplayerClient::pass(quint64 questionSequence,
                             quint64 phaseSequence)
{
      sendCommand(QStringLiteral("PASS"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)},
                   {QStringLiteral("phaseSeq"), number(phaseSequence)}});
}

void MultiplayerClient::selectSecretTarget(quint64 questionSequence,
                                           PlayerId targetId)
{
      sendCommand(QStringLiteral("SECRET_TARGET"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)},
                   {QStringLiteral("targetPlayerId"), targetId}});
}

void MultiplayerClient::submitSecretWager(quint64 questionSequence, int amount)
{
      sendCommand(QStringLiteral("SECRET_WAGER"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)},
                   {QStringLiteral("amount"), number(amount)}});
}

void MultiplayerClient::requestPause(quint64 phaseSequence, bool paused)
{
      sendCommand(QStringLiteral("PAUSE_REQUEST"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("phaseSeq"), number(phaseSequence)},
                   {QStringLiteral("paused"), boolValue(paused)}});
}

void MultiplayerClient::requestAppeal(quint64 questionSequence)
{
      sendCommand(QStringLiteral("APPEAL_REQUEST"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("questionSeq"), number(questionSequence)}});
}

void MultiplayerClient::voteAppeal(quint64 appealId, bool accepted)
{
      sendCommand(QStringLiteral("APPEAL_VOTE"),
                  {{QStringLiteral("requestId"), requestId()},
                   {QStringLiteral("actionId"), number(actionId())},
                   {QStringLiteral("appealId"), number(appealId)},
                   {QStringLiteral("accepted"), boolValue(accepted)}});
}

void MultiplayerClient::handleConnected()
{
      sendHello();
}

void MultiplayerClient::handleLine(const QByteArray &line)
{
      MultiplayerProtocol::Frame frame;
      QString error;
      if (!MultiplayerProtocol::parseFrame(line, &frame, &error))
      {
            emit protocolError(QStringLiteral("BAD_FRAME"), error);
            return;
      }
      handleFrame(frame);
}

void MultiplayerClient::handleDisconnected()
{
      m_waitingForWelcome = false;
      emit disconnected(QStringLiteral("The connection was closed"));
}

void MultiplayerClient::handleTransportError(const QString &message)
{
      emit protocolError(QStringLiteral("TRANSPORT_ERROR"), message);
}

void MultiplayerClient::sendHello()
{
      sendCommand(QStringLiteral("HELLO"),
                  {{QStringLiteral("protocol"),
                    QString::number(MultiplayerProtocol::ProtocolVersion)},
                   {QStringLiteral("token"), m_identity.token},
                   {QStringLiteral("nickname"), m_identity.nickname},
                   {QStringLiteral("packHash"), m_config.packHash},
                   {QStringLiteral("profileTransfer"), m_profileTransferId},
                   {QStringLiteral("lastSessionId"), m_sessionId},
                   {QStringLiteral("lastSnapshotSeq"),
                    number(m_lastSnapshotSequence)},
                   {QStringLiteral("requestId"), requestId()}});
}

void MultiplayerClient::sendProfile()
{
      if (m_identity.profilePng.isEmpty() ||
          m_identity.profilePng.size() > MultiplayerProtocol::MaxProfileBytes)
      {
            sendReady();
            return;
      }
      const QByteArray hash = QCryptographicHash::hash(
            m_identity.profilePng, QCryptographicHash::Sha256);
      sendCommand(QStringLiteral("PROFILE_BEGIN"),
                  {{QStringLiteral("transferId"), m_profileTransferId},
                   {QStringLiteral("playerId"), m_localPlayerId},
                   {QStringLiteral("format"), QStringLiteral("png")},
                   {QStringLiteral("bytes"), number(m_identity.profilePng.size())},
                   {QStringLiteral("sha256"), QString::fromLatin1(hash.toHex())}});
      constexpr int chunkSize = 24 * 1024;
      int index = 0;
      for (int offset = 0; offset < m_identity.profilePng.size();
           offset += chunkSize)
      {
            const QByteArray chunk = m_identity.profilePng.mid(offset, chunkSize);
            sendCommand(QStringLiteral("PROFILE_CHUNK"),
                        {{QStringLiteral("transferId"), m_profileTransferId},
                         {QStringLiteral("index"), number(index++)},
                         {QStringLiteral("data"),
                          QString::fromLatin1(chunk.toBase64(
                                QByteArray::Base64UrlEncoding |
                                QByteArray::OmitTrailingEquals))}});
      }
      sendCommand(QStringLiteral("PROFILE_END"),
                  {{QStringLiteral("transferId"), m_profileTransferId},
                   {QStringLiteral("sha256"), QString::fromLatin1(hash.toHex())}});
      sendReady();
}

void MultiplayerClient::sendReady()
{
      sendCommand(QStringLiteral("READY"),
                  {{QStringLiteral("sessionId"), m_sessionId},
                   {QStringLiteral("playerId"), m_localPlayerId},
                   {QStringLiteral("requestId"), requestId()}});
}

void MultiplayerClient::sendCommand(const QString &command,
                                    QMap<QString, QString> fields)
{
      if (m_connection == nullptr || !m_connection->isConnected())
      {
            return;
      }
      m_connection->sendLine(MultiplayerProtocol::encodeFrame(command, fields));
}

QString MultiplayerClient::requestId()
{
      m_lastRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
      return m_lastRequestId;
}

quint64 MultiplayerClient::actionId() { return m_nextActionId++; }

void MultiplayerClient::handleFrame(const MultiplayerProtocol::Frame &frame)
{
      if (!MultiplayerProtocol::isKnownCommand(frame.command))
      {
            emit protocolError(QStringLiteral("BAD_FRAME"),
                               QStringLiteral("Unknown command"));
            return;
      }
      const QMap<QString, QString> &fields = frame.fields;
      if (frame.command == QStringLiteral("WELCOME"))
      {
            m_sessionId = fields.value(QStringLiteral("sessionId"));
            m_localPlayerId = fields.value(QStringLiteral("playerId"));
            if (!m_identity.profilePng.isEmpty())
            {
                  m_profileCache.insert(m_localPlayerId,
                                        m_identity.profilePng);
            }
            m_reconnected = fields.value(QStringLiteral("reconnect")) ==
                            QStringLiteral("1");
            emit connected(m_localPlayerId, m_reconnected);
            if (m_profileTransferId == QStringLiteral("none"))
            {
                  sendReady();
            }
            else
            {
                  sendProfile();
            }
            return;
      }
      if (frame.command == QStringLiteral("PACK_OK"))
      {
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_BEGIN"))
      {
            handleProfileBegin(fields);
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_CHUNK"))
      {
            handleProfileChunk(fields);
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_END"))
      {
            handleProfileEnd(fields);
            return;
      }
      if (frame.command == QStringLiteral("ROSTER_BEGIN"))
      {
            m_roster.clear();
            m_rosterOpen = true;
            m_rosterSequence = fields.value(QStringLiteral("rosterSeq")).toULongLong();
            return;
      }
      if (frame.command == QStringLiteral("ROSTER_PLAYER"))
      {
            parseRosterPlayer(fields);
            return;
      }
      if (frame.command == QStringLiteral("ROSTER_END"))
      {
            m_rosterOpen = false;
            emitLobby();
            return;
      }
      if (frame.command == QStringLiteral("GAME_STARTED"))
      {
            m_config.maxPlayers = fields.value(QStringLiteral("maxPlayers")).toInt();
            m_config.answerDurationMs =
                  fields.value(QStringLiteral("answerDurationMs")).toUInt();
            m_config.questionDurationMs =
                  fields.value(QStringLiteral("questionDurationMs")).toUInt();
            m_config.questionPickDurationMs =
                  fields.value(QStringLiteral("questionPickDurationMs")).toUInt();
            m_config.answerWaitDurationMs =
                  fields.value(QStringLiteral("answerWaitDurationMs")).toUInt();
            m_config.answerRevealDurationMs =
                  fields.value(QStringLiteral("answerRevealDurationMs")).toUInt();
            m_config.appealDurationMs =
                  fields.value(QStringLiteral("appealDurationMs")).toUInt();
            emit configurationReceived(m_config);
            emit gameStarted();
            return;
      }
      if (frame.command == QStringLiteral("BOARD_STATE"))
      {
            BoardState board;
            board.boardSequence = fields.value(QStringLiteral("boardSeq")).toULongLong();
            board.round = fields.value(QStringLiteral("round")).toInt();
            const QStringList used =
                  fields.value(QStringLiteral("used")).split(QLatin1Char(','),
                                                               Qt::SkipEmptyParts);
            for (const QString &cell : used)
            {
                  const QStringList coordinates = cell.split(QLatin1Char(':'));
                  if (coordinates.size() != 2)
                  {
                        continue;
                  }
                  bool themeOk = false;
                  bool questionOk = false;
                  const int theme = coordinates[0].toInt(&themeOk);
                  const int question = coordinates[1].toInt(&questionOk);
                  if (themeOk && questionOk)
                  {
                        board.cells.push_back({board.round, theme, question, true});
                  }
            }
            if (board.boardSequence < m_lastBoardSequence)
            {
                  return;
            }
            m_lastBoardSequence = board.boardSequence;
            emit boardReceived(board);
            return;
      }
      if (frame.command == QStringLiteral("PICKER_CHANGED"))
      {
            emit pickerReceived(fields.value(QStringLiteral("playerId")));
            return;
      }
      if (frame.command == QStringLiteral("PHASE_START") ||
          frame.command == QStringLiteral("PHASE_RESUMED") ||
          frame.command == QStringLiteral("PHASE_SYNC"))
      {
            PhaseState state;
            QString error;
            if (parsePhase(fields, &state, &error))
            {
                  if (state.phaseSequence < m_lastPhaseSequence)
                  {
                        return;
                  }
                  m_lastPhaseSequence = state.phaseSequence;
                  m_lastQuestionSequence = std::max(
                        m_lastQuestionSequence, state.questionSequence);
                  emit phaseReceived(state);
            }
            else
            {
                  emit protocolError(QStringLiteral("BAD_FIELD"), error);
            }
            return;
      }
      if (frame.command == QStringLiteral("REACTION_OPEN"))
      {
            PhaseState state;
            state.phase = SessionPhase::WaitingForReaction;
            state.phaseSequence = fields.value(QStringLiteral("phaseSeq")).toULongLong();
            state.questionSequence =
                  fields.value(QStringLiteral("questionSeq")).toULongLong();
            state.durationMs = fields.value(QStringLiteral("durationMs")).toUInt();
            state.remainingMs = fields.value(QStringLiteral("remainingMs")).toUInt();
            if (state.phaseSequence < m_lastPhaseSequence)
            {
                  return;
            }
            m_lastPhaseSequence = state.phaseSequence;
            m_lastQuestionSequence = std::max(m_lastQuestionSequence,
                                              state.questionSequence);
            emit phaseReceived(state);
            return;
      }
      if (frame.command == QStringLiteral("QUESTION_START"))
      {
            QuestionPresentation question;
            QString error;
            if (parseQuestion(fields, &question, &error))
            {
                  if (question.questionSequence < m_lastQuestionSequence)
                  {
                        return;
                  }
                  m_lastQuestionSequence = question.questionSequence;
                  if (m_snapshotOpen)
                  {
                        m_snapshot.question = question;
                  }
                  else
                  {
                        emit questionReceived(question);
                  }
            }
            else
            {
                  emit protocolError(QStringLiteral("BAD_FIELD"), error);
            }
            return;
      }
      if (frame.command == QStringLiteral("REACTION_WINNER"))
      {
            emit reactionWinnerReceived(
                  fields.value(QStringLiteral("playerId")),
                  fields.value(QStringLiteral("measuredElapsedMs")).toUInt());
            return;
      }
      if (frame.command == QStringLiteral("ANSWER_OWNER"))
      {
            emit answerOwnerReceived(fields.value(QStringLiteral("playerId")),
                                     fields.value(QStringLiteral("durationMs"))
                                           .toUInt());
            return;
      }
      if (frame.command == QStringLiteral("ANSWER_RESULT"))
      {
            AnswerResult result;
            result.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            result.playerId = fields.value(QStringLiteral("playerId"));
            result.correct = fields.value(QStringLiteral("correct")) ==
                             QStringLiteral("1");
            result.amount = fields.value(QStringLiteral("amount")).toInt();
            result.balance = fields.value(QStringLiteral("balance")).toInt();
            parseAnswerType(fields.value(QStringLiteral("answerKind")),
                            &result.answerKind);
            result.submitted = fields.value(QStringLiteral("submitted"));
            result.remainingReactionMs =
                  fields.value(QStringLiteral("remainingReactionMs")).toUInt();
            result.retryAllowed = fields.value(QStringLiteral("retryAllowed")) ==
                                  QStringLiteral("1");
            emit answerResultReceived(result);
            return;
      }
      if (frame.command == QStringLiteral("REACTION_RESUMED"))
      {
            PhaseState state;
            state.phase = SessionPhase::WaitingForReaction;
            state.phaseSequence = fields.value(QStringLiteral("phaseSeq")).toULongLong();
            state.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            state.remainingMs = fields.value(QStringLiteral("remainingMs")).toUInt();
            state.durationMs = state.remainingMs;
            emit phaseReceived(state);
            return;
      }
      if (frame.command == QStringLiteral("FORALL_RESULT"))
      {
            ForAllResult result;
            result.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            const int count = fields.value(QStringLiteral("resultCount")).toInt();
            for (int index = 0; index < count; ++index)
            {
                  AnswerResult answer;
                  answer.questionSequence = result.questionSequence;
                  answer.playerId = fields.value(QStringLiteral("player%1").arg(index));
                  answer.correct = fields.value(QStringLiteral("correct%1").arg(index)) ==
                                   QStringLiteral("1");
                  answer.amount = fields.value(QStringLiteral("amount%1").arg(index)).toInt();
                  answer.balance = fields.value(QStringLiteral("balance%1").arg(index)).toInt();
                  parseAnswerType(
                        fields.value(QStringLiteral("answerKind%1").arg(index)),
                        &answer.answerKind);
                  answer.submitted =
                        fields.value(QStringLiteral("submitted%1").arg(index));
                  result.results.push_back(answer);
            }
            emit forAllResultReceived(result);
            return;
      }
      if (frame.command == QStringLiteral("SECRET_TARGETS"))
      {
            QVector<PlayerState> targets;
            const int count = fields.value(QStringLiteral("count")).toInt();
            for (int index = 0; index < count; ++index)
            {
                  PlayerState target;
                  target.id = fields.value(QStringLiteral("target%1").arg(index));
                  target.nickname = fields.value(QStringLiteral("target%1Name").arg(index));
                  target.connected = true;
                  targets.push_back(target);
            }
            emit secretTargetListReceived(targets);
            return;
      }
      if (frame.command == QStringLiteral("SECRET_WAGER_PROMPT"))
      {
            SecretWagerParameters parameters;
            parameters.minimum = fields.value(QStringLiteral("minimum")).toInt();
            parameters.maximum = fields.value(QStringLiteral("maximum")).toInt();
            parameters.step = fields.value(QStringLiteral("step")).toInt();
            parameters.theme = fields.value(QStringLiteral("secretTheme"));
            emit secretWagerPromptReceived(parameters);
            return;
      }
      if (frame.command == QStringLiteral("ANSWER_REVEAL"))
      {
            AnswerReveal reveal;
            reveal.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            const int count = fields.value(QStringLiteral("rightCount")).toInt();
            for (int index = 0; index < count; ++index)
            {
                  reveal.rightAnswers.push_back(
                        fields.value(QStringLiteral("right%1").arg(index)));
            }
            parseMediaType(fields.value(QStringLiteral("answerMediaType")),
                           &reveal.answerMediaType);
            reveal.answerMediaPath = fields.value(QStringLiteral("answerMediaPath"));
            reveal.mediaDurationMs =
                  fields.value(QStringLiteral("mediaDurationMs")).toUInt();
            reveal.answerOwner = fields.value(QStringLiteral("answerOwner"));
            reveal.nextPicker = fields.value(QStringLiteral("nextPicker"));
            if (m_snapshotOpen)
            {
                  m_snapshot.reveal = reveal;
            }
            else
            {
                  emit revealReceived(reveal);
            }
            return;
      }
      if (frame.command == QStringLiteral("APPEAL_OPEN"))
      {
            AppealState appeal;
            appeal.appealId = fields.value(QStringLiteral("appealId")).toULongLong();
            appeal.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            appeal.appellant = fields.value(QStringLiteral("appellant"));
            appeal.submitted = fields.value(QStringLiteral("submitted"));
            const int rightCount =
                  fields.value(QStringLiteral("rightCount")).toInt();
            for (int index = 0; index < rightCount; ++index)
            {
                  appeal.rightAnswers.push_back(
                        fields.value(QStringLiteral("right%1").arg(index)));
            }
            parseMediaType(fields.value(QStringLiteral("answerMediaType")),
                           &appeal.answerMediaType);
            appeal.answerMediaPath =
                  fields.value(QStringLiteral("answerMediaPath"));
            appeal.durationMs = fields.value(QStringLiteral("durationMs")).toUInt();
            if (m_snapshotOpen)
            {
                  m_snapshot.appeal = appeal;
            }
            else
            {
                  emit appealReceived(appeal);
            }
            return;
      }
      if (frame.command == QStringLiteral("APPEAL_RESULT"))
      {
            AppealResult result;
            result.appealId = fields.value(QStringLiteral("appealId")).toULongLong();
            result.questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
            result.accepted = fields.value(QStringLiteral("accepted")) ==
                              QStringLiteral("1");
            result.appellant = fields.value(QStringLiteral("appellant"));
            result.correction = fields.value(QStringLiteral("correction")).toInt();
            result.balance = fields.value(QStringLiteral("balance")).toInt();
            result.nextPicker = fields.value(QStringLiteral("nextPicker"));
            emit appealResultReceived(result);
            return;
      }
      if (frame.command == QStringLiteral("PAUSE_STATE"))
      {
            SessionPhase phase = SessionPhase::Lobby;
            if (!parsePhaseName(fields.value(QStringLiteral("phase")), &phase))
            {
                  emit protocolError(QStringLiteral("BAD_FIELD"),
                                     QStringLiteral("Invalid pause phase"));
                  return;
            }
            emit pauseReceived(fields.value(QStringLiteral("paused")) ==
                                     QStringLiteral("1"),
                               phase,
                               fields.value(QStringLiteral("remainingMs")).toUInt());
            return;
      }
      if (frame.command == QStringLiteral("PING"))
      {
            sendCommand(QStringLiteral("PONG"),
                        {{QStringLiteral("pingId"),
                          fields.value(QStringLiteral("pingId"))}});
            return;
      }
      if (frame.command == QStringLiteral("SNAPSHOT_BEGIN"))
      {
            startSnapshot(fields);
            return;
      }
      if (frame.command == QStringLiteral("SNAPSHOT_CONFIG"))
      {
            m_config.maxPlayers = fields.value(QStringLiteral("maxPlayers")).toInt();
            m_config.answerDurationMs =
                  fields.value(QStringLiteral("answerDurationMs")).toUInt();
            m_config.questionDurationMs =
                  fields.value(QStringLiteral("questionDurationMs")).toUInt();
            m_config.questionPickDurationMs =
                  fields.value(QStringLiteral("questionPickDurationMs")).toUInt();
            m_config.answerWaitDurationMs =
                  fields.value(QStringLiteral("answerWaitDurationMs")).toUInt();
            return;
      }
      if (frame.command == QStringLiteral("SNAPSHOT_PLAYER"))
      {
            PlayerState state;
            state.id = fields.value(QStringLiteral("playerId"));
            state.nickname = fields.value(QStringLiteral("nickname"));
            state.connected = fields.value(QStringLiteral("connected")) ==
                              QStringLiteral("1");
            state.balance = fields.value(QStringLiteral("balance")).toInt();
            state.correctAnswers =
                  fields.value(QStringLiteral("correctAnswers")).toInt();
            state.wrongAnswers =
                  fields.value(QStringLiteral("wrongAnswers")).toInt();
            state.hasPassed = fields.value(QStringLiteral("hasPassed")) ==
                              QStringLiteral("1");
            state.answeredIncorrectly =
                  fields.value(QStringLiteral("answeredIncorrectly")) ==
                  QStringLiteral("1");
            state.mayAppeal = fields.value(QStringLiteral("mayAppeal")) ==
                              QStringLiteral("1");
            m_snapshot.players.push_back(state);
            return;
      }
      if (frame.command == QStringLiteral("SNAPSHOT_CELL"))
      {
            BoardCell cell;
            cell.round = fields.value(QStringLiteral("round")).toInt();
            cell.theme = fields.value(QStringLiteral("theme")).toInt();
            cell.question = fields.value(QStringLiteral("question")).toInt();
            cell.used = fields.value(QStringLiteral("used")) == QStringLiteral("1");
            m_snapshot.board.round = cell.round;
            m_snapshot.board.cells.push_back(cell);
            return;
      }
      if (frame.command == QStringLiteral("SNAPSHOT_END"))
      {
            finishSnapshot(fields);
            return;
      }
      if (frame.command == QStringLiteral("PLAYER_CONNECTION"))
      {
            const PlayerId id = fields.value(QStringLiteral("playerId"));
            for (PlayerState &state : m_roster)
            {
                  if (state.id == id)
                  {
                        state.connected = fields.value(QStringLiteral("connected")) ==
                                          QStringLiteral("1");
                        break;
                  }
            }
            emitLobby();
            return;
      }
      if (frame.command == QStringLiteral("GAME_FINISHED"))
      {
            emit finished();
            return;
      }
      if (frame.command == QStringLiteral("GAME_ABORTED"))
      {
            emit disconnected(tr("The host ended the game"));
            return;
      }
      if (frame.command == QStringLiteral("ERROR"))
      {
            emit protocolError(fields.value(QStringLiteral("code")),
                               fields.value(QStringLiteral("message")));
            return;
      }
}

bool MultiplayerClient::parsePhase(const QMap<QString, QString> &fields,
                                   PhaseState *state,
                                   QString *errorMessage) const
{
      if (state == nullptr)
      {
            return false;
      }
      SessionPhase phase;
      if (!parsePhaseName(fields.value(QStringLiteral("phase")), &phase))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Invalid phase");
            }
            return false;
      }
      state->phase = phase;
      state->phaseSequence = fields.value(QStringLiteral("phaseSeq")).toULongLong();
      state->questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
      state->durationMs = fields.value(QStringLiteral("durationMs")).toUInt();
      state->remainingMs = fields.value(QStringLiteral("remainingMs")).toUInt();
      state->owner = fields.value(QStringLiteral("owner"));
      state->paused = false;
      return true;
}

bool MultiplayerClient::parseQuestion(const QMap<QString, QString> &fields,
                                      QuestionPresentation *question,
                                      QString *errorMessage) const
{
      if (question == nullptr)
      {
            return false;
      }
      *question = {};
      question->questionSequence = fields.value(QStringLiteral("questionSeq")).toULongLong();
      question->round = fields.value(QStringLiteral("round")).toInt();
      question->theme = fields.value(QStringLiteral("theme")).toInt();
      question->question = fields.value(QStringLiteral("question")).toInt();
      question->themeName = fields.value(QStringLiteral("themeName"));
      question->price = fields.value(QStringLiteral("price")).toInt();
      if (!parseQuestionType(fields.value(QStringLiteral("questionType")),
                             &question->questionType) ||
          !parseAnswerType(fields.value(QStringLiteral("answerType")),
                           &question->answerType) ||
          !parseMediaType(fields.value(QStringLiteral("mediaType")),
                          &question->mediaType))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Invalid question metadata");
            }
            return false;
      }
      question->text = fields.value(QStringLiteral("text"));
      question->mediaPath = fields.value(QStringLiteral("mediaPath"));
      question->mediaDurationMs =
            fields.value(QStringLiteral("mediaDurationMs")).toUInt();
      question->answerDurationMs =
            fields.value(QStringLiteral("answerDurationMs")).toUInt();
      question->answerOwner = fields.value(QStringLiteral("answerOwner"));
      const int optionCount = fields.value(QStringLiteral("optionCount")).toInt();
      if (optionCount < 0 || optionCount > 1024)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Invalid option count");
            }
            return false;
      }
      for (int index = 0; index < optionCount; ++index)
      {
            question->answerOptions.push_back(
                  {fields.value(QStringLiteral("option%1Id").arg(index)),
                   fields.value(QStringLiteral("option%1Text").arg(index))});
      }
      return true;
}

bool MultiplayerClient::parseAnswerType(const QString &value,
                                        AnswerType *type) const
{
      if (type == nullptr)
      {
            return false;
      }
      if (value.compare(QStringLiteral("Text"), Qt::CaseInsensitive) == 0)
      {
            *type = AnswerType::Text;
      }
      else if (value.compare(QStringLiteral("Select"), Qt::CaseInsensitive) == 0)
      {
            *type = AnswerType::Select;
      }
      else if (value.compare(QStringLiteral("Point"), Qt::CaseInsensitive) == 0)
      {
            *type = AnswerType::Point;
      }
      else if (value.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0)
      {
            *type = AnswerType::Unknown;
      }
      else
      {
            return false;
      }
      return true;
}

bool MultiplayerClient::parseMediaType(const QString &value,
                                       MediaType *type) const
{
      if (type == nullptr)
      {
            return false;
      }
      const QStringList names = {QStringLiteral("None"), QStringLiteral("Image"),
                                 QStringLiteral("Audio"), QStringLiteral("Video")};
      for (int index = 0; index < names.size(); ++index)
      {
            if (value.compare(names[index], Qt::CaseInsensitive) == 0)
            {
                  *type = static_cast<MediaType>(index);
                  return true;
            }
      }
      return false;
}

bool MultiplayerClient::parseQuestionType(const QString &value,
                                           QuestionType *type) const
{
      if (type == nullptr)
      {
            return false;
      }
      const QStringList names = {QStringLiteral("Default"),
                                 QStringLiteral("ForAll"),
                                 QStringLiteral("SecretPublicPrice"),
                                 QStringLiteral("Unknown")};
      for (int index = 0; index < names.size(); ++index)
      {
            if (value.compare(names[index], Qt::CaseInsensitive) == 0)
            {
                  *type = static_cast<QuestionType>(index);
                  return true;
            }
      }
      return false;
}

bool MultiplayerClient::parseUnsigned(const QMap<QString, QString> &fields,
                                       const QString &name, quint64 *value) const
{
      QString error;
      return MultiplayerProtocol::parseUnsigned(fields, name, value, &error);
}

void MultiplayerClient::parseRosterPlayer(
      const QMap<QString, QString> &fields)
{
      PlayerState state;
      state.id = fields.value(QStringLiteral("playerId"));
      state.nickname = fields.value(QStringLiteral("nickname"));
      state.connected = fields.value(QStringLiteral("connected")) ==
                        QStringLiteral("1");
      state.ready = fields.value(QStringLiteral("ready")) == QStringLiteral("1");
      state.balance = fields.value(QStringLiteral("balance")).toInt();
      state.correctAnswers =
            fields.value(QStringLiteral("correctAnswers")).toInt();
      state.wrongAnswers =
            fields.value(QStringLiteral("wrongAnswers")).toInt();
      state.hasPassed = fields.value(QStringLiteral("hasPassed")) ==
                        QStringLiteral("1");
      state.answeredIncorrectly =
            fields.value(QStringLiteral("answeredIncorrectly")) ==
            QStringLiteral("1");
      state.hasAnsweredForAll =
            fields.value(QStringLiteral("hasAnsweredForAll")) ==
            QStringLiteral("1");
      state.mayAppeal = fields.value(QStringLiteral("mayAppeal")) ==
                        QStringLiteral("1");
      state.isPicker = fields.value(QStringLiteral("isPicker")) ==
                       QStringLiteral("1");
      state.profilePng = m_profileCache.value(state.id);
      updateRosterPlayer(state);
}

void MultiplayerClient::handleProfileBegin(
      const QMap<QString, QString> &fields)
{
      m_profile = {};
      m_profile.transferId = fields.value(QStringLiteral("transferId"));
      m_profile.playerId = fields.value(QStringLiteral("playerId"));
      m_profile.expectedBytes = fields.value(QStringLiteral("bytes")).toInt();
      m_profile.hash = fields.value(QStringLiteral("sha256")).toLatin1();
      if (m_profile.expectedBytes < 0 ||
          m_profile.expectedBytes > MultiplayerProtocol::MaxProfileBytes)
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("Invalid profile size"));
            m_profile.expectedBytes = -1;
      }
}

void MultiplayerClient::handleProfileChunk(
      const QMap<QString, QString> &fields)
{
      if (m_profile.expectedBytes < 0 ||
          fields.value(QStringLiteral("transferId")) != m_profile.transferId)
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("No profile transfer is active"));
            return;
      }
      const int index = fields.value(QStringLiteral("index")).toInt();
      if (index != m_profile.nextChunk)
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("Profile chunks are out of order"));
            return;
      }
      const QByteArray chunk = profileFromBase64(fields.value(QStringLiteral("data")));
      if (chunk.size() > MultiplayerProtocol::MaxProfileChunkBytes ||
          m_profile.data.size() + chunk.size() > m_profile.expectedBytes)
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("Invalid profile chunk"));
            return;
      }
      m_profile.data += chunk;
      ++m_profile.nextChunk;
}

void MultiplayerClient::handleProfileEnd(const QMap<QString, QString> &fields)
{
      if (m_profile.expectedBytes < 0 ||
          fields.value(QStringLiteral("transferId")) != m_profile.transferId ||
          m_profile.data.size() != m_profile.expectedBytes ||
          QImage::fromData(m_profile.data).isNull())
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("Invalid profile transfer"));
            return;
      }
      const QByteArray hash = QCryptographicHash::hash(
            m_profile.data, QCryptographicHash::Sha256)
                                  .toHex();
      if (hash.compare(fields.value(QStringLiteral("sha256")).toLatin1(),
                       Qt::CaseInsensitive) != 0)
      {
            emit protocolError(QStringLiteral("PROFILE_INVALID"),
                               QStringLiteral("Profile hash mismatch"));
            return;
      }
      m_profileCache.insert(m_profile.playerId, m_profile.data);
      for (PlayerState &state : m_roster)
      {
            if (state.id == m_profile.playerId)
            {
                  state.profilePng = m_profile.data;
                  break;
            }
      }
      if (!m_rosterOpen)
      {
            emitLobby();
      }
      m_profile = {};
}

void MultiplayerClient::startSnapshot(const QMap<QString, QString> &fields)
{
      m_snapshot = {};
      m_snapshotOpen = true;
      m_snapshot.snapshotSequence = fields.value(QStringLiteral("snapshotSeq")).toULongLong();
      if (m_snapshot.snapshotSequence < m_lastSnapshotSequence)
      {
            m_snapshotOpen = false;
            return;
      }
      m_lastSnapshotSequence = m_snapshot.snapshotSequence;
      m_snapshot.sessionId = fields.value(QStringLiteral("sessionId"));
      m_sessionId = m_snapshot.sessionId;
      m_snapshot.currentPicker = fields.value(QStringLiteral("currentPicker"));
      m_snapshot.answerOwner = fields.value(QStringLiteral("answerOwner"));
      m_snapshot.board.boardSequence =
            fields.value(QStringLiteral("boardSeq")).toULongLong();
      m_snapshot.board.round = fields.value(QStringLiteral("round")).toInt();
      parsePhase(fields, &m_snapshot.phase);
      m_snapshot.phase.paused = fields.value(QStringLiteral("paused")) ==
                                QStringLiteral("1");
      m_snapshot.phase.remainingMs =
            fields.value(QStringLiteral("remainingMs")).toUInt();
      m_snapshot.paused = m_snapshot.phase.paused;
}

void MultiplayerClient::finishSnapshot(const QMap<QString, QString> &)
{
      if (!m_snapshotOpen)
      {
            return;
      }
      m_snapshotOpen = false;
      for (const PlayerState &state : m_roster)
      {
            if (!state.profilePng.isEmpty())
            {
                  m_profileCache.insert(state.id, state.profilePng);
            }
      }
      m_roster = m_snapshot.players;
      for (PlayerState &state : m_roster)
      {
            state.profilePng = m_profileCache.value(state.id);
      }
      m_snapshot.players = m_roster;
      emit snapshotApplied(m_snapshot);
      emitLobby();
}

void MultiplayerClient::emitLobby() { emit lobbyChanged(m_roster); }

void MultiplayerClient::updateRosterPlayer(const PlayerState &state)
{
      for (PlayerState &existing : m_roster)
      {
            if (existing.id == state.id)
            {
                  const QByteArray profile = existing.profilePng;
                  existing = state;
                  if (existing.profilePng.isEmpty())
                  {
                        existing.profilePng = profile;
                  }
                  return;
            }
      }
      m_roster.push_back(state);
}

bool MultiplayerClient::parsePhaseName(const QString &value,
                                       SessionPhase *phase) const
{
      return MultiplayerProtocol::phaseFromName(value, phase);
}
