#include "singleplayerscreen.h"
#include "ui_singleplayerscreen.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPixmap>
#include <QTextStream>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

std::vector<QString> readCachedSettings()
{
      std::ifstream cacheFile(packCachePath());
      std::string row;
      if (!std::getline(cacheFile, row))
      {
            return {};
      }

      std::vector<QString> fields;
      std::size_t index = 0;
      while (index < row.size())
      {
            if (row[index++] != '"')
            {
                  return {};
            }

            std::string value;
            while (index < row.size())
            {
                  if (row[index] != '"')
                  {
                        value += row[index++];
                  }
                  else if (index + 1 < row.size() && row[index + 1] == '"')
                  {
                        value += '"';
                        index += 2;
                  }
                  else
                  {
                        ++index;
                        fields.push_back(QString::fromUtf8(
                              value.data(),
                              static_cast<qsizetype>(value.size())));
                        if (index == row.size())
                        {
                              return fields;
                        }
                        if (row[index++] != ',')
                        {
                              return {};
                        }
                        break;
                  }
            }
      }
      return fields;
}

std::string csvField(const QString &value)
{
      std::string field = value.toUtf8().toStdString();
      std::size_t quote = 0;
      while ((quote = field.find('"', quote)) != std::string::npos)
      {
            field.insert(quote, 1, '"');
            quote += 2;
      }
      return '"' + field + '"';
}

void cacheSettings(const QString &packPath, const QString &picturePath,
                   const QString &nickname, int playerCount,
                   int answerDuration, int questionDuration,
                   int questionPickDuration, int answerWaitDuration)
{
      std::ofstream cacheFile(packCachePath(), std::ios::trunc);
      cacheFile << csvField(packPath) << ',' << csvField(picturePath) << ','
                << csvField(nickname) << ','
                << csvField(QString::number(playerCount)) << ','
                << csvField(QString::number(answerDuration)) << ','
                << csvField(QString::number(questionDuration)) << ','
                << csvField(QString::number(questionPickDuration)) << ','
                << csvField(QString::number(answerWaitDuration)) << '\n';
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
      connect(ui->pickProfilePictureButton, &QPushButton::clicked, this,
              &SinglePlayerScreen::pickProfilePicture);
      connect(ui->nicknameLineEdit, &QLineEdit::editingFinished, this,
              &SinglePlayerScreen::saveCache);
      connect(ui->createButton, &QPushButton::clicked, this,
              &SinglePlayerScreen::createGame);
      connect(ui->backButton, &QPushButton::clicked, this,
              &SinglePlayerScreen::cancelled);

      const std::vector<QString> cachedSettings = readCachedSettings();
      if (!cachedSettings.empty())
      {
            usePack(cachedSettings[0], false, false);
      }
      if (cachedSettings.size() > 1)
      {
            useProfilePicture(cachedSettings[1], false, false);
      }
      if (cachedSettings.size() > 2)
      {
            ui->nicknameLineEdit->setText(cachedSettings[2]);
      }
      if (cachedSettings.size() > 3)
      {
            ui->horizontalSlider->setValue(cachedSettings[3].toInt());
      }
      if (cachedSettings.size() > 4)
      {
            ui->answerDurationSpinBox->setValue(cachedSettings[4].toInt());
      }
      if (cachedSettings.size() > 5)
      {
            ui->questionDurationSpinBox->setValue(cachedSettings[5].toInt());
      }
      if (cachedSettings.size() > 6)
      {
            ui->questionPickDurationSpinBox->setValue(
                  cachedSettings[6].toInt());
      }
      if (cachedSettings.size() > 7)
      {
            ui->answerWaitDurationSpinBox->setValue(cachedSettings[7].toInt());
      }

      connect(ui->horizontalSlider, &QSlider::valueChanged, this,
              [this]() { saveCache(); });
      connect(ui->answerDurationSpinBox, &QSpinBox::valueChanged, this,
              [this]() { saveCache(); });
      connect(ui->questionDurationSpinBox, &QSpinBox::valueChanged, this,
              [this]() { saveCache(); });
      connect(ui->questionPickDurationSpinBox, &QSpinBox::valueChanged, this,
              [this]() { saveCache(); });
      connect(ui->answerWaitDurationSpinBox, &QSpinBox::valueChanged, this,
              [this]() { saveCache(); });
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

void SinglePlayerScreen::usePack(const QString &path, bool showInvalidWarning,
                                 bool updateCache)
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
      if (updateCache)
      {
            saveCache();
      }
}

void SinglePlayerScreen::pickProfilePicture()
{
      const QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open profile picture"), "/home/username",
            tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
      if (fileName.isEmpty())
      {
            return;
      }
      useProfilePicture(fileName, true);
}

void SinglePlayerScreen::useProfilePicture(const QString &path,
                                           bool showInvalidWarning,
                                           bool updateCache)
{
      const QPixmap picture(path);
      if (picture.isNull())
      {
            if (showInvalidWarning)
            {
                  QMessageBox::warning(
                        this, tr("Invalid profile picture"),
                        tr("The selected file is not a valid image."));
            }
            return;
      }

      ProfilePicturePath = QFileInfo(path).absoluteFilePath();
      ui->profilePicturePreview->setPixmap(picture);
      if (updateCache)
      {
            saveCache();
      }
}

void SinglePlayerScreen::saveCache() const
{
      cacheSettings(GamepackPath, ProfilePicturePath,
                    ui->nicknameLineEdit->text(),
                    ui->horizontalSlider->value(),
                    ui->answerDurationSpinBox->value(),
                    ui->questionDurationSpinBox->value(),
                    ui->questionPickDurationSpinBox->value(),
                    ui->answerWaitDurationSpinBox->value());
}

void SinglePlayerScreen::createGame()
{
      if (GamepackPath.isEmpty())
      {
            return;
      }
      emit SingleGameStarted(
            ui->playerCount->text().toInt(), GamepackPath, ProfilePicturePath,
            ui->nicknameLineEdit->text(),
            ui->answerDurationSpinBox->value(),
            ui->questionDurationSpinBox->value(),
            ui->questionPickDurationSpinBox->value(),
            ui->answerWaitDurationSpinBox->value());
}
