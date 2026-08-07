#include "packmanifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace
{
QByteArray hashFile(const QString &path, QString *errorMessage)
{
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Unable to read %1")
                                      .arg(QDir::toNativeSeparators(path));
            }
            return {};
      }

      QCryptographicHash hash(QCryptographicHash::Sha256);
      while (!file.atEnd())
      {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty() && !file.atEnd())
            {
                  if (errorMessage != nullptr)
                  {
                        *errorMessage = QStringLiteral("Unable to read %1")
                                            .arg(QDir::toNativeSeparators(path));
                  }
                  return {};
            }
            hash.addData(chunk);
      }
      return hash.result();
}

QByteArray canonicalManifest(const QVector<PackManifestEntry> &entries)
{
      QByteArray canonical;
      for (const PackManifestEntry &entry : entries)
      {
            canonical += entry.relativePath.toUtf8();
            canonical += '\t';
            canonical += QByteArray::number(entry.size);
            canonical += '\t';
            canonical += entry.sha256.toHex();
            canonical += '\n';
      }
      return canonical;
}
} // namespace

bool isValidPackDirectory(const QString &path)
{
      const QDir pack(path);
      return QFileInfo(pack.filePath(QStringLiteral("Audio"))).isDir() &&
             QFileInfo(pack.filePath(QStringLiteral("content.xml"))).isFile() &&
             QFileInfo(pack.filePath(QStringLiteral("Images"))).isDir() &&
             QFileInfo(pack.filePath(QStringLiteral("quality.marker"))).isFile() &&
             QFileInfo(pack.filePath(QStringLiteral("Video"))).isDir();
}

bool buildPackManifest(const QString &path, PackManifest *manifest,
                       QString *errorMessage)
{
      if (manifest == nullptr)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Manifest output is null");
            }
            return false;
      }

      *manifest = {};
      const QFileInfo rootInfo(path);
      if (!rootInfo.isDir())
      {
            manifest->error = QStringLiteral("Pack directory does not exist");
            if (errorMessage != nullptr)
            {
                  *errorMessage = manifest->error;
            }
            return false;
      }
      manifest->rootPath = rootInfo.absoluteFilePath();

      QDirIterator iterator(manifest->rootPath, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
      while (iterator.hasNext())
      {
            const QString absolutePath = iterator.next();
            const QFileInfo info(absolutePath);
            if (!info.isFile() || info.isSymLink())
            {
                  continue;
            }

            QString relative = QDir(manifest->rootPath).relativeFilePath(
                  absolutePath);
            relative.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QByteArray hash = hashFile(absolutePath, errorMessage);
            if (hash.isEmpty())
            {
                  manifest->error = errorMessage != nullptr
                                          ? *errorMessage
                                          : QStringLiteral("Unable to hash pack file");
                  return false;
            }
            manifest->entries.push_back({relative, info.size(), hash});
      }

      std::sort(manifest->entries.begin(), manifest->entries.end(),
                [](const PackManifestEntry &left, const PackManifestEntry &right)
                { return left.relativePath < right.relativePath; });
      manifest->hash = QString::fromLatin1(
            QCryptographicHash::hash(canonicalManifest(manifest->entries),
                                     QCryptographicHash::Sha256)
                  .toHex());
      return true;
}

bool verifyPackManifest(const QString &path, const PackManifest &expected,
                        QString *errorMessage)
{
      PackManifest actual;
      if (!buildPackManifest(path, &actual, errorMessage))
      {
            return false;
      }
      if (actual.hash.compare(expected.hash, Qt::CaseInsensitive) != 0)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Pack manifest hash mismatch");
            }
            return false;
      }
      if (actual.entries.size() != expected.entries.size())
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Pack manifest file count mismatch");
            }
            return false;
      }
      for (int index = 0; index < actual.entries.size(); ++index)
      {
            const PackManifestEntry &left = actual.entries[index];
            const PackManifestEntry &right = expected.entries[index];
            if (left.relativePath != right.relativePath || left.size != right.size ||
                left.sha256 != right.sha256)
            {
                  if (errorMessage != nullptr)
                  {
                        *errorMessage = QStringLiteral("Pack file mismatch: %1")
                                            .arg(left.relativePath);
                  }
                  return false;
            }
      }
      return true;
}

QString packManifestHash(const QString &path, QString *errorMessage)
{
      PackManifest manifest;
      if (!buildPackManifest(path, &manifest, errorMessage))
      {
            return {};
      }
      return manifest.hash;
}
