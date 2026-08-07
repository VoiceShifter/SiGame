#ifndef PACKMANIFEST_H
#define PACKMANIFEST_H

#include <QByteArray>
#include <QString>
#include <QVector>

struct PackManifestEntry
{
      QString relativePath;
      qint64 size{};
      QByteArray sha256;
};

struct PackManifest
{
      QString rootPath;
      QString hash;
      QVector<PackManifestEntry> entries;
      QString error;

      bool isValid() const { return !hash.isEmpty() && error.isEmpty(); }
};

bool isValidPackDirectory(const QString &path);
bool buildPackManifest(const QString &path, PackManifest *manifest,
                       QString *errorMessage = nullptr);
bool verifyPackManifest(const QString &path, const PackManifest &expected,
                        QString *errorMessage = nullptr);
QString packManifestHash(const QString &path, QString *errorMessage = nullptr);

#endif // PACKMANIFEST_H
