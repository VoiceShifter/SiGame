#include "singleplayerscreen.h"
#include "ui_singleplayerscreen.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path packCachePath()
{
      const QByteArray path =
            QCoreApplication::applicationDirPath().toUtf8() +
            "/last_pack.csv";
      return std::filesystem::u8path(path.constData());
}

bool isValidPack(const QString &path)
{
      const QDir pack(path);
      return QFileInfo(pack.filePath(QStringLiteral("Audio"))).isDir() &&
             QFileInfo(pack.filePath(QStringLiteral("content.xml"))).isFile() &&
             QFileInfo(pack.filePath(QStringLiteral("Images"))).isDir() &&
             QFileInfo(pack.filePath(QStringLiteral("quality.marker"))).isFile() &&
             QFileInfo(pack.filePath(QStringLiteral("Video"))).isDir();
}

QString readCachedPack()
{
      std::ifstream cacheFile(packCachePath());
      std::string row;
      if (!std::getline(cacheFile, row) || row.size() < 2 || row.front() != '"')
      {
            return {};
      }

      std::string path;
      for (std::size_t index = 1; index < row.size(); ++index)
      {
            if (row[index] != '"')
            {
                  path += row[index];
            }
            else if (index + 1 < row.size() && row[index + 1] == '"')
            {
                  path += '"';
                  ++index;
            }
            else
            {
                  return QString::fromUtf8(path.data(),
                                           static_cast<qsizetype>(path.size()));
            }
      }
      return {};
}

void cachePack(const QString &path)
{
      std::string escapedPath = path.toUtf8().toStdString();
      std::size_t quote = 0;
      while ((quote = escapedPath.find('"', quote)) != std::string::npos)
      {
            escapedPath.insert(quote, 1, '"');
            quote += 2;
      }

      std::ofstream cacheFile(packCachePath(), std::ios::trunc);
      cacheFile << '"' << escapedPath << '"' << '\n';
}
} // namespace

SinglePlayerScreen::SinglePlayerScreen(QWidget *parent)
      : QWidget(parent), ui(new Ui::SinglePlayerScreen), GamepackPath{""}
{
      ui->setupUi(this);
      connect(ui->horizontalSlider, &QSlider::sliderMoved, this,
              [this](int value)
              { ui->playerCount->setText(QString::number(value)); });
      connect(ui->horizontalSlider, &QSlider::valueChanged, this,
              [this](int value)
              { ui->playerCount->setText(QString::number(value)); });
      connect(ui->pickPuckButton, &QPushButton::clicked, this,
              &SinglePlayerScreen::pickPack);
      connect(ui->createButton, &QPushButton::clicked, this,
              &SinglePlayerScreen::createGame);

      const QString cachedPack = readCachedPack();
      if (!cachedPack.isEmpty())
      {
            usePack(cachedPack, false);
      }
}

SinglePlayerScreen::~SinglePlayerScreen() { delete ui; }

void SinglePlayerScreen::pickPack()
{
      QString fileName = QFileDialog::getExistingDirectory(
            this, tr("Open pack"), "/home/username");
      if (fileName.isEmpty())
      {
            return;
      }
      usePack(fileName, true);
}

void SinglePlayerScreen::usePack(const QString &path, bool showInvalidWarning)
{
      if (!isValidPack(path))
      {
            if (showInvalidWarning)
            {
                  QMessageBox::warning(
                        this, tr("Invalid pack"),
                        tr("The selected folder is not a valid game pack. It "
                           "must contain Audio, content.xml, Images, "
                           "quality.marker, and Video."));
            }
            return;
      }

      GamepackPath = QDir(path).absolutePath();
      ui->pickPuckButton->setText(GamepackPath);
      cachePack(GamepackPath);
}

void SinglePlayerScreen::createGame()
{
      if (GamepackPath.isEmpty())
      {
            return;
      }
      emit SingleGameStarted(ui->playerCount->text().toInt(), GamepackPath);
}
