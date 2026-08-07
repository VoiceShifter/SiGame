#ifndef MULTIPLAYERCONNECTION_H
#define MULTIPLAYERCONNECTION_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>

class QTcpSocket;

class MultiplayerConnection : public QObject
{
      Q_OBJECT

    public:
      explicit MultiplayerConnection(QObject *parent = nullptr);
      ~MultiplayerConnection() override;

      void connectToHost(const QHostAddress &address, quint16 port);
      void adoptSocket(QTcpSocket *socket);
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

      QTcpSocket *m_socket{};
      QByteArray m_readBuffer;
};

#endif // MULTIPLAYERCONNECTION_H
