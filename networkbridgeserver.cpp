#include "networkbridgeserver.h"

#include "multiplayerconnection.h"
#include "multiplayerprotocol.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

namespace
{
quint64 connectionId(const QJsonObject &object)
{
      bool ok = false;
      const quint64 value = object.value(QStringLiteral("connectionId"))
                                  .toString()
                                  .toULongLong(&ok);
      return ok ? value : 0;
}

bool isLocalOrigin(const QString &origin)
{
      if (origin.isEmpty() || origin == QStringLiteral("null"))
      {
            return true;
      }
      const QUrl url(origin);
      const QString host = url.host().toLower();
      return host == QStringLiteral("localhost") ||
             host == QStringLiteral("127.0.0.1") ||
             host == QStringLiteral("::1");
}
} // namespace

NetworkBridgeServer::NetworkBridgeServer(QObject *parent)
      : QObject(parent),
        m_server(new QWebSocketServer(QStringLiteral("SiGame network bridge"),
                                      QWebSocketServer::NonSecureMode, this))
{
      connect(m_server, &QWebSocketServer::newConnection, this,
              &NetworkBridgeServer::acceptBrowserConnections);
}

NetworkBridgeServer::~NetworkBridgeServer()
{
      const QList<QWebSocket *> sockets = m_sessions.keys();
      for (QWebSocket *socket : sockets)
      {
            closeSession(socket);
      }
      m_server->close();
}

bool NetworkBridgeServer::listen(quint16 port)
{
      return m_server->listen(QHostAddress::LocalHost, port);
}

QString NetworkBridgeServer::errorString() const
{
      return m_server->errorString();
}

void NetworkBridgeServer::acceptBrowserConnections()
{
      while (m_server->hasPendingConnections())
      {
            QWebSocket *socket = m_server->nextPendingConnection();
            if (socket == nullptr)
            {
                  continue;
            }
            socket->setParent(this);
            if (!isLocalOrigin(socket->origin()))
            {
                  qWarning() << "Rejected SiGame bridge origin:"
                             << socket->origin();
                  socket->close(
                        QWebSocketProtocol::CloseCodePolicyViolated,
                        tr("Only localhost pages may use the SiGame network "
                           "bridge"));
                  socket->deleteLater();
                  continue;
            }
            m_sessions.insert(socket, BrowserSession{});
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString &message)
                    { handleBrowserMessage(socket, message); });
            connect(socket, &QWebSocket::disconnected, this,
                    [this, socket]()
                    {
                          handleBrowserDisconnected(socket);
                          socket->deleteLater();
                    });
            sendEvent(socket, QStringLiteral("ready"));
      }
}

void NetworkBridgeServer::handleBrowserMessage(QWebSocket *socket,
                                               const QString &message)
{
      QJsonParseError parseError;
      const QJsonDocument document =
            QJsonDocument::fromJson(message.toUtf8(), &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject())
      {
            sendEvent(socket, QStringLiteral("error"),
                      {{QStringLiteral("message"),
                        tr("Invalid bridge command")}});
            return;
      }

      const QJsonObject object = document.object();
      const QString command = object.value(QStringLiteral("command")).toString();
      if (command == QStringLiteral("listen"))
      {
            const int port = object.value(QStringLiteral("port")).toInt();
            if (port < 1 || port > 65535)
            {
                  sendEvent(socket, QStringLiteral("error"),
                            {{QStringLiteral("message"),
                              tr("Invalid TCP listening port")}});
                  return;
            }
            startListening(socket, static_cast<quint16>(port));
            return;
      }
      if (command == QStringLiteral("connect"))
      {
            const QHostAddress address(
                  object.value(QStringLiteral("address")).toString());
            const int port = object.value(QStringLiteral("port")).toInt();
            if (address.isNull() || port < 1 || port > 65535)
            {
                  sendEvent(socket, QStringLiteral("error"),
                            {{QStringLiteral("message"),
                              tr("Invalid TCP destination")}});
                  return;
            }
            connectToHost(socket, address, static_cast<quint16>(port));
            return;
      }
      if (command == QStringLiteral("send"))
      {
            auto session = m_sessions.find(socket);
            const quint64 id = connectionId(object);
            if (session == m_sessions.end() || id == 0 ||
                !session->connections.contains(id))
            {
                  sendEvent(socket, QStringLiteral("error"),
                            {{QStringLiteral("connectionId"),
                              QString::number(id)},
                             {QStringLiteral("message"),
                              tr("Unknown bridge connection")}});
                  return;
            }
            session->connections.value(id)->sendLine(
                  object.value(QStringLiteral("line")).toString().toUtf8());
            return;
      }
      if (command == QStringLiteral("close"))
      {
            auto session = m_sessions.find(socket);
            const quint64 id = connectionId(object);
            if (session != m_sessions.end() &&
                session->connections.contains(id))
            {
                  session->connections.value(id)->close();
            }
            return;
      }
      if (command == QStringLiteral("stop"))
      {
            auto session = m_sessions.find(socket);
            if (session == m_sessions.end())
            {
                  return;
            }
            if (session->listener != nullptr)
            {
                  session->listener->close();
                  session->listener->deleteLater();
                  session->listener = nullptr;
            }
            const auto connections = session->connections;
            for (MultiplayerConnection *connection : connections)
            {
                  connection->close();
            }
            sendEvent(socket, QStringLiteral("stopped"));
            return;
      }

      sendEvent(socket, QStringLiteral("error"),
                {{QStringLiteral("message"),
                  tr("Unknown bridge command")}});
}

