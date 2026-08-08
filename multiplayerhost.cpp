#include "multiplayerhost.h"

#include "multiplayerprotocol.h"
#include "packmanifest.h"
#include "pingworker.h"

#include <QCryptographicHash>
#include <QImage>
#include <QLocale>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace
{
QString stringValue(quint64 value) { return QString::number(value); }
QString stringValue(qint64 value) { return QString::number(value); }
QString stringValue(unsigned int value) { return QString::number(value); }
QString stringValue(int value) { return QString::number(value); }

QString usedCellsValue(const BoardState &state)
{
      QStringList cells;
      for (const BoardCell &cell : state.cells)
      {
            if (cell.used)
            {
                  cells.push_back(QStringLiteral("%1:%2")
                                        .arg(cell.theme)
                                        .arg(cell.question));
            }
      }
      return cells.join(QLatin1Char(','));
}

bool parseIntField(const QMap<QString, QString> &fields, const QString &name,
                  int *value)
{
      if (value == nullptr || !fields.contains(name))
      {
            return false;
      }
      bool ok = false;
      const int parsed = fields.value(name).toInt(&ok);
      if (!ok)
      {
            return false;
      }
      *value = parsed;
      return true;
}

bool parseUIntField(const QMap<QString, QString> &fields, const QString &name,
                   quint64 *value)
{
      QString error;
      return MultiplayerProtocol::parseUnsigned(fields, name, value, &error);
}

QMap<QString, QString> questionFields(const QuestionPresentation &question)
{
      QMap<QString, QString> fields;
      fields.insert(QStringLiteral("questionSeq"),
                    stringValue(question.questionSequence));
      fields.insert(QStringLiteral("round"), stringValue(question.round));
      fields.insert(QStringLiteral("theme"), stringValue(question.theme));
      fields.insert(QStringLiteral("question"), stringValue(question.question));
      fields.insert(QStringLiteral("themeName"), question.themeName);
      fields.insert(QStringLiteral("price"), stringValue(question.price));
      fields.insert(QStringLiteral("questionType"),
                    MultiplayerProtocol::questionTypeName(
                          static_cast<int>(question.questionType)));
      fields.insert(QStringLiteral("answerType"),
                    MultiplayerProtocol::answerTypeName(
                          static_cast<int>(question.answerType)));
      fields.insert(QStringLiteral("text"), question.text);
      fields.insert(QStringLiteral("mediaType"),
                    MultiplayerProtocol::mediaTypeName(
                          static_cast<int>(question.mediaType)));
      fields.insert(QStringLiteral("mediaPath"), question.mediaPath);
      fields.insert(QStringLiteral("answerDurationMs"),
                    stringValue(question.answerDurationMs));
      fields.insert(QStringLiteral("answerOwner"), question.answerOwner);
      fields.insert(QStringLiteral("optionCount"),
                    stringValue(question.answerOptions.size()));
      for (int index = 0; index < question.answerOptions.size(); ++index)
      {
            fields.insert(QStringLiteral("option%1Id").arg(index),
                          question.answerOptions[index].id);
            fields.insert(QStringLiteral("option%1Text").arg(index),
                          question.answerOptions[index].text);
      }
      return fields;
}
} // namespace

MultiplayerHost::MultiplayerHost(const GameConfig &config, const Game &game,
                                 const PlayerIdentity &identity,
                                 QObject *parent)
      : QObject(parent), m_config(config), m_game(game), m_identity(identity),
        m_localPlayerId(QStringLiteral("player-host-%1")
                              .arg(identity.token.left(12))),
        m_session(new GameSession(game, config, this)),
        m_server(new QTcpServer(this))
{
      if (m_identity.token.isEmpty())
      {
            m_identity = PlayerIdentity::load();
            m_localPlayerId = QStringLiteral("player-host-%1")
                                    .arg(m_identity.token.left(12));
      }
      PlayerState host;
      host.id = m_localPlayerId;
      host.token = m_identity.token;
      host.nickname = m_identity.nickname;
      host.profilePng = m_identity.profilePng;
      host.connected = true;
      host.ready = true;
      m_session->addPlayer(host);
      connect(m_server, &QTcpServer::newConnection, this,
              &MultiplayerHost::acceptPendingConnections);
      m_syncTimer.setInterval(500);
      connect(&m_syncTimer, &QTimer::timeout, this,
              &MultiplayerHost::sendPhaseSync);
      connectSessionSignals();
}

MultiplayerHost::MultiplayerHost(const GameConfig &config, const Game &game,
                                 QObject *parent)
      : MultiplayerHost(config, game, PlayerIdentity::load(), parent)
{
}

MultiplayerHost::~MultiplayerHost() { stop(); }

bool MultiplayerHost::listen(quint16 port)
{
      if (m_server->isListening())
      {
            return true;
      }
      if (!m_server->listen(QHostAddress::AnyIPv4, port))
      {
            emit listeningFailed(m_server->errorString());
            return false;
      }
      startPings();
      emit listeningStarted(m_server->serverPort(), localAddresses());
      sendRoster();
      return true;
}

void MultiplayerHost::stop()
{
      if (m_started)
      {
            broadcast(QStringLiteral("GAME_ABORTED"),
                      {{QStringLiteral("sessionId"), m_session->sessionId()},
                       {QStringLiteral("reason"), QStringLiteral("host-ended")}});
      }
      for (auto iterator = m_peers.begin(); iterator != m_peers.end(); ++iterator)
      {
            if (iterator.value().connection != nullptr)
            {
                  iterator.value().connection->close();
                  iterator.value().connection->deleteLater();
            }
      }
      m_peers.clear();
      if (m_server != nullptr)
      {
            m_server->close();
      }
      m_syncTimer.stop();
      stopPings();
      m_started = false;
}

void MultiplayerHost::startGame()
{
      if (m_started)
      {
            return;
      }
      m_started = true;
      sendGameStarted();
      m_session->startGame();
      m_syncTimer.start();
}

bool MultiplayerHost::isListening() const
{
      return m_server != nullptr && m_server->isListening();
}

void MultiplayerHost::requestSnapshot(PlayerId playerId)
{
      Peer *peer = peerForPlayer(playerId);
      if (peer != nullptr)
      {
            sendSnapshot(*peer);
      }
}

void MultiplayerHost::onReactionClaim(PlayerId playerId,
                                       quint64 questionSequence,
                                       quint64 phaseSequence, quint64 actionId,
                                       unsigned int elapsedMs)
{
      unsigned int effectiveElapsed = elapsedMs;
      if (playerId == m_localPlayerId)
      {
            double totalOneWay = 0.0;
            int count = 0;
            for (auto iterator = m_remoteRtt.cbegin();
                 iterator != m_remoteRtt.cend(); ++iterator)
            {
                  if (isListening() && isConnected(iterator.key()))
                  {
                        totalOneWay += iterator.value() / 2.0;
                        ++count;
                  }
            }
            if (count > 0)
            {
                  effectiveElapsed += static_cast<unsigned int>(
                        std::lround(totalOneWay / count));
            }
      }
      if (playerId == m_localPlayerId)
      {
            m_session->submitReaction(playerId, questionSequence,
                                      phaseSequence, actionId, elapsedMs,
                                      effectiveElapsed);
      }
      else
      {
            m_session->submitReaction(playerId, questionSequence,
                                      phaseSequence, actionId, elapsedMs);
      }
}

void MultiplayerHost::onAnswerSubmitted(PlayerId playerId,
                                         quint64 questionSequence,
                                         quint64 phaseSequence,
                                         quint64 actionId,
                                         const AnswerSubmission &submission)
{
      m_session->submitAnswer(playerId, questionSequence, phaseSequence,
                              actionId, submission);
}

void MultiplayerHost::onAnswerDraftChanged(
      PlayerId playerId, quint64 questionSequence, quint64 phaseSequence,
      quint64 actionId, const QString &answer)
{
      AnswerSubmission submission;
      submission.answerType = AnswerType::Text;
      submission.answer = answer;
      m_session->updateAnswerDraft(playerId, questionSequence, phaseSequence,
                                   actionId, submission);
}

