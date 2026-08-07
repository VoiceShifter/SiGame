#include "multiplayerconnection.h"

#include "multiplayerprotocol.h"

#include <QMetaObject>
#include <QThread>
#include <QTcpSocket>

MultiplayerConnection::MultiplayerConnection(QObject *parent) : QObject(parent) {}

MultiplayerConnection::~MultiplayerConnection()
{
      if (m_socket != nullptr)
      {
            m_socket->disconnect(this);
            m_socket->close();
            m_socket->deleteLater();
      }
}

void MultiplayerConnection::connectToHost(const QHostAddress &address,
                                           quint16 port)
{
      if (m_socket != nullptr)
      {
            m_socket->abort();
            m_socket->deleteLater();
      }
      attachSocket(new QTcpSocket(this));
      m_socket->connectToHost(address, port);
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
      if (m_socket != nullptr)
      {
            m_socket->close();
      }
}

bool MultiplayerConnection::isConnected() const
{
      return m_socket != nullptr &&
             m_socket->state() == QAbstractSocket::ConnectedState;
}

QHostAddress MultiplayerConnection::peerAddress() const
{
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
      if (m_socket == nullptr || !isConnected())
      {
            return;
      }
      if (!line.endsWith('\n'))
      {
            line += '\n';
      }
      if (line.size() > MultiplayerProtocol::MaxControlLineBytes)
      {
            emit transportError(QStringLiteral("Control line is too large"));
            return;
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

void MultiplayerConnection::rejectOversizedLine()
{
      emit transportError(QStringLiteral("Control line is too large"));
      close();
      m_readBuffer.clear();
}
