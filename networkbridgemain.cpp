#include "networkbridgeserver.h"

#include "multiplayerprotocol.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
      QCoreApplication application(argc, argv);
      QCoreApplication::setApplicationName(QStringLiteral("SiGameBridge"));

      QCommandLineParser parser;
      parser.setApplicationDescription(
            QStringLiteral("Native TCP bridge for the SiGame browser build"));
      parser.addHelpOption();
      const QCommandLineOption portOption(
            {QStringLiteral("p"), QStringLiteral("port")},
            QStringLiteral("Local WebSocket bridge port"),
            QStringLiteral("port"),
            QString::number(MultiplayerProtocol::BridgePort));
      parser.addOption(portOption);
      parser.process(application);

      bool ok = false;
      const int parsedPort = parser.value(portOption).toInt(&ok);
      if (!ok || parsedPort < 1 || parsedPort > 65535)
      {
            QTextStream(stderr) << "Invalid bridge port\n";
            return 1;
      }

      NetworkBridgeServer bridge;
      if (!bridge.listen(static_cast<quint16>(parsedPort)))
      {
            QTextStream(stderr) << "Unable to start SiGameBridge: "
                                << bridge.errorString() << '\n';
            return 2;
      }

      QTextStream(stdout) << "SiGameBridge listening at ws://127.0.0.1:"
                          << parsedPort << '\n';
      return application.exec();
}
