#ifndef PLAYERIDENTITY_H
#define PLAYERIDENTITY_H

#include <QByteArray>
#include <QString>

struct PlayerIdentity
{
      QString token;
      QString nickname;
      QString profilePath;
      QByteArray profilePng;

      static PlayerIdentity load();
      bool save() const;
      bool loadProfile(QString *errorMessage = nullptr);
};

PlayerIdentity loadPlayerIdentity();
bool savePlayerIdentity(const PlayerIdentity &identity);
QByteArray loadProfilePng(const QString &path);
QString profileSha256(const QByteArray &profilePng);
QString profileBase64(const QByteArray &profilePng);
QByteArray profileFromBase64(const QString &encoded);

#endif // PLAYERIDENTITY_H