void MultiplayerHost::onQuestionSelected(PlayerId playerId, int roundIndex,
                                         int themeIndex, int questionIndex,
                                         quint64 actionId)
{
      m_session->selectQuestion(playerId, roundIndex, themeIndex, questionIndex,
                                actionId);
}

void MultiplayerHost::onSecretTargetSelected(PlayerId pickerId,
                                             PlayerId targetId,
                                             quint64 questionSequence,
                                             quint64 actionId)
{
      m_session->selectSecretTarget(pickerId, targetId, questionSequence,
                                    actionId);
}

void MultiplayerHost::onSecretWagerSubmitted(PlayerId targetId, int amount,
                                             quint64 questionSequence,
                                             quint64 actionId)
{
      m_session->submitSecretWager(targetId, amount, questionSequence, actionId);
}

void MultiplayerHost::onPass(PlayerId playerId, quint64 questionSequence,
                             quint64 phaseSequence, quint64 actionId)
{
      m_session->passQuestion(playerId, questionSequence, phaseSequence,
                              actionId);
}

void MultiplayerHost::onPauseRequested(PlayerId playerId, bool paused,
                                       quint64 phaseSequence, quint64 actionId)
{
      m_session->requestPause(playerId, paused, actionId, phaseSequence);
}

void MultiplayerHost::onAppealRequested(PlayerId playerId,
                                        quint64 questionSequence,
                                        quint64 actionId)
{
      m_session->requestAppeal(playerId, questionSequence, actionId);
}

void MultiplayerHost::onAppealVote(PlayerId playerId, quint64 appealId,
                                   bool accepted, quint64 actionId)
{
      m_session->submitAppealVote(playerId, appealId, accepted, actionId);
}

void MultiplayerHost::acceptPendingConnections()
{
      while (m_server->hasPendingConnections())
      {
            QTcpSocket *socket = m_server->nextPendingConnection();
            auto *connection = new MultiplayerConnection(this);
            Peer peer;
            peer.connection = connection;
            m_peers.insert(connection, peer);
            connection->adoptSocket(socket);
            connect(connection, &MultiplayerConnection::lineReceived, this,
                    &MultiplayerHost::handleLine);
            connect(connection, &MultiplayerConnection::disconnected, this,
                    &MultiplayerHost::handleDisconnected);
            connect(connection, &MultiplayerConnection::transportError, this,
                    &MultiplayerHost::handleTransportError);
      }
}

void MultiplayerHost::handleLine(const QByteArray &line)
{
      auto *connection = qobject_cast<MultiplayerConnection *>(sender());
      Peer *peer = peerForConnection(connection);
      if (peer == nullptr)
      {
            return;
      }
      MultiplayerProtocol::Frame frame;
      QString error;
      if (!MultiplayerProtocol::parseFrame(line, &frame, &error))
      {
            sendError(*peer, QStringLiteral("BAD_FRAME"), error);
            return;
      }
      if (!peer->handshaken && frame.command != QStringLiteral("HELLO"))
      {
            sendError(*peer, QStringLiteral("HANDSHAKE_REQUIRED"),
                      QStringLiteral("HELLO must be the first command"));
            peer->connection->close();
            return;
      }
      handleFrame(*peer, frame);
}

void MultiplayerHost::handleDisconnected()
{
      auto *connection = qobject_cast<MultiplayerConnection *>(sender());
      Peer *peer = peerForConnection(connection);
      if (peer == nullptr)
      {
            return;
      }
      const PlayerId playerId = peer->playerId;
      if (!playerId.isEmpty())
      {
            m_session->setPlayerConnected(playerId, false);
            emit playerDisconnected(playerId);
            broadcast(QStringLiteral("PLAYER_CONNECTION"),
                      {{QStringLiteral("playerId"), playerId},
                       {QStringLiteral("connected"), QStringLiteral("0")},
                       {QStringLiteral("reserved"), QStringLiteral("1")}});
            sendRoster();
      }
      m_peers.remove(connection);
      if (connection != nullptr)
      {
            connection->deleteLater();
      }
}

void MultiplayerHost::handleTransportError(const QString &message)
{
      auto *connection = qobject_cast<MultiplayerConnection *>(sender());
      Peer *peer = peerForConnection(connection);
      if (peer != nullptr)
      {
            sendError(*peer, QStringLiteral("BAD_FRAME"), message,
                      peer->requestId);
            peer->connection->close();
      }
      else
      {
            emit protocolError({}, QStringLiteral("BAD_FRAME"), message);
      }
}

