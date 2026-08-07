#include "playeridentity.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QSettings>
#include <QUuid>

namespace
{
QSettings identitySettings()
{
      return QSettings(QStringLiteral("SiGame"), QStringLiteral("SiGame"));
}

QString newToken()
{
      return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

PlayerIdentity PlayerIdentity::load()
{
      return loadPlayerIdentity();
}

bool PlayerIdentity::save() const
{
      return savePlayerIdentity(*this);
}

bool PlayerIdentity::loadProfile(QString *errorMessage)
{
      if (profilePath.isEmpty())
      {
            profilePng.clear();
            return true;
      }

      const QImage image(profilePath);
      if (image.isNull())
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Unable to load profile image");
            }
            return false;
      }

      QByteArray data;
      QBuffer buffer(&data);
      if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Unable to encode profile image");
            }
            return false;
      }
      profilePng = data;
      return true;
}

PlayerIdentity loadPlayerIdentity()
{
      QSettings settings = identitySettings();
      PlayerIdentity identity;
      identity.token = settings.value(QStringLiteral("token")).toString();
      if (identity.token.isEmpty())
      {
            identity.token = newToken();
            settings.setValue(QStringLiteral("token"), identity.token);
            settings.sync();
      }
      identity.nickname = settings.value(QStringLiteral("nickname")).toString();
      identity.profilePath =
            settings.value(QStringLiteral("profilePath")).toString();
      identity.loadProfile();
      return identity;
}

bool savePlayerIdentity(const PlayerIdentity &identity)
{
      QSettings settings = identitySettings();
      settings.setValue(QStringLiteral("token"), identity.token.isEmpty()
                                                   ? newToken()
                                                   : identity.token);
      settings.setValue(QStringLiteral("nickname"), identity.nickname);
      settings.setValue(QStringLiteral("profilePath"), identity.profilePath);
      settings.sync();
      return settings.status() == QSettings::NoError;
}

QByteArray loadProfilePng(const QString &path)
{
      if (path.isEmpty())
      {
            return {};
      }
      QImage image(path);
      if (image.isNull())
      {
            return {};
      }
      QByteArray data;
      QBuffer buffer(&data);
      if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
      {
            return {};
      }
      return data;
}

QString profileSha256(const QByteArray &profilePng)
{
      return QString::fromLatin1(
            QCryptographicHash::hash(profilePng, QCryptographicHash::Sha256)
                  .toHex());
}

QString profileBase64(const QByteArray &profilePng)
{
      return QString::fromLatin1(
            profilePng.toBase64(QByteArray::Base64UrlEncoding |
                                QByteArray::OmitTrailingEquals));
}

QByteArray profileFromBase64(const QString &encoded)
{
      return QByteArray::fromBase64(encoded.toLatin1(),
                                    QByteArray::Base64UrlEncoding);
}
