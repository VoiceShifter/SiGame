#ifndef MULTIPLAYERPROTOCOL_H
#define MULTIPLAYERPROTOCOL_H

#include "playerstate.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QtGlobal>

namespace MultiplayerProtocol
{
constexpr int ProtocolVersion = 1;
constexpr int MaxControlLineBytes = 64 * 1024;
constexpr int MaxProfileBytes = 4 * 1024 * 1024;
constexpr int MaxProfileChunkBytes = 32 * 1024;
constexpr quint16 DefaultPort = 32323;
constexpr quint16 BridgePort = 32324;

struct Frame
{
      QString command;
      QMap<QString, QString> fields;
};

bool parseFrame(const QByteArray &line, Frame *frame,
                QString *errorMessage = nullptr);
QByteArray encodeFrame(const QString &command,
                       const QMap<QString, QString> &fields = {});

QString encodeValue(const QString &value);
bool decodeValue(const QString &value, QString *decoded,
                 QString *errorMessage = nullptr);

bool parseUnsigned(const QMap<QString, QString> &fields, const QString &name,
                   quint64 *value, QString *errorMessage = nullptr);
bool parseSigned(const QMap<QString, QString> &fields, const QString &name,
                 qint64 *value, QString *errorMessage = nullptr);
bool parseBool(const QMap<QString, QString> &fields, const QString &name,
              bool *value, QString *errorMessage = nullptr);

QString phaseName(int phase);
QString phaseName(SessionPhase phase);
bool phaseFromName(const QString &name, SessionPhase *phase);
QString answerTypeName(int type);
QString mediaTypeName(int type);
QString questionTypeName(int type);

bool isKnownCommand(const QString &command);
}

#endif // MULTIPLAYERPROTOCOL_H