void MultiplayerHost::handleFrame(Peer &peer,
                                  const MultiplayerProtocol::Frame &frame)
{
      if (frame.command == QStringLiteral("HELLO"))
      {
            if (peer.handshaken)
            {
                  sendError(peer, QStringLiteral("BAD_FRAME"),
                            QStringLiteral("HELLO was already received"));
            }
            else
            {
                  handleHello(peer, frame.fields);
            }
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_BEGIN"))
      {
            handleProfileBegin(peer, frame.fields);
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_CHUNK"))
      {
            handleProfileChunk(peer, frame.fields);
            return;
      }
      if (frame.command == QStringLiteral("PROFILE_END"))
      {
            handleProfileEnd(peer, frame.fields);
            return;
      }
      if (frame.command == QStringLiteral("READY"))
      {
            handleReady(peer, frame.fields);
            return;
      }
      if (frame.command == QStringLiteral("PONG"))
      {
            quint64 pingId = 0;
            if (!parseUIntField(frame.fields, QStringLiteral("pingId"), &pingId))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid ping ID"));
                  return;
            }
            if (m_pingWorker != nullptr)
            {
                  QMetaObject::invokeMethod(
                        m_pingWorker, "pingCompleted", Qt::QueuedConnection,
                        Q_ARG(QString, peer.playerId), Q_ARG(quint64, pingId));
            }
            return;
      }
      if (frame.command == QStringLiteral("PING"))
      {
            sendError(peer, QStringLiteral("BAD_FRAME"),
                      QStringLiteral("PING is sent by the host"));
            return;
      }
      if (!MultiplayerProtocol::isKnownCommand(frame.command))
      {
            sendError(peer, QStringLiteral("BAD_FRAME"),
                      QStringLiteral("Unknown command"),
                      frame.fields.value(QStringLiteral("requestId")));
            return;
      }
      if (!peer.ready)
      {
            sendError(peer, QStringLiteral("HANDSHAKE_REQUIRED"),
                      QStringLiteral("The player is not ready"));
            return;
      }
      handleAction(peer, frame);
}

void MultiplayerHost::handleHello(Peer &peer,
                                  const QMap<QString, QString> &fields)
{
      quint64 protocol = 0;
      if (!parseUIntField(fields, QStringLiteral("protocol"), &protocol) ||
          protocol != MultiplayerProtocol::ProtocolVersion)
      {
            sendError(peer, QStringLiteral("BAD_PROTOCOL"),
                      QStringLiteral("Unsupported protocol version"),
                      fields.value(QStringLiteral("requestId")));
            return;
      }
      const QString token = fields.value(QStringLiteral("token"));
      const QString receivedHash = fields.value(QStringLiteral("packHash"));
      const QString requestId = fields.value(QStringLiteral("requestId"));
      if (token.isEmpty() || receivedHash.isEmpty())
      {
            sendError(peer, QStringLiteral("BAD_FIELD"),
                      QStringLiteral("HELLO is missing identity fields"),
                      requestId);
            return;
      }
      if (!m_config.packHash.isEmpty() &&
          receivedHash.compare(m_config.packHash, Qt::CaseInsensitive) != 0)
      {
            emit packMismatch({}, m_config.packHash, receivedHash);
            sendFrame(peer, QStringLiteral("ERROR"),
                      {{QStringLiteral("code"),
                        QStringLiteral("PACK_MISMATCH")},
                       {QStringLiteral("message"),
                        QStringLiteral("The local pack does not match the host")},
                       {QStringLiteral("expectedHash"), m_config.packHash},
                       {QStringLiteral("receivedHash"), receivedHash},
                       {QStringLiteral("requestId"), requestId}});
            emit protocolError(peer.playerId, QStringLiteral("PACK_MISMATCH"),
                               QStringLiteral("The local pack does not match the host"));
            peer.connection->close();
            return;
      }

      const PlayerId reservedId = m_session->playerIdForToken(token);
      if (!reservedId.isEmpty())
      {
            if (hasActivePeerForPlayer(reservedId))
            {
                  sendError(peer, QStringLiteral("LOBBY_FULL"),
                            QStringLiteral("This player token is already connected"),
                            requestId);
                  peer.connection->close();
                  return;
            }
            peer.playerId = reservedId;
            peer.token = token;
            peer.handshaken = true;
            peer.profileTransferId = fields.value(QStringLiteral("profileTransfer"));
            peer.profilePending = peer.profileTransferId != QStringLiteral("none") &&
                                  !peer.profileTransferId.isEmpty();
            peer.expectedProfileBytes = -1;
            m_session->setPlayerConnected(reservedId, true);
            const PlayerState *state = m_session->player(reservedId);
            sendFrame(peer, QStringLiteral("WELCOME"),
                      {{QStringLiteral("protocol"),
                        QString::number(MultiplayerProtocol::ProtocolVersion)},
                       {QStringLiteral("sessionId"), m_session->sessionId()},
                       {QStringLiteral("playerId"), reservedId},
                       {QStringLiteral("reconnect"), QStringLiteral("1")},
                       {QStringLiteral("maxPlayers"),
                        QString::number(m_config.maxPlayers)},
                       {QStringLiteral("packHash"), m_config.packHash},
                       {QStringLiteral("requestId"), requestId}});
            sendFrame(peer, QStringLiteral("PACK_OK"),
                      {{QStringLiteral("hash"), m_config.packHash},
                       {QStringLiteral("requestId"), requestId}});
            if (state != nullptr && !state->profilePng.isEmpty())
            {
                  sendProfile(peer, *state);
            }
            emit playerReconnected(state == nullptr ? PlayerState() : *state);
            return;
      }

      if (m_session->players().size() >= m_config.maxPlayers)
      {
            sendError(peer, QStringLiteral("LOBBY_FULL"),
                      QStringLiteral("The lobby has no free slots"), requestId);
            peer.connection->close();
            return;
      }
      peer.playerId = QStringLiteral("player-%1")
                            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
      peer.token = token;
      PlayerState state;
      state.id = peer.playerId;
      state.token = token;
      state.nickname = fields.value(QStringLiteral("nickname"));
      state.connected = true;
      state.ready = false;
      if (!m_session->addPlayer(state))
      {
            sendError(peer, QStringLiteral("LOBBY_FULL"),
                      QStringLiteral("The player could not be added"), requestId);
            peer.connection->close();
            return;
      }
      peer.handshaken = true;
      peer.profileTransferId = fields.value(QStringLiteral("profileTransfer"));
      peer.profilePending = peer.profileTransferId != QStringLiteral("none") &&
                            !peer.profileTransferId.isEmpty();
      sendFrame(peer, QStringLiteral("WELCOME"),
                {{QStringLiteral("protocol"),
                  QString::number(MultiplayerProtocol::ProtocolVersion)},
                 {QStringLiteral("sessionId"), m_session->sessionId()},
                 {QStringLiteral("playerId"), peer.playerId},
                 {QStringLiteral("reconnect"), QStringLiteral("0")},
                 {QStringLiteral("maxPlayers"),
                  QString::number(m_config.maxPlayers)},
                 {QStringLiteral("packHash"), m_config.packHash},
                 {QStringLiteral("requestId"), requestId}});
      sendFrame(peer, QStringLiteral("PACK_OK"),
                {{QStringLiteral("hash"), m_config.packHash},
                 {QStringLiteral("requestId"), requestId}});
      emit playerJoined(state);
      sendRoster();
}

void MultiplayerHost::handleProfileBegin(
      Peer &peer, const QMap<QString, QString> &fields)
{
      if (!peer.handshaken || !peer.profilePending)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("No profile transfer is expected"));
            return;
      }
      if (fields.value(QStringLiteral("transferId")) != peer.profileTransferId)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("The profile transfer ID is invalid"));
            return;
      }
      int bytes = 0;
      quint64 parsedBytes = 0;
      if (!parseUIntField(fields, QStringLiteral("bytes"), &parsedBytes) ||
          parsedBytes > static_cast<quint64>(MultiplayerProtocol::MaxProfileBytes))
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("Invalid profile size"));
            return;
      }
      bytes = static_cast<int>(parsedBytes);
      peer.expectedProfileBytes = bytes;
      peer.expectedProfileHash =
            fields.value(QStringLiteral("sha256")).toLatin1();
      peer.profileData.clear();
      peer.nextProfileChunk = 0;
}

void MultiplayerHost::handleProfileChunk(
      Peer &peer, const QMap<QString, QString> &fields)
{
      if (!peer.handshaken || peer.expectedProfileBytes < 0)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("No profile transfer is active"));
            return;
      }
      int index = 0;
      if (!parseIntField(fields, QStringLiteral("index"), &index) ||
          index != peer.nextProfileChunk)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("Profile chunks are out of order"));
            return;
      }
      const QByteArray chunk = profileFromBase64(fields.value(QStringLiteral("data")));
      if (chunk.size() > MultiplayerProtocol::MaxProfileChunkBytes ||
          peer.profileData.size() + chunk.size() > peer.expectedProfileBytes ||
          peer.profileData.size() + chunk.size() > MultiplayerProtocol::MaxProfileBytes)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("Invalid profile chunk"));
            return;
      }
      peer.profileData += chunk;
      ++peer.nextProfileChunk;
}

void MultiplayerHost::handleProfileEnd(Peer &peer,
                                       const QMap<QString, QString> &fields)
{
      if (!peer.handshaken || peer.expectedProfileBytes < 0 ||
          fields.value(QStringLiteral("transferId")) != peer.profileTransferId ||
          peer.profileData.size() != peer.expectedProfileBytes)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("The profile transfer is incomplete"));
            return;
      }
      const QByteArray hash = QCryptographicHash::hash(
            peer.profileData, QCryptographicHash::Sha256)
                                  .toHex();
      if (QImage::fromData(peer.profileData).isNull())
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("The profile is not a valid image"));
            return;
      }
      if (hash.compare(fields.value(QStringLiteral("sha256")).toLatin1(),
                       Qt::CaseInsensitive) != 0)
      {
            sendError(peer, QStringLiteral("PROFILE_INVALID"),
                      QStringLiteral("The profile hash does not match"));
            return;
      }
      m_session->updatePlayerProfile(peer.playerId, peer.profileData);
      peer.profilePending = false;
      peer.expectedProfileBytes = -1;
      sendRoster();
}

void MultiplayerHost::handleReady(Peer &peer,
                                  const QMap<QString, QString> &fields)
{
      if (!peer.handshaken ||
          fields.value(QStringLiteral("sessionId")) != m_session->sessionId() ||
          fields.value(QStringLiteral("playerId")) != peer.playerId ||
          peer.profilePending)
      {
            sendError(peer, QStringLiteral("HANDSHAKE_REQUIRED"),
                      QStringLiteral("The player is not ready"),
                      fields.value(QStringLiteral("requestId")));
            return;
      }
      peer.ready = true;
      m_session->setPlayerReady(peer.playerId, true);
      sendRoster();
      if (m_started)
      {
            sendGameStarted();
            sendSnapshot(peer);
      }
      else
      {
            const QVector<PlayerState> roster = m_session->players();
            const int connected = static_cast<int>(std::count_if(
                  roster.cbegin(), roster.cend(),
                  [](const PlayerState &state)
                  { return state.connected && state.ready; }));
            if (connected >= m_config.maxPlayers)
            {
                  startGame();
            }
      }
}

