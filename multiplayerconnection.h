#ifndef MULTIPLAYERCONNECTION_H
#define MULTIPLAYERCONNECTION_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>

class QTcpSocket;
#ifdef Q_OS_WASM
class QWebSocket;
#endif

class MultiplayerConnection : public QObject
{
      Q_OBJECT

    public:
      explicit MultiplayerConnection(QObject *parent = nullptr);
      ~MultiplayerConnection() override;

      void connectToHost(const QHostAddress &address, quint16 port);
      void adoptSocket(QTcpSocket *socket);
#ifdef Q_OS_WASM
      void adoptBridgeConnection(QWebSocket *socket, quint64 connectionId,
                                 const QHostAddress &peerAddress);
      void deliverBridgeLine(const QByteArray &line);
      void bridgeDisconnected();
      void bridgeTransportError(const QString &message);
#endif
      void close();
      bool isConnected() const;
      QHostAddress peerAddress() const;

    public slots:
      void sendLine(QByteArray line);

    signals:
      void lineReceived(const QByteArray &line);
      void connected();
      void disconnected();
      void transportError(const QString &message);

    private slots:
      void readAvailable();
      void handleDisconnected();
      void handleSocketError();

    private:
      void attachSocket(QTcpSocket *socket);
      void rejectOversizedLine();
#ifdef Q_OS_WASM
      void handleBridgeMessage(const QString &message);
      void sendBridgeCommand(const QString &command, const QByteArray &line = {});
#endif

      QTcpSocket *m_socket{};
      QByteArray m_readBuffer;
#ifdef Q_OS_WASM
      QWebSocket *m_bridgeSocket{};
      quint64 m_bridgeConnectionId{};
      QHostAddress m_bridgePeerAddress;
      bool m_bridgeConnected{};
      bool m_ownsBridgeSocket{};
#endif
};

#endif // MULTIPLAYERCONNECTION_H
