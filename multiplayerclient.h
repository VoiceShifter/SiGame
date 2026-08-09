#ifndef MULTIPLAYERCLIENT_H
#define MULTIPLAYERCLIENT_H

#include "gameconfig.h"
#include "multiplayerconnection.h"
#include "multiplayerprotocol.h"
#include "playeridentity.h"
#include "playerstate.h"

#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QVector>

class MultiplayerClient : public QObject
{
      Q_OBJECT

    public:
      explicit MultiplayerClient(QObject *parent = nullptr);
      ~MultiplayerClient() override;

      void connectToHost(const QHostAddress &address, quint16 port,
                         const GameConfig &localConfig,
                         const PlayerIdentity &identity);
      void disconnectFromHost();
      void reconnect();
      bool isConnected() const;

      PlayerId localPlayerId() const { return m_localPlayerId; }
      QString sessionId() const { return m_sessionId; }
      const GameConfig &config() const { return m_config; }
      QVector<PlayerState> roster() const { return m_roster; }

      void selectQuestion(int roundIndex, int themeIndex, int questionIndex);
      void submitReaction(quint64 questionSequence, quint64 phaseSequence,
                          unsigned int elapsedMs);
      void submitAnswer(quint64 questionSequence, quint64 phaseSequence,
                        const AnswerSubmission &submission);
      void updateAnswerDraft(quint64 questionSequence, quint64 phaseSequence,
                             const QString &answer);
      void pass(quint64 questionSequence, quint64 phaseSequence);
      void selectSecretTarget(quint64 questionSequence, PlayerId targetId);
      void submitSecretWager(quint64 questionSequence, int amount);
      void requestPause(quint64 phaseSequence, bool paused);
      void requestAppeal(quint64 questionSequence);
      void voteAppeal(quint64 appealId, bool accepted);

    signals:
      void connected(PlayerId localPlayerId, bool reconnected);
      void disconnected(QString reason);
      void lobbyChanged(const QVector<PlayerState> &players);
      void configurationReceived(const GameConfig &config);
      void gameStarted();
      void phaseReceived(const PhaseState &state);
      void boardReceived(const BoardState &state);
      void questionReceived(const QuestionPresentation &presentation);
      void secretTargetListReceived(const QVector<PlayerState> &targets);
      void secretInformationReceived(const SecretWagerParameters &parameters);
      void secretWagerPromptReceived(const SecretWagerParameters &parameters);
      void reactionWinnerReceived(PlayerId playerId, unsigned int elapsedMs);
      void answerOwnerReceived(PlayerId playerId, unsigned int durationMs);
      void pickerReceived(PlayerId playerId);
      void answerResultReceived(const AnswerResult &result);
      void forAllResultReceived(const ForAllResult &result);
      void revealReceived(const AnswerReveal &reveal);
      void appealReceived(const AppealState &state);
      void appealResultReceived(const AppealResult &result);
      void pauseReceived(bool paused, SessionPhase phase,
                         unsigned int remainingMs);
      void snapshotApplied(const SessionSnapshot &snapshot);
      void finished();
      void protocolError(QString code, QString message);

    private slots:
      void handleConnected();
      void handleLine(const QByteArray &line);
      void handleDisconnected();
      void handleTransportError(const QString &message);

    private:
      struct ProfileTransfer
      {
            QString transferId;
            PlayerId playerId;
            int expectedBytes{-1};
            int nextChunk{};
            QByteArray data;
            QByteArray hash;
      };

      void sendHello();
      void sendProfile();
      void sendReady();
      void sendCommand(const QString &command,
                       QMap<QString, QString> fields = {});
      QString requestId();
      quint64 actionId();
      void handleFrame(const MultiplayerProtocol::Frame &frame);
      bool parsePhase(const QMap<QString, QString> &fields, PhaseState *state,
                      QString *errorMessage = nullptr) const;
      bool parseQuestion(const QMap<QString, QString> &fields,
                         QuestionPresentation *question,
                         QString *errorMessage = nullptr) const;
      bool parseAnswerType(const QString &value, AnswerType *type) const;
      bool parseMediaType(const QString &value, MediaType *type) const;
      bool parseQuestionType(const QString &value, QuestionType *type) const;
      bool parsePhaseName(const QString &value, SessionPhase *phase) const;
      bool parseUnsigned(const QMap<QString, QString> &fields,
                         const QString &name, quint64 *value) const;
      void parseRosterPlayer(const QMap<QString, QString> &fields);
      void handleProfileBegin(const QMap<QString, QString> &fields);
      void handleProfileChunk(const QMap<QString, QString> &fields);
      void handleProfileEnd(const QMap<QString, QString> &fields);
      void startSnapshot(const QMap<QString, QString> &fields);
      void finishSnapshot(const QMap<QString, QString> &fields);
      void emitLobby();
      void updateRosterPlayer(const PlayerState &state);

      MultiplayerConnection *m_connection{};
      QHostAddress m_address;
      quint16 m_port{32323};
      GameConfig m_config;
      PlayerIdentity m_identity;
      QString m_sessionId;
      PlayerId m_localPlayerId;
      bool m_reconnected{};
      bool m_waitingForWelcome{};
      QString m_profileTransferId;
      QString m_lastRequestId;
      quint64 m_nextActionId{1};
      quint64 m_lastSnapshotSequence{};
      quint64 m_lastPhaseSequence{};
      quint64 m_lastQuestionSequence{};
      quint64 m_lastBoardSequence{};
      QVector<PlayerState> m_roster;
      QHash<PlayerId, QByteArray> m_profileCache;
      quint64 m_rosterSequence{};
      bool m_rosterOpen{};
      ProfileTransfer m_profile;
      SessionSnapshot m_snapshot;
      bool m_snapshotOpen{};
};

#endif // MULTIPLAYERCLIENT_H
