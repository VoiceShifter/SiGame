#ifndef MULTIPLAYERHOST_H
#define MULTIPLAYERHOST_H

#include "gameconfig.h"
#include "gamecontent.h"
#include "gamesession.h"
#include "multiplayerconnection.h"
#include "multiplayerprotocol.h"
#include "playeridentity.h"

#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVector>

class PingWorker;
class QTcpServer;

struct SessionEvent
{
      QString command;
      QMap<QString, QString> fields;
};

class MultiplayerHost : public QObject
{
      Q_OBJECT

    public:
      explicit MultiplayerHost(const GameConfig &config, const Game &game,
                               const PlayerIdentity &identity,
                               QObject *parent = nullptr);
      explicit MultiplayerHost(const GameConfig &config, const Game &game,
                               QObject *parent = nullptr);
      ~MultiplayerHost() override;

      bool listen(quint16 port = 32323);
      void stop();
      void startGame();
      void requestSnapshot(PlayerId playerId);

      GameSession *session() const { return m_session; }
      const GameConfig &config() const { return m_config; }
      PlayerId localPlayerId() const { return m_localPlayerId; }
      bool isListening() const;
      bool isStarted() const { return m_started; }

      void onReactionClaim(PlayerId playerId, quint64 questionSequence,
                           quint64 phaseSequence, quint64 actionId,
                           unsigned int elapsedMs);
      void onAnswerSubmitted(PlayerId playerId, quint64 questionSequence,
                             quint64 phaseSequence, quint64 actionId,
                             const AnswerSubmission &submission);
      void onAnswerDraftChanged(PlayerId playerId, quint64 questionSequence,
                                quint64 phaseSequence, quint64 actionId,
                                const QString &answer);
      void onQuestionSelected(PlayerId playerId, int roundIndex,
                              int themeIndex, int questionIndex,
                              quint64 actionId);
      void onSecretTargetSelected(PlayerId pickerId, PlayerId targetId,
                                  quint64 questionSequence, quint64 actionId);
      void onSecretWagerSubmitted(PlayerId targetId, int amount,
                                  quint64 questionSequence, quint64 actionId);
      void onPass(PlayerId playerId, quint64 questionSequence,
                  quint64 phaseSequence, quint64 actionId);
      void onPauseRequested(PlayerId playerId, bool paused,
                            quint64 phaseSequence, quint64 actionId);
      void onAppealRequested(PlayerId playerId, quint64 questionSequence,
                             quint64 actionId);
      void onAppealVote(PlayerId playerId, quint64 appealId, bool accepted,
                        quint64 actionId);

    signals:
      void playerJoined(const PlayerState &state);
      void playerReconnected(const PlayerState &state);
      void playerDisconnected(PlayerId playerId);
      void rosterChanged(const QVector<PlayerState> &players);
      void packMismatch(PlayerId playerId, QString expected, QString received);
      void listeningFailed(QString error);
      void listeningStarted(quint16 port, const QStringList &addresses);
      void protocolError(PlayerId playerId, QString code, QString message);
      void stateBroadcast(const SessionEvent &event);

    private slots:
      void acceptPendingConnections();
      void handleLine(const QByteArray &line);
      void handleDisconnected();
      void handleTransportError(const QString &message);
      void sendPhaseSync();

    private:
      struct Peer
      {
            MultiplayerConnection *connection{};
            PlayerId playerId;
            PlayerToken token;
            QString requestId;
            QString profileTransferId;
            QByteArray profileData;
            int expectedProfileBytes{-1};
            int nextProfileChunk{};
            QByteArray expectedProfileHash;
            bool handshaken{};
            bool ready{};
            bool profilePending{};
      };

      void handleFrame(Peer &peer, const MultiplayerProtocol::Frame &frame);
      void handleHello(Peer &peer, const QMap<QString, QString> &fields);
      void handleProfileBegin(Peer &peer, const QMap<QString, QString> &fields);
      void handleProfileChunk(Peer &peer, const QMap<QString, QString> &fields);
      void handleProfileEnd(Peer &peer, const QMap<QString, QString> &fields);
      void handleReady(Peer &peer, const QMap<QString, QString> &fields);
      void handleAction(Peer &peer, const MultiplayerProtocol::Frame &frame);
      bool parseAnswer(const QMap<QString, QString> &fields,
                       AnswerSubmission *submission, QString *errorMessage) const;
      void sendFrame(Peer &peer, const QString &command,
                     const QMap<QString, QString> &fields = {});
      void sendError(Peer &peer, const QString &code, const QString &message,
                     const QString &requestId = {});
      void sendError(PlayerId playerId, const QString &code,
                     const QString &message);
      void broadcast(const QString &command,
                     const QMap<QString, QString> &fields = {});
      void sendRoster();
      void sendRoster(Peer &peer);
      void sendProfile(Peer &peer, const PlayerState &state);
      void sendGameStarted();
      void sendSnapshot(Peer &peer);
      Peer *peerForPlayer(const PlayerId &playerId);
      const Peer *peerForPlayer(const PlayerId &playerId) const;
      Peer *peerForConnection(MultiplayerConnection *connection);
      bool hasActivePeerForPlayer(const PlayerId &playerId) const;
      bool isConnected(const PlayerId &playerId) const;
      void connectSessionSignals();
      void startPings();
      void stopPings();
      void sendSessionEvent(const QString &command,
                            const QMap<QString, QString> &fields);
      static QString boolValue(bool value);
      static QStringList localAddresses();

      GameConfig m_config;
      Game m_game;
      PlayerIdentity m_identity;
      PlayerId m_localPlayerId;
      GameSession *m_session{};
      QTcpServer *m_server{};
      QHash<MultiplayerConnection *, Peer> m_peers;
      quint64 m_rosterSequence{};
      QTimer m_syncTimer;
      QString m_lastRequestId;
      bool m_started{};
      QThread *m_pingThread{};
      PingWorker *m_pingWorker{};
      QHash<PlayerId, double> m_remoteRtt;
};

#endif // MULTIPLAYERHOST_H