void MultiplayerHost::handleAction(Peer &peer,
                                   const MultiplayerProtocol::Frame &frame)
{
      const QMap<QString, QString> &fields = frame.fields;
      peer.requestId = fields.value(QStringLiteral("requestId"));
      m_lastRequestId = peer.requestId;
      quint64 actionId = 0;
      if (!parseUIntField(fields, QStringLiteral("actionId"), &actionId) ||
          actionId == 0)
      {
            sendError(peer, QStringLiteral("BAD_FIELD"),
                      QStringLiteral("An action ID is required"), peer.requestId);
            return;
      }
      quint64 questionSequence = 0;
      quint64 phaseSequence = 0;
      if (frame.command == QStringLiteral("SELECT_QUESTION"))
      {
            int round = 0;
            int theme = 0;
            int question = 0;
            quint64 requestedPhase = 0;
            if (!parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &requestedPhase))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("A phase sequence is required"),
                            peer.requestId);
                  return;
            }
            if (requestedPhase != m_session->phaseSequence())
            {
                  sendError(peer, QStringLiteral("STALE_SEQUENCE"),
                            QStringLiteral("The phase sequence is stale"),
                            peer.requestId);
                  return;
            }
            if (!parseIntField(fields, QStringLiteral("round"), &round) ||
                !parseIntField(fields, QStringLiteral("theme"), &theme) ||
                !parseIntField(fields, QStringLiteral("question"), &question))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid question coordinates"),
                            peer.requestId);
                  return;
            }
            onQuestionSelected(peer.playerId, round, theme, question, actionId);
            return;
      }
      if (frame.command == QStringLiteral("REACTION_CLAIM"))
      {
            quint64 elapsed = 0;
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence) ||
                !parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &phaseSequence) ||
                !parseUIntField(fields, QStringLiteral("elapsedMs"), &elapsed) ||
                elapsed > std::numeric_limits<unsigned int>::max())
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid reaction claim"),
                            peer.requestId);
                  return;
            }
            onReactionClaim(peer.playerId, questionSequence, phaseSequence,
                            actionId, static_cast<unsigned int>(elapsed));
            return;
      }
      if (frame.command == QStringLiteral("ANSWER_DRAFT"))
      {
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence) ||
                !parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &phaseSequence) ||
                !fields.contains(QStringLiteral("answer")))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid answer draft"),
                            peer.requestId);
                  return;
            }
            onAnswerDraftChanged(peer.playerId, questionSequence,
                                 phaseSequence, actionId,
                                 fields.value(QStringLiteral("answer")));
            return;
      }
      if (frame.command == QStringLiteral("ANSWER_SUBMIT"))
      {
            AnswerSubmission submission;
            QString error;
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence) ||
                !parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &phaseSequence) ||
                !parseAnswer(fields, &submission, &error))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            error.isEmpty() ? QStringLiteral("Invalid answer")
                                            : error,
                            peer.requestId);
                  return;
            }
            onAnswerSubmitted(peer.playerId, questionSequence, phaseSequence,
                              actionId, submission);
            return;
      }
      if (frame.command == QStringLiteral("PASS"))
      {
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence) ||
                !parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &phaseSequence))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid pass sequence"),
                            peer.requestId);
                  return;
            }
            onPass(peer.playerId, questionSequence, phaseSequence, actionId);
            return;
      }
      if (frame.command == QStringLiteral("SECRET_TARGET"))
      {
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid secret sequence"),
                            peer.requestId);
                  return;
            }
            onSecretTargetSelected(peer.playerId,
                                   fields.value(QStringLiteral("targetPlayerId")),
                                   questionSequence, actionId);
            return;
      }
      if (frame.command == QStringLiteral("SECRET_WAGER"))
      {
            qint64 amount = 0;
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence) ||
                !MultiplayerProtocol::parseSigned(
                      fields, QStringLiteral("amount"), &amount) ||
                amount < std::numeric_limits<int>::min() ||
                amount > std::numeric_limits<int>::max())
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid secret wager"),
                            peer.requestId);
                  return;
            }
            onSecretWagerSubmitted(peer.playerId, static_cast<int>(amount),
                                   questionSequence, actionId);
            return;
      }
      if (frame.command == QStringLiteral("PAUSE_REQUEST"))
      {
            bool paused = false;
            if (!parseUIntField(fields, QStringLiteral("phaseSeq"),
                                &phaseSequence) ||
                !MultiplayerProtocol::parseBool(fields, QStringLiteral("paused"),
                                                &paused))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid pause request"),
                            peer.requestId);
                  return;
            }
            onPauseRequested(peer.playerId, paused, phaseSequence, actionId);
            return;
      }
      if (frame.command == QStringLiteral("APPEAL_REQUEST"))
      {
            if (!parseUIntField(fields, QStringLiteral("questionSeq"),
                                &questionSequence))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid appeal sequence"),
                            peer.requestId);
                  return;
            }
            onAppealRequested(peer.playerId, questionSequence, actionId);
            return;
      }
      if (frame.command == QStringLiteral("APPEAL_VOTE"))
      {
            quint64 appealId = 0;
            bool accepted = false;
            if (!parseUIntField(fields, QStringLiteral("appealId"), &appealId) ||
                !MultiplayerProtocol::parseBool(fields, QStringLiteral("accepted"),
                                                &accepted))
            {
                  sendError(peer, QStringLiteral("BAD_FIELD"),
                            QStringLiteral("Invalid appeal vote"),
                            peer.requestId);
                  return;
            }
            onAppealVote(peer.playerId, appealId, accepted, actionId);
            return;
      }
      sendError(peer, QStringLiteral("BAD_FRAME"),
                QStringLiteral("Unsupported action"), peer.requestId);
}

bool MultiplayerHost::parseAnswer(const QMap<QString, QString> &fields,
                                  AnswerSubmission *submission,
                                  QString *errorMessage) const
{
      if (submission == nullptr)
      {
            return false;
      }
      const QString type = fields.value(QStringLiteral("answerType"));
      if (type.compare(QStringLiteral("Text"), Qt::CaseInsensitive) == 0)
      {
            submission->answerType = AnswerType::Text;
            submission->answer = fields.value(QStringLiteral("answer"));
      }
      else if (type.compare(QStringLiteral("Select"), Qt::CaseInsensitive) == 0)
      {
            submission->answerType = AnswerType::Select;
            submission->optionId = fields.value(QStringLiteral("optionId"));
      }
      else if (type.compare(QStringLiteral("Point"), Qt::CaseInsensitive) == 0)
      {
            bool xOk = false;
            bool yOk = false;
            const double x = fields.value(QStringLiteral("x")).toDouble(&xOk);
            const double y = fields.value(QStringLiteral("y")).toDouble(&yOk);
            if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y))
            {
                  if (errorMessage != nullptr)
                  {
                        *errorMessage = QStringLiteral("Invalid point coordinates");
                  }
                  return false;
            }
            submission->answerType = AnswerType::Point;
            submission->point = QPointF(x, y);
            submission->hasPoint = true;
      }
      else
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Unknown answer type");
            }
            return false;
      }
      submission->mode = fields.value(QStringLiteral("mode"));
      return true;
}

void MultiplayerHost::sendFrame(Peer &peer, const QString &command,
                                const QMap<QString, QString> &fields)
{
      if (peer.connection != nullptr)
      {
            peer.connection->sendLine(MultiplayerProtocol::encodeFrame(command,
                                                                        fields));
      }
}

void MultiplayerHost::sendError(Peer &peer, const QString &code,
                                const QString &message,
                                const QString &requestId)
{
      sendFrame(peer, QStringLiteral("ERROR"),
                {{QStringLiteral("code"), code},
                 {QStringLiteral("message"), message},
                 {QStringLiteral("requestId"), requestId},
                 {QStringLiteral("phaseSeq"),
                  stringValue(m_session->phaseSequence())},
                 {QStringLiteral("questionSeq"),
                  stringValue(m_session->questionSequence())}});
      emit protocolError(peer.playerId, code, message);
}

