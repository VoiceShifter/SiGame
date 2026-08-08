#include "playeridentity.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLockFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <memory>

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

struct ProcessIdentityState
{
      QString token;
      QString settingsKey;
      std::unique_ptr<QLockFile> lock;
};

ProcessIdentityState &processIdentityState()
{
      static ProcessIdentityState state = []()
      {
            ProcessIdentityState result;
            QSettings settings = identitySettings();
            QDir lockDirectory(
                  QStandardPaths::writableLocation(
                        QStandardPaths::GenericConfigLocation));
            lockDirectory.mkpath(QStringLiteral("SiGame"));
            lockDirectory.cd(QStringLiteral("SiGame"));
            constexpr int maximumLocalInstances = 16;
            for (int slot = 0; slot < maximumLocalInstances; ++slot)
            {
                  auto lock = std::make_unique<QLockFile>(
                        lockDirectory.filePath(
                              QStringLiteral("identity-%1.lock").arg(slot)));
                  if (!lock->tryLock())
                  {
                        continue;
                  }
                  result.settingsKey =
                        slot == 0
                              ? QStringLiteral("token")
                              : QStringLiteral("instanceTokens/%1").arg(slot);
                  result.token = settings.value(result.settingsKey).toString();
                  if (result.token.isEmpty())
                  {
                        result.token = newToken();
                        settings.setValue(result.settingsKey, result.token);
                        settings.sync();
                  }
                  result.lock = std::move(lock);
                  return result;
            }
            result.token = newToken();
            return result;
      }();
      return state;
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
      identity.token = processIdentityState().token;
      identity.nickname = settings.value(QStringLiteral("nickname")).toString();
      identity.profilePath =
            settings.value(QStringLiteral("profilePath")).toString();
      identity.loadProfile();
      return identity;
}

bool savePlayerIdentity(const PlayerIdentity &identity)
{
      QSettings settings = identitySettings();
      ProcessIdentityState &state = processIdentityState();
      state.token = identity.token.isEmpty() ? state.token : identity.token;
      if (!state.settingsKey.isEmpty())
      {
            settings.setValue(state.settingsKey, state.token);
      }
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
