#include "multiplayerconnection.h"

#include "multiplayerprotocol.h"

#include <QMetaObject>
#include <QThread>
#include <QTcpSocket>
#ifdef Q_OS_WASM
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>
#include <QWebSocket>
#endif

MultiplayerConnection::MultiplayerConnection(QObject *parent) : QObject(parent) {}

MultiplayerConnection::~MultiplayerConnection()
{
      if (m_socket != nullptr)
      {
            m_socket->disconnect(this);
            m_socket->close();
            m_socket->deleteLater();
      }
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr && m_ownsBridgeSocket)
      {
            m_bridgeSocket->disconnect(this);
            m_bridgeSocket->close();
            m_bridgeSocket->deleteLater();
      }
#endif
}

void MultiplayerConnection::connectToHost(const QHostAddress &address,
                                           quint16 port)
{
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr && m_ownsBridgeSocket)
      {
            m_bridgeSocket->disconnect(this);
            m_bridgeSocket->close();
            m_bridgeSocket->deleteLater();
      }
      m_bridgeConnectionId = 0;
      m_bridgePeerAddress = address;
      m_bridgeConnected = false;
      m_ownsBridgeSocket = true;
      m_bridgeSocket = new QWebSocket(QStringLiteral("http://localhost"),
                                      QWebSocketProtocol::VersionLatest, this);
      connect(m_bridgeSocket, &QWebSocket::connected, this,
              [this, address, port]()
              {
                    QJsonObject command;
                    command.insert(QStringLiteral("command"),
                                   QStringLiteral("connect"));
                    command.insert(QStringLiteral("address"),
                                   address.toString());
                    command.insert(QStringLiteral("port"),
                                   static_cast<int>(port));
                    m_bridgeSocket->sendTextMessage(QString::fromUtf8(
                          QJsonDocument(command).toJson(QJsonDocument::Compact)));
              });
      connect(m_bridgeSocket, &QWebSocket::textMessageReceived, this,
              &MultiplayerConnection::handleBridgeMessage);
      connect(m_bridgeSocket, &QWebSocket::disconnected, this,
              [this]()
              {
                    const bool wasConnected = m_bridgeConnected;
                    m_bridgeConnected = false;
                    if (wasConnected)
                    {
                          emit disconnected();
                    }
                    else
                    {
                          const QString reason = m_bridgeSocket->closeReason();
                          emit transportError(
                                reason.isEmpty()
                                      ? tr("The native network bridge closed "
                                           "the connection")
                                      : tr("The native network bridge closed "
                                           "the connection: %1")
                                              .arg(reason));
                    }
              });
      connect(m_bridgeSocket, &QWebSocket::errorOccurred, this,
              [this](QAbstractSocket::SocketError)
              {
                    emit transportError(
                          tr("Native network bridge: %1")
                                .arg(m_bridgeSocket->errorString()));
              });
      m_bridgeSocket->open(
            QUrl(QStringLiteral("ws://127.0.0.1:%1")
                       .arg(MultiplayerProtocol::BridgePort)));
      return;
#else
      if (m_socket != nullptr)
      {
            m_socket->abort();
            m_socket->deleteLater();
      }
      attachSocket(new QTcpSocket(this));
      m_socket->connectToHost(address, port);
#endif
}

void MultiplayerConnection::adoptSocket(QTcpSocket *socket)
{
      if (socket == nullptr)
      {
            return;
      }
      if (m_socket != nullptr)
      {
            m_socket->disconnect(this);
            m_socket->close();
            m_socket->deleteLater();
      }
      socket->setParent(this);
      attachSocket(socket);
      if (socket->state() == QAbstractSocket::ConnectedState)
      {
            emit connected();
      }
}

#ifdef Q_OS_WASM
void MultiplayerConnection::adoptBridgeConnection(
      QWebSocket *socket, quint64 connectionId,
      const QHostAddress &peerAddress)
{
      if (socket == nullptr || connectionId == 0)
      {
            return;
      }
      m_bridgeSocket = socket;
      m_bridgeConnectionId = connectionId;
      m_bridgePeerAddress = peerAddress;
      m_bridgeConnected = true;
      m_ownsBridgeSocket = false;
}

void MultiplayerConnection::deliverBridgeLine(const QByteArray &line)
{
      if (line.size() > MultiplayerProtocol::MaxControlLineBytes)
      {
            rejectOversizedLine();
            return;
      }
      emit lineReceived(line);
}

void MultiplayerConnection::bridgeDisconnected()
{
      if (!m_bridgeConnected)
      {
            return;
      }
      m_bridgeConnected = false;
      emit disconnected();
}

void MultiplayerConnection::bridgeTransportError(const QString &message)
{
      emit transportError(message);
}
#endif

void MultiplayerConnection::attachSocket(QTcpSocket *socket)
{
      m_socket = socket;
      m_readBuffer.clear();
      connect(m_socket, &QTcpSocket::readyRead, this,
              &MultiplayerConnection::readAvailable);
      connect(m_socket, &QTcpSocket::connected, this,
              &MultiplayerConnection::connected);
      connect(m_socket, &QTcpSocket::disconnected, this,
              &MultiplayerConnection::handleDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
      connect(m_socket, &QTcpSocket::errorOccurred, this,
              &MultiplayerConnection::handleSocketError);
#else
      connect(m_socket,
              QOverload<QAbstractSocket::SocketError>::of(
                    &QTcpSocket::error),
              this, &MultiplayerConnection::handleSocketError);
#endif
}

void MultiplayerConnection::close()
{
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr)
      {
            if (m_bridgeConnectionId != 0)
            {
                  sendBridgeCommand(QStringLiteral("close"));
            }
            if (m_ownsBridgeSocket)
            {
                  m_bridgeSocket->close();
            }
            return;
      }
#endif
      if (m_socket != nullptr)
      {
            m_socket->close();
      }
}