void MultiplayerHost::sendError(PlayerId playerId, const QString &code,
                                const QString &message)
{
      Peer *peer = peerForPlayer(playerId);
      if (peer != nullptr)
      {
            sendError(*peer, code, message, peer->requestId);
      }
      else
      {
            emit protocolError(playerId, code, message);
      }
}

void MultiplayerHost::broadcast(const QString &command,
                                const QMap<QString, QString> &fields)
{
      for (auto iterator = m_peers.begin(); iterator != m_peers.end(); ++iterator)
      {
            if (iterator.value().handshaken)
            {
                  sendFrame(iterator.value(), command, fields);
            }
      }
      emit stateBroadcast({command, fields});
}

void MultiplayerHost::sendRoster()
{
      ++m_rosterSequence;
      const QVector<PlayerState> roster = m_session->players();
      emit rosterChanged(roster);
      for (auto iterator = m_peers.begin(); iterator != m_peers.end(); ++iterator)
      {
            if (iterator.value().handshaken)
            {
                  sendRoster(iterator.value());
            }
      }
}

void MultiplayerHost::sendRoster(Peer &peer)
{
      const QVector<PlayerState> roster = m_session->players();
      const int connected = static_cast<int>(std::count_if(
            roster.cbegin(), roster.cend(),
            [](const PlayerState &state) { return state.connected; }));
      const int reserved = static_cast<int>(std::count_if(
            roster.cbegin(), roster.cend(),
            [](const PlayerState &state) { return !state.connected; }));
      sendFrame(peer, QStringLiteral("LOBBY_STATE"),
                {{QStringLiteral("sessionId"), m_session->sessionId()},
                 {QStringLiteral("rosterSeq"),
                  stringValue(m_rosterSequence)},
                 {QStringLiteral("connected"), stringValue(connected)},
                 {QStringLiteral("reserved"), stringValue(reserved)},
                 {QStringLiteral("maxPlayers"),
                  stringValue(m_config.maxPlayers)},
                 {QStringLiteral("started"), boolValue(m_started)}});
      sendFrame(peer, QStringLiteral("ROSTER_BEGIN"),
                {{QStringLiteral("rosterSeq"), stringValue(m_rosterSequence)},
                 {QStringLiteral("count"), stringValue(roster.size())}});
      for (const PlayerState &state : roster)
      {
            sendFrame(peer, QStringLiteral("ROSTER_PLAYER"),
                      {{QStringLiteral("rosterSeq"),
                        stringValue(m_rosterSequence)},
                       {QStringLiteral("playerId"), state.id},
                       {QStringLiteral("nickname"), state.nickname},
                       {QStringLiteral("connected"), boolValue(state.connected)},
                       {QStringLiteral("ready"), boolValue(state.ready)},
                       {QStringLiteral("balance"), stringValue(state.balance)}});
            if (!state.profilePng.isEmpty())
            {
                  sendProfile(peer, state);
            }
      }
      sendFrame(peer, QStringLiteral("ROSTER_END"),
                {{QStringLiteral("rosterSeq"),
                  stringValue(m_rosterSequence)}});
}

void MultiplayerHost::sendProfile(Peer &peer, const PlayerState &state)
{
      if (state.profilePng.isEmpty() ||
          state.profilePng.size() > MultiplayerProtocol::MaxProfileBytes)
      {
            return;
      }
      const QString transferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
      const QByteArray hash = QCryptographicHash::hash(
            state.profilePng, QCryptographicHash::Sha256);
      sendFrame(peer, QStringLiteral("PROFILE_BEGIN"),
                {{QStringLiteral("transferId"), transferId},
                 {QStringLiteral("playerId"), state.id},
                 {QStringLiteral("format"), QStringLiteral("png")},
                 {QStringLiteral("bytes"), stringValue(state.profilePng.size())},
                 {QStringLiteral("sha256"), QString::fromLatin1(hash.toHex())}});
      constexpr int chunkSize = 24 * 1024;
      int index = 0;
      for (int offset = 0; offset < state.profilePng.size(); offset += chunkSize)
      {
            const QByteArray chunk = state.profilePng.mid(offset, chunkSize);
            sendFrame(peer, QStringLiteral("PROFILE_CHUNK"),
                      {{QStringLiteral("transferId"), transferId},
                       {QStringLiteral("index"), stringValue(index++)},
                       {QStringLiteral("data"),
                        QString::fromLatin1(chunk.toBase64(
                              QByteArray::Base64UrlEncoding |
                              QByteArray::OmitTrailingEquals))}});
      }
      sendFrame(peer, QStringLiteral("PROFILE_END"),
                {{QStringLiteral("transferId"), transferId},
                 {QStringLiteral("sha256"), QString::fromLatin1(hash.toHex())}});
}

void MultiplayerHost::sendGameStarted()
{
      broadcast(QStringLiteral("GAME_STARTED"),
                {{QStringLiteral("sessionId"), m_session->sessionId()},
                 {QStringLiteral("round"), QStringLiteral("0")},
                 {QStringLiteral("maxPlayers"), stringValue(m_config.maxPlayers)},
                 {QStringLiteral("answerDurationMs"),
                  stringValue(m_config.answerDurationMs)},
                 {QStringLiteral("questionDurationMs"),
                  stringValue(m_config.questionDurationMs)},
                 {QStringLiteral("questionPickDurationMs"),
                  stringValue(m_config.questionPickDurationMs)},
                 {QStringLiteral("answerWaitDurationMs"),
                  stringValue(m_config.answerWaitDurationMs)},
                 {QStringLiteral("answerRevealDurationMs"),
                  stringValue(m_config.answerRevealDurationMs)},
                 {QStringLiteral("appealDurationMs"),
                  stringValue(m_config.appealDurationMs)}});
}

