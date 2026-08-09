#ifndef NETWORKBRIDGESERVER_H
#define NETWORKBRIDGESERVER_H

#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class MultiplayerConnection;
class QTcpServer;
class QWebSocket;
class QWebSocketServer;

class NetworkBridgeServer : public QObject
{
      Q_OBJECT

    public:
      explicit NetworkBridgeServer(QObject *parent = nullptr);
      ~NetworkBridgeServer() override;

      bool listen(quint16 port);
      QString errorString() const;

    private:
      struct BrowserSession
      {
            QTcpServer *listener{};
            QHash<quint64, MultiplayerConnection *> connections;
            quint64 nextConnectionId{1};
      };

      void acceptBrowserConnections();
      void handleBrowserMessage(QWebSocket *socket, const QString &message);
      void handleBrowserDisconnected(QWebSocket *socket);
      void startListening(QWebSocket *socket, quint16 port);
      void connectToHost(QWebSocket *socket, const QHostAddress &address,
                         quint16 port);
      void registerConnection(QWebSocket *socket, quint64 connectionId,
                              MultiplayerConnection *connection,
                              bool acceptedConnection);
      void sendEvent(QWebSocket *socket, const QString &event,
                     const QJsonObject &fields = {}) const;
      void closeSession(QWebSocket *socket);
      static QStringList localAddresses();

      QWebSocketServer *m_server{};
      QHash<QWebSocket *, BrowserSession> m_sessions;
};

#endif // NETWORKBRIDGESERVER_H