void NetworkBridgeServer::handleBrowserDisconnected(QWebSocket *socket)
{
      closeSession(socket);
}

void NetworkBridgeServer::startListening(QWebSocket *socket, quint16 port)
{
      auto session = m_sessions.find(socket);
      if (session == m_sessions.end())
      {
            return;
      }
      if (session->listener != nullptr)
      {
            session->listener->close();
            session->listener->deleteLater();
      }
      session->listener = new QTcpServer(this);
      QTcpServer *listener = session->listener;
      connect(listener, &QTcpServer::newConnection, this,
              [this, socket, listener]()
              {
                    auto currentSession = m_sessions.find(socket);
                    if (currentSession == m_sessions.end() ||
                        currentSession->listener != listener)
                    {
                          return;
                    }
                    while (listener->hasPendingConnections())
                    {
                          QTcpSocket *tcpSocket = listener->nextPendingConnection();
                          const quint64 id = currentSession->nextConnectionId++;
                          auto *connection = new MultiplayerConnection(this);
                          connection->adoptSocket(tcpSocket);
                          registerConnection(socket, id, connection, true);
                    }
              });
      if (!listener->listen(QHostAddress::AnyIPv4, port))
      {
            sendEvent(socket, QStringLiteral("error"),
                      {{QStringLiteral("message"), listener->errorString()}});
            listener->deleteLater();
            session->listener = nullptr;
            return;
      }

      QJsonArray addresses;
      for (const QString &address : localAddresses())
      {
            addresses.push_back(address);
      }
      sendEvent(socket, QStringLiteral("listening"),
                {{QStringLiteral("port"), static_cast<int>(listener->serverPort())},
                 {QStringLiteral("addresses"), addresses}});
}

void NetworkBridgeServer::connectToHost(QWebSocket *socket,
                                        const QHostAddress &address,
                                        quint16 port)
{
      auto session = m_sessions.find(socket);
      if (session == m_sessions.end())
      {
            return;
      }
      const quint64 id = session->nextConnectionId++;
      auto *connection = new MultiplayerConnection(this);
      registerConnection(socket, id, connection, false);
      connection->connectToHost(address, port);
}

void NetworkBridgeServer::registerConnection(
      QWebSocket *socket, quint64 id, MultiplayerConnection *connection,
      bool acceptedConnection)
{
      auto session = m_sessions.find(socket);
      if (session == m_sessions.end())
      {
            connection->deleteLater();
            return;
      }
      session->connections.insert(id, connection);
      connect(connection, &MultiplayerConnection::connected, this,
              [this, socket, id, connection]()
              {
                    sendEvent(socket, QStringLiteral("connected"),
                              {{QStringLiteral("connectionId"),
                                QString::number(id)},
                               {QStringLiteral("peerAddress"),
                                connection->peerAddress().toString()}});
              });
      connect(connection, &MultiplayerConnection::lineReceived, this,
              [this, socket, id](const QByteArray &line)
              {
                    sendEvent(socket, QStringLiteral("line"),
                              {{QStringLiteral("connectionId"),
                                QString::number(id)},
                               {QStringLiteral("line"),
                                QString::fromUtf8(line)}});
              });
      connect(connection, &MultiplayerConnection::transportError, this,
              [this, socket, id](const QString &message)
              {
                    sendEvent(socket, QStringLiteral("error"),
                              {{QStringLiteral("connectionId"),
                                QString::number(id)},
                               {QStringLiteral("message"), message}});
              });
      connect(connection, &MultiplayerConnection::disconnected, this,
              [this, socket, id, connection]()
              {
                    sendEvent(socket, QStringLiteral("disconnected"),
                              {{QStringLiteral("connectionId"),
                                QString::number(id)}});
                    auto session = m_sessions.find(socket);
                    if (session != m_sessions.end())
                    {
                          session->connections.remove(id);
                    }
                    connection->deleteLater();
              });
      if (acceptedConnection)
      {
            sendEvent(socket, QStringLiteral("accepted"),
                      {{QStringLiteral("connectionId"), QString::number(id)},
                       {QStringLiteral("peerAddress"),
                        connection->peerAddress().toString()}});
      }
}

void NetworkBridgeServer::sendEvent(QWebSocket *socket, const QString &event,
                                    const QJsonObject &fields) const
{
      if (socket == nullptr ||
          socket->state() != QAbstractSocket::ConnectedState)
      {
            return;
      }
      QJsonObject object = fields;
      object.insert(QStringLiteral("event"), event);
      socket->sendTextMessage(
            QString::fromUtf8(QJsonDocument(object).toJson(
                  QJsonDocument::Compact)));
}

void NetworkBridgeServer::closeSession(QWebSocket *socket)
{
      auto session = m_sessions.find(socket);
      if (session == m_sessions.end())
      {
            return;
      }
      if (session->listener != nullptr)
      {
            session->listener->close();
            session->listener->deleteLater();
      }
      const auto connections = session->connections;
      m_sessions.erase(session);
      for (MultiplayerConnection *connection : connections)
      {
            connection->disconnect(this);
            connection->close();
            connection->deleteLater();
      }
}

QStringList NetworkBridgeServer::localAddresses()
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