void MultiplayerHost::sendSnapshot(Peer &peer)
{
      const SessionSnapshot snapshot = m_session->snapshot();
      const QString snapshotSeq = stringValue(snapshot.snapshotSequence);
      sendFrame(peer, QStringLiteral("SNAPSHOT_BEGIN"),
                {{QStringLiteral("snapshotSeq"), snapshotSeq},
                 {QStringLiteral("sessionId"), snapshot.sessionId},
                 {QStringLiteral("phaseSeq"),
                  stringValue(snapshot.phase.phaseSequence)},
                 {QStringLiteral("questionSeq"),
                  stringValue(snapshot.phase.questionSequence)},
                 {QStringLiteral("boardSeq"),
                  stringValue(snapshot.board.boardSequence)},
                 {QStringLiteral("phase"),
                  MultiplayerProtocol::phaseName(snapshot.phase.phase)},
                 {QStringLiteral("paused"), boolValue(snapshot.paused)},
                 {QStringLiteral("remainingMs"),
                  stringValue(snapshot.phase.remainingMs)},
                 {QStringLiteral("currentPicker"), snapshot.currentPicker},
                 {QStringLiteral("answerOwner"), snapshot.answerOwner}});
      sendFrame(peer, QStringLiteral("SNAPSHOT_CONFIG"),
                {{QStringLiteral("snapshotSeq"), snapshotSeq},
                 {QStringLiteral("maxPlayers"), stringValue(m_config.maxPlayers)},
                 {QStringLiteral("answerDurationMs"),
                  stringValue(m_config.answerDurationMs)},
                 {QStringLiteral("questionDurationMs"),
                  stringValue(m_config.questionDurationMs)},
                 {QStringLiteral("questionPickDurationMs"),
                  stringValue(m_config.questionPickDurationMs)},
                 {QStringLiteral("answerWaitDurationMs"),
                  stringValue(m_config.answerWaitDurationMs)}});
      for (const PlayerState &state : snapshot.players)
      {
            sendFrame(peer, QStringLiteral("SNAPSHOT_PLAYER"),
                      {{QStringLiteral("snapshotSeq"), snapshotSeq},
                       {QStringLiteral("playerId"), state.id},
                       {QStringLiteral("nickname"), state.nickname},
                       {QStringLiteral("connected"), boolValue(state.connected)},
                       {QStringLiteral("balance"), stringValue(state.balance)},
                       {QStringLiteral("hasPassed"), boolValue(state.hasPassed)},
                       {QStringLiteral("answeredIncorrectly"),
                        boolValue(state.answeredIncorrectly)},
                       {QStringLiteral("mayAppeal"), boolValue(state.mayAppeal)}});
      }
      for (const BoardCell &cell : snapshot.board.cells)
      {
            sendFrame(peer, QStringLiteral("SNAPSHOT_CELL"),
                      {{QStringLiteral("snapshotSeq"), snapshotSeq},
                       {QStringLiteral("round"), stringValue(cell.round)},
                       {QStringLiteral("theme"), stringValue(cell.theme)},
                       {QStringLiteral("question"), stringValue(cell.question)},
                       {QStringLiteral("used"), boolValue(cell.used)}});
      }
      if (snapshot.question.has_value())
      {
            QMap<QString, QString> fields = questionFields(*snapshot.question);
            fields.insert(QStringLiteral("snapshotSeq"), snapshotSeq);
            sendFrame(peer, QStringLiteral("QUESTION_START"), fields);
      }
      if (snapshot.reveal.has_value())
      {
            const AnswerReveal &reveal = *snapshot.reveal;
            QMap<QString, QString> fields{
                  {QStringLiteral("questionSeq"),
                   stringValue(reveal.questionSequence)},
                  {QStringLiteral("rightCount"),
                   stringValue(reveal.rightAnswers.size())},
                  {QStringLiteral("answerMediaType"),
                   MultiplayerProtocol::mediaTypeName(
                         static_cast<int>(reveal.answerMediaType))},
                  {QStringLiteral("answerMediaPath"), reveal.answerMediaPath},
                  {QStringLiteral("answerOwner"), reveal.answerOwner},
                  {QStringLiteral("nextPicker"), reveal.nextPicker}};
            for (int index = 0; index < reveal.rightAnswers.size(); ++index)
            {
                  fields.insert(QStringLiteral("right%1").arg(index),
                                reveal.rightAnswers[index]);
            }
            fields.insert(QStringLiteral("snapshotSeq"), snapshotSeq);
            sendFrame(peer, QStringLiteral("ANSWER_REVEAL"), fields);
      }
      if (snapshot.appeal.has_value())
      {
            const AppealState &appeal = *snapshot.appeal;
            QMap<QString, QString> appealFields{
                  {QStringLiteral("appealId"), stringValue(appeal.appealId)},
                  {QStringLiteral("questionSeq"),
                   stringValue(appeal.questionSequence)},
                  {QStringLiteral("appellant"), appeal.appellant},
                  {QStringLiteral("submitted"), appeal.submitted},
                  {QStringLiteral("rightCount"),
                   stringValue(appeal.rightAnswers.size())},
                  {QStringLiteral("answerMediaType"),
                   MultiplayerProtocol::mediaTypeName(
                         static_cast<int>(appeal.answerMediaType))},
                  {QStringLiteral("answerMediaPath"), appeal.answerMediaPath},
                  {QStringLiteral("voterCount"),
                   stringValue(appeal.voters.size())},
                  {QStringLiteral("durationMs"),
                   stringValue(appeal.durationMs)}};
            for (int index = 0; index < appeal.rightAnswers.size(); ++index)
            {
                  appealFields.insert(QStringLiteral("right%1").arg(index),
                                      appeal.rightAnswers[index]);
            }
            sendFrame(peer, QStringLiteral("APPEAL_OPEN"), appealFields);
      }
      if (m_session->phase() == SessionPhase::SecretTargetSelection &&
          peer.playerId == m_session->currentPicker())
      {
            const QString selectionMode = m_session->secretSelectionMode();
            QVector<PlayerState> targets;
            for (const PlayerState &state : m_session->players())
            {
                  if (state.connected && state.ready &&
                      !(selectionMode.compare(QStringLiteral("exceptCurrent"),
                                              Qt::CaseInsensitive) == 0 &&
                        state.id == peer.playerId))
                  {
                        targets.push_back(state);
                  }
            }
            QMap<QString, QString> fields{
                  {QStringLiteral("questionSeq"),
                   stringValue(m_session->questionSequence())},
                  {QStringLiteral("selectionMode"), selectionMode},
                  {QStringLiteral("count"), stringValue(targets.size())}};
            for (int index = 0; index < targets.size(); ++index)
            {
                  fields.insert(QStringLiteral("target%1").arg(index),
                                targets[index].id);
                  fields.insert(QStringLiteral("target%1Name").arg(index),
                                targets[index].nickname);
            }
            sendFrame(peer, QStringLiteral("SECRET_TARGETS"), fields);
      }
      if (m_session->phase() == SessionPhase::SecretWager &&
          peer.playerId == m_session->secretTarget())
      {
            const SecretWagerParameters parameters =
                  m_session->secretWagerParameters();
            sendFrame(peer, QStringLiteral("SECRET_WAGER_PROMPT"),
                      {{QStringLiteral("questionSeq"),
                        stringValue(m_session->questionSequence())},
                       {QStringLiteral("minimum"),
                        stringValue(parameters.minimum)},
                       {QStringLiteral("maximum"),
                        stringValue(parameters.maximum)},
                       {QStringLiteral("step"),
                        stringValue(parameters.step)},
                       {QStringLiteral("secretTheme"), parameters.theme}});
      }
      sendFrame(peer, QStringLiteral("SNAPSHOT_END"),
                {{QStringLiteral("snapshotSeq"), snapshotSeq}});
}

MultiplayerHost::Peer *MultiplayerHost::peerForPlayer(const PlayerId &playerId)
{
      for (auto iterator = m_peers.begin(); iterator != m_peers.end(); ++iterator)
      {
            if (iterator.value().playerId == playerId)
            {
                  return &iterator.value();
            }
      }
      return nullptr;
}

const MultiplayerHost::Peer *MultiplayerHost::peerForPlayer(
      const PlayerId &playerId) const
{
      for (auto iterator = m_peers.cbegin(); iterator != m_peers.cend(); ++iterator)
      {
            if (iterator.value().playerId == playerId)
            {
                  return &iterator.value();
            }
      }
      return nullptr;
}

MultiplayerHost::Peer *MultiplayerHost::peerForConnection(
      MultiplayerConnection *connection)
{
      auto iterator = m_peers.find(connection);
      return iterator == m_peers.end() ? nullptr : &iterator.value();
}

bool MultiplayerHost::hasActivePeerForPlayer(const PlayerId &playerId) const
{
      if (playerId == m_localPlayerId)
      {
            return true;
      }
      const Peer *peer = peerForPlayer(playerId);
      return peer != nullptr && peer->connection != nullptr &&
             peer->connection->isConnected();
}