bool MultiplayerConnection::isConnected() const
{
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr)
      {
            return m_bridgeConnected;
      }
#endif
      return m_socket != nullptr &&
             m_socket->state() == QAbstractSocket::ConnectedState;
}

QHostAddress MultiplayerConnection::peerAddress() const
{
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr)
      {
            return m_bridgePeerAddress;
      }
#endif
      return m_socket == nullptr ? QHostAddress() : m_socket->peerAddress();
}

void MultiplayerConnection::sendLine(QByteArray line)
{
      if (QThread::currentThread() != thread())
      {
            QMetaObject::invokeMethod(this, "sendLine", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, line));
            return;
      }
      if (!isConnected())
      {
            return;
      }
      if (line.size() > MultiplayerProtocol::MaxControlLineBytes)
      {
            emit transportError(QStringLiteral("Control line is too large"));
            return;
      }
#ifdef Q_OS_WASM
      if (m_bridgeSocket != nullptr)
      {
            while (line.endsWith('\n') || line.endsWith('\r'))
            {
                  line.chop(1);
            }
            sendBridgeCommand(QStringLiteral("send"), line);
            return;
      }
#endif
      if (!line.endsWith('\n'))
      {
            line += '\n';
      }
      m_socket->write(line);
}

void MultiplayerConnection::readAvailable()
{
      if (m_socket == nullptr)
      {
            return;
      }
      m_readBuffer += m_socket->readAll();
      if (m_readBuffer.size() > MultiplayerProtocol::MaxControlLineBytes &&
          !m_readBuffer.contains('\n'))
      {
            rejectOversizedLine();
            return;
      }

      while (true)
      {
            const qsizetype newline = m_readBuffer.indexOf('\n');
            if (newline < 0)
            {
                  break;
            }
            QByteArray line = m_readBuffer.left(newline);
            m_readBuffer.remove(0, newline + 1);
            if (line.endsWith('\r'))
            {
                  line.chop(1);
            }
            if (line.size() > MultiplayerProtocol::MaxControlLineBytes)
            {
                  rejectOversizedLine();
                  return;
            }
            emit lineReceived(line);
      }
}

void MultiplayerConnection::handleDisconnected() { emit disconnected(); }

void MultiplayerConnection::handleSocketError()
{
      if (m_socket != nullptr)
      {
            emit transportError(m_socket->errorString());
      }
}

#ifdef Q_OS_WASM
void MultiplayerConnection::handleBridgeMessage(const QString &message)
{
      QJsonParseError parseError;
      const QJsonDocument document =
            QJsonDocument::fromJson(message.toUtf8(), &parseError);
      if (parseError.error != QJsonParseError::NoError || !document.isObject())
      {
            emit transportError(
                  tr("Invalid response from native network bridge"));
            return;
      }
      const QJsonObject object = document.object();
      const QString event = object.value(QStringLiteral("event")).toString();
      bool idOk = false;
      const quint64 id = object.value(QStringLiteral("connectionId"))
                               .toString()
                               .toULongLong(&idOk);
      if (event == QStringLiteral("connected"))
      {
            if (!idOk || id == 0)
            {
                  return;
            }
            m_bridgeConnectionId = id;
            const QHostAddress peer(
                  object.value(QStringLiteral("peerAddress")).toString());
            if (!peer.isNull())
            {
                  m_bridgePeerAddress = peer;
            }
            if (!m_bridgeConnected)
            {
                  m_bridgeConnected = true;
                  emit connected();
            }
            return;
      }
      if (event == QStringLiteral("error") &&
          (!idOk || m_bridgeConnectionId == 0 ||
           id == m_bridgeConnectionId))
      {
            bridgeTransportError(
                  object.value(QStringLiteral("message")).toString());
            return;
      }
      if (idOk && id != m_bridgeConnectionId)
      {
            return;
      }
      if (event == QStringLiteral("line"))
      {
            deliverBridgeLine(
                  object.value(QStringLiteral("line")).toString().toUtf8());
      }
      else if (event == QStringLiteral("disconnected"))
      {
            if (m_bridgeConnected)
            {
                  bridgeDisconnected();
            }
            else
            {
                  emit transportError(tr("Unable to connect to the game host"));
            }
      }
}

void MultiplayerConnection::sendBridgeCommand(const QString &command,
                                               const QByteArray &line)
{
      if (m_bridgeSocket == nullptr ||
          m_bridgeSocket->state() != QAbstractSocket::ConnectedState)
      {
            return;
      }
      QJsonObject object;
      object.insert(QStringLiteral("command"), command);
      object.insert(QStringLiteral("connectionId"),
                    QString::number(m_bridgeConnectionId));
      if (!line.isNull())
      {
            object.insert(QStringLiteral("line"), QString::fromUtf8(line));
      }
      m_bridgeSocket->sendTextMessage(QString::fromUtf8(
            QJsonDocument(object).toJson(QJsonDocument::Compact)));
}
#endif

void MultiplayerConnection::rejectOversizedLine()
{
      emit transportError(QStringLiteral("Control line is too large"));
      close();
      m_readBuffer.clear();
}