void MultiplayerHost::connectSessionSignals()
{
      connect(m_session, &GameSession::phaseStarted, this,
              [this](const PhaseState &state)
              {
                    sendSessionEvent(
                          QStringLiteral("PHASE_START"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(state.phaseSequence)},
                           {QStringLiteral("phase"),
                            MultiplayerProtocol::phaseName(state.phase)},
                           {QStringLiteral("durationMs"),
                            stringValue(state.durationMs)},
                           {QStringLiteral("remainingMs"),
                            stringValue(state.remainingMs)},
                           {QStringLiteral("questionSeq"),
                            stringValue(state.questionSequence)},
                           {QStringLiteral("owner"), state.owner}});
              });
      connect(m_session, &GameSession::pauseChanged, this,
              [this](bool paused, const PhaseState &state)
              {
                    sendSessionEvent(
                          QStringLiteral("PAUSE_STATE"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(state.phaseSequence)},
                           {QStringLiteral("phase"),
                            MultiplayerProtocol::phaseName(state.phase)},
                           {QStringLiteral("paused"), boolValue(paused)},
                           {QStringLiteral("remainingMs"),
                            stringValue(state.remainingMs)}});
                    if (!paused)
                    {
                          sendSessionEvent(
                                QStringLiteral("PHASE_RESUMED"),
                                {{QStringLiteral("phaseSeq"),
                                  stringValue(state.phaseSequence)},
                                 {QStringLiteral("phase"),
                                  MultiplayerProtocol::phaseName(state.phase)},
                                 {QStringLiteral("durationMs"),
                                  stringValue(state.durationMs)},
                                 {QStringLiteral("remainingMs"),
                                  stringValue(state.remainingMs)},
                                 {QStringLiteral("questionSeq"),
                                  stringValue(state.questionSequence)},
                                 {QStringLiteral("owner"), state.owner}});
                    }
              });
      connect(m_session, &GameSession::boardChanged, this,
              [this](const BoardState &state)
              {
                    sendSessionEvent(
                          QStringLiteral("BOARD_STATE"),
                          {{QStringLiteral("boardSeq"),
                            stringValue(state.boardSequence)},
                           {QStringLiteral("round"), stringValue(state.round)},
                           {QStringLiteral("used"), usedCellsValue(state)}});
              });
      connect(m_session, &GameSession::questionStarted, this,
              [this](const QuestionPresentation &question)
              { sendSessionEvent(QStringLiteral("QUESTION_START"),
                                 questionFields(question)); });
      connect(m_session, &GameSession::reactionOpened, this,
              [this](const ReactionState &state)
              {
                    sendSessionEvent(
                          QStringLiteral("REACTION_OPEN"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(state.phaseSequence)},
                           {QStringLiteral("questionSeq"),
                            stringValue(state.questionSequence)},
                           {QStringLiteral("durationMs"),
                            stringValue(state.durationMs)},
                           {QStringLiteral("remainingMs"),
                            stringValue(state.remainingMs)}});
              });
      connect(m_session, &GameSession::reactionWinner, this,
              [this](const PlayerId &playerId, unsigned int elapsedMs)
              {
                    sendSessionEvent(
                          QStringLiteral("REACTION_WINNER"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(m_session->phaseSequence())},
                           {QStringLiteral("questionSeq"),
                            stringValue(m_session->questionSequence())},
                           {QStringLiteral("playerId"), playerId},
                           {QStringLiteral("measuredElapsedMs"),
                            stringValue(elapsedMs)}});
              });
      connect(m_session, &GameSession::answerOwnerChanged, this,
              [this](const PlayerId &playerId, unsigned int durationMs)
              {
                    sendSessionEvent(
                          QStringLiteral("ANSWER_OWNER"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(m_session->phaseSequence())},
                           {QStringLiteral("questionSeq"),
                            stringValue(m_session->questionSequence())},
                           {QStringLiteral("playerId"), playerId},
                           {QStringLiteral("durationMs"),
                            stringValue(durationMs)}});
              });
      connect(m_session, &GameSession::answerResult, this,
              [this](const AnswerResult &result)
              {
                    sendSessionEvent(
                          QStringLiteral("ANSWER_RESULT"),
                          {{QStringLiteral("questionSeq"),
                            stringValue(result.questionSequence)},
                           {QStringLiteral("playerId"), result.playerId},
                           {QStringLiteral("correct"), boolValue(result.correct)},
                           {QStringLiteral("amount"), stringValue(result.amount)},
                           {QStringLiteral("balance"), stringValue(result.balance)},
                           {QStringLiteral("answerKind"),
                            MultiplayerProtocol::answerTypeName(
                                  static_cast<int>(result.answerKind))},
                           {QStringLiteral("submitted"), result.submitted},
                           {QStringLiteral("remainingReactionMs"),
                            stringValue(result.remainingReactionMs)},
                           {QStringLiteral("retryAllowed"),
                            boolValue(result.retryAllowed)}});
              });
      connect(m_session, &GameSession::reactionResumed, this,
              [this](unsigned int remainingMs, const PlayerId &excluded)
              {
                    sendSessionEvent(
                          QStringLiteral("REACTION_RESUMED"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(m_session->phaseSequence())},
                           {QStringLiteral("questionSeq"),
                            stringValue(m_session->questionSequence())},
                           {QStringLiteral("remainingMs"), stringValue(remainingMs)},
                           {QStringLiteral("excludedPlayerId"), excluded}});
              });
      connect(m_session, &GameSession::answerRevealed, this,
              [this](const AnswerReveal &reveal)
              {
                    QMap<QString, QString> fields{
                          {QStringLiteral("questionSeq"),
                           stringValue(reveal.questionSequence)},
                          {QStringLiteral("rightCount"),
                           stringValue(reveal.rightAnswers.size())},
                          {QStringLiteral("answerMediaType"),
                           MultiplayerProtocol::mediaTypeName(
                                 static_cast<int>(reveal.answerMediaType))},
                          {QStringLiteral("answerMediaPath"),
                           reveal.answerMediaPath},
                          {QStringLiteral("answerOwner"), reveal.answerOwner},
                          {QStringLiteral("nextPicker"), reveal.nextPicker}};
                    for (int index = 0; index < reveal.rightAnswers.size(); ++index)
                    {
                          fields.insert(QStringLiteral("right%1").arg(index),
                                        reveal.rightAnswers[index]);
                    }
                    sendSessionEvent(QStringLiteral("ANSWER_REVEAL"), fields);
              });
      connect(m_session, &GameSession::forAllProgress, this,
              [this](quint64 questionSequence, int received, int expected)
              {
                    sendSessionEvent(
                          QStringLiteral("FORALL_PROGRESS"),
                          {{QStringLiteral("questionSeq"),
                            stringValue(questionSequence)},
                           {QStringLiteral("received"), stringValue(received)},
                           {QStringLiteral("expected"), stringValue(expected)}});
              });
      connect(m_session, &GameSession::forAllResult, this,
              [this](const ForAllResult &result)
              {
                    QMap<QString, QString> fields{
                          {QStringLiteral("questionSeq"),
                           stringValue(result.questionSequence)},
                          {QStringLiteral("resultCount"),
                           stringValue(result.results.size())}};
                    for (int index = 0; index < result.results.size(); ++index)
                    {
                          const AnswerResult &answer = result.results[index];
                          fields.insert(QStringLiteral("player%1").arg(index),
                                        answer.playerId);
                          fields.insert(QStringLiteral("correct%1").arg(index),
                                        boolValue(answer.correct));
                          fields.insert(QStringLiteral("amount%1").arg(index),
                                        stringValue(answer.amount));
                          fields.insert(QStringLiteral("balance%1").arg(index),
                                        stringValue(answer.balance));
                    }
                    sendSessionEvent(QStringLiteral("FORALL_RESULT"), fields);
              });
      connect(m_session, &GameSession::appealOpened, this,
              [this](const AppealState &appeal)
              {
                    QMap<QString, QString> fields{
                          {QStringLiteral("appealId"),
                           stringValue(appeal.appealId)},
                          {QStringLiteral("questionSeq"),
                           stringValue(appeal.questionSequence)},
                          {QStringLiteral("appellant"), appeal.appellant},
                          {QStringLiteral("submitted"), appeal.submitted},
                          {QStringLiteral("rightCount"),
                           stringValue(appeal.rightAnswers.size())},
                          {QStringLiteral("answerMediaType"),
                           MultiplayerProtocol::mediaTypeName(
                                 static_cast<int>(appeal.answerMediaType))},
                          {QStringLiteral("answerMediaPath"),
                           appeal.answerMediaPath},
                          {QStringLiteral("voterCount"),
                           stringValue(appeal.voters.size())},
                          {QStringLiteral("durationMs"),
                           stringValue(appeal.durationMs)}};
                    for (int index = 0;
                         index < appeal.rightAnswers.size(); ++index)
                    {
                          fields.insert(QStringLiteral("right%1").arg(index),
                                        appeal.rightAnswers[index]);
                    }
                    sendSessionEvent(QStringLiteral("APPEAL_OPEN"), fields);
              });
      connect(m_session, &GameSession::appealFinished, this,
              [this](const AppealResult &result)
              {
                    sendSessionEvent(
                          QStringLiteral("APPEAL_RESULT"),
                          {{QStringLiteral("appealId"), stringValue(result.appealId)},
                           {QStringLiteral("questionSeq"),
                            stringValue(result.questionSequence)},
                           {QStringLiteral("accepted"),
                            boolValue(result.accepted)},
                           {QStringLiteral("appellant"), result.appellant},
                           {QStringLiteral("correction"),
                            stringValue(result.correction)},
                           {QStringLiteral("balance"),
                            stringValue(result.balance)},
                           {QStringLiteral("nextPicker"), result.nextPicker}});
              });
      connect(m_session, &GameSession::pickerChanged, this,
              [this](const PlayerId &playerId)
              {
                    sendSessionEvent(
                          QStringLiteral("PICKER_CHANGED"),
                          {{QStringLiteral("phaseSeq"),
                            stringValue(m_session->phaseSequence())},
                           {QStringLiteral("playerId"), playerId}});
                    sendSessionEvent(
                          QStringLiteral("ROUND_STARTED"),
                          {{QStringLiteral("sessionId"), m_session->sessionId()},
                           {QStringLiteral("round"), QStringLiteral("0")},
                           {QStringLiteral("picker"), playerId},
                           {QStringLiteral("phaseSeq"),
                            stringValue(m_session->phaseSequence())}});
              });
      connect(m_session, &GameSession::playersChanged, this,
              [this](const QVector<PlayerState> &players)
              {
                    emit rosterChanged(players);
                    sendRoster();
              });
      connect(m_session, &GameSession::secretTargetsReady, this,
              [this](quint64 questionSequence,
                     const QVector<PlayerState> &targets)
              {
                    Peer *picker = peerForPlayer(m_session->currentPicker());
                    if (picker == nullptr)
                    {
                          return;
                    }
                    const QString selectionMode =
                          m_session->secretSelectionMode();
                    QMap<QString, QString> fields{
                          {QStringLiteral("questionSeq"),
                           stringValue(questionSequence)},
                          {QStringLiteral("selectionMode"), selectionMode},
                          {QStringLiteral("count"), stringValue(targets.size())}};
                    for (int index = 0; index < targets.size(); ++index)
                    {
                          fields.insert(QStringLiteral("target%1").arg(index),
                                        targets[index].id);
                          fields.insert(QStringLiteral("target%1Name").arg(index),
                                        targets[index].nickname);
                    }
                    sendFrame(*picker, QStringLiteral("SECRET_TARGETS"), fields);
              });
      connect(m_session, &GameSession::secretWagerPrompt, this,
              [this](const PlayerId &target,
                     const SecretWagerParameters &parameters)
              {
                    Peer *peer = peerForPlayer(target);
                    if (peer == nullptr)
                    {
                          return;
                    }
                    sendFrame(*peer, QStringLiteral("SECRET_WAGER_PROMPT"),
                              {{QStringLiteral("questionSeq"),
                                stringValue(m_session->questionSequence())},
                               {QStringLiteral("minimum"),
                                stringValue(parameters.minimum)},
                               {QStringLiteral("maximum"),
                                stringValue(parameters.maximum)},
                               {QStringLiteral("step"),
                                stringValue(parameters.step)},
                               {QStringLiteral("secretTheme"), parameters.theme}});
              });
      connect(m_session, &GameSession::secretReady, this,
              [this](quint64 questionSequence, const PlayerId &target)
              {
                    sendSessionEvent(
                          QStringLiteral("SECRET_READY"),
                          {{QStringLiteral("questionSeq"),
                            stringValue(questionSequence)},
                           {QStringLiteral("targetPlayerId"), target}});
              });
      connect(m_session, &GameSession::actionRejected, this,
              [this](const PlayerId &playerId, const QString &code,
                     const QString &message)
              { sendError(playerId, code, message); });
      connect(m_session, &GameSession::gameFinished, this,
              [this]()
              {
                    sendSessionEvent(
                          QStringLiteral("GAME_FINISHED"),
                          {{QStringLiteral("sessionId"), m_session->sessionId()},
                           {QStringLiteral("reason"),
                            QStringLiteral("board-exhausted")}});
              });
}

void MultiplayerHost::startPings()
{
      if (m_pingThread != nullptr)
      {
            return;
      }
      m_pingThread = new QThread(this);
      m_pingWorker = new PingWorker;
      m_pingWorker->moveToThread(m_pingThread);
      connect(m_pingThread, &QThread::started, m_pingWorker,
              &PingWorker::start);
      connect(m_pingWorker, &PingWorker::pingRequested, this,
              [this](const PlayerId &playerId, quint64 pingId)
              {
                    Peer *peer = peerForPlayer(playerId);
                    if (peer != nullptr)
                    {
                          sendFrame(*peer, QStringLiteral("PING"),
                                    {{QStringLiteral("pingId"),
                                      stringValue(pingId)}});
                    }
              });
      connect(m_pingWorker, &PingWorker::averageUpdated, this,
              [this](const PlayerId &playerId, double rttMs)
              {
                    m_remoteRtt.insert(playerId, rttMs);
                    double maximumRtt = 0.0;
                    for (auto iterator = m_remoteRtt.cbegin();
                         iterator != m_remoteRtt.cend(); ++iterator)
                    {
                          if (isConnected(iterator.key()))
                          {
                                maximumRtt =
                                      std::max(maximumRtt, iterator.value());
                          }
                    }
                    m_session->setReactionDecisionWindowMs(
                          static_cast<unsigned int>(std::ceil(maximumRtt)));
              });
      connect(m_session, &GameSession::playersChanged, m_pingWorker,
              [worker = m_pingWorker, localId = m_localPlayerId](
                    const QVector<PlayerState> &players)
              {
                    QVector<PlayerId> remote;
                    for (const PlayerState &state : players)
                    {
                          if (state.id != localId && state.connected)
                          {
                                remote.push_back(state.id);
                          }
                    }
                    worker->setPlayers(remote);
              });
      m_pingThread->start();
}

void MultiplayerHost::stopPings()
{
      if (m_pingThread == nullptr)
      {
            return;
      }
      QMetaObject::invokeMethod(m_pingWorker, "stop", Qt::BlockingQueuedConnection);
      m_pingThread->quit();
      m_pingThread->wait();
      delete m_pingWorker;
      m_pingWorker = nullptr;
      m_pingThread->deleteLater();
      m_pingThread = nullptr;
      m_remoteRtt.clear();
}

void MultiplayerHost::sendPhaseSync()
{
      if (!m_started || m_session == nullptr ||
          m_session->phase() == SessionPhase::Lobby ||
          m_session->phase() == SessionPhase::Finished ||
          m_session->isPaused())
      {
            return;
      }
      const SessionSnapshot snapshot = m_session->snapshot();
      sendSessionEvent(
            QStringLiteral("PHASE_SYNC"),
            {{QStringLiteral("phaseSeq"),
              stringValue(snapshot.phase.phaseSequence)},
             {QStringLiteral("phase"),
              MultiplayerProtocol::phaseName(snapshot.phase.phase)},
             {QStringLiteral("durationMs"),
              stringValue(snapshot.phase.durationMs)},
             {QStringLiteral("remainingMs"),
              stringValue(snapshot.phase.remainingMs)},
             {QStringLiteral("questionSeq"),
              stringValue(snapshot.phase.questionSequence)},
             {QStringLiteral("owner"), snapshot.phase.owner}});
}

void MultiplayerHost::sendSessionEvent(const QString &command,
                                       const QMap<QString, QString> &fields)
{
      broadcast(command, fields);
}

QString MultiplayerHost::boolValue(bool value)
{
      return value ? QStringLiteral("1") : QStringLiteral("0");
}

QStringList MultiplayerHost::localAddresses()
{
      QStringList addresses;
      for (const QHostAddress &address : QNetworkInterface::allAddresses())
      {
            if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                address != QHostAddress::LocalHost)
            {
                  addresses.push_back(address.toString());
            }
      }
      if (addresses.isEmpty())
      {
            addresses.push_back(QHostAddress(QHostAddress::LocalHost).toString());
      }
      return addresses;
}

bool MultiplayerHost::isConnected(const PlayerId &playerId) const
{
      const PlayerState *state = m_session->player(playerId);
      return state != nullptr && state->connected;
}
