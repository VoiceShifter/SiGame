#include "multiplayerjoinscreen.h"

#include "packmanifest.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QVBoxLayout>

MultiplayerJoinScreen::MultiplayerJoinScreen(QWidget *parent)
      : QWidget(parent), m_identity(PlayerIdentity::load())
{
      auto *layout = new QVBoxLayout(this);
      auto *form = new QFormLayout;
      QSettings settings(QStringLiteral("SiGame"), QStringLiteral("SiGame"));
      m_addressEdit = new QLineEdit(
            settings.value(QStringLiteral("join/address"),
                           QStringLiteral("127.0.0.1"))
                  .toString(),
            this);
      m_portSpin = new QSpinBox(this);
      m_portSpin->setRange(1, 65535);
      m_portSpin->setValue(settings.value(QStringLiteral("join/port"),
                                           MultiplayerProtocol::DefaultPort)
                                 .toInt());
      form->addRow(tr("Host address:"), m_addressEdit);
      form->addRow(tr("Port:"), m_portSpin);
      m_packPath = settings.value(QStringLiteral("join/packPath")).toString();
      m_packEdit = new QLineEdit(this);
      m_packEdit->setReadOnly(true);
      if (isValidPackDirectory(m_packPath))
      {
            m_packEdit->setText(m_packPath);
      }
      auto *packButton = new QPushButton(tr("Choose pack"), this);
      auto *packRow = new QHBoxLayout;
      packRow->addWidget(m_packEdit);
      packRow->addWidget(packButton);
      form->addRow(tr("Question pack:"), packRow);
      m_nicknameEdit = new QLineEdit(m_identity.nickname, this);
      form->addRow(tr("Nickname:"), m_nicknameEdit);
      m_profilePreview = new QLabel(this);
      m_profilePreview->setFixedSize(64, 64);
      m_profilePreview->setScaledContents(true);
      auto *profileButton = new QPushButton(tr("Choose profile picture"), this);
      auto *profileRow = new QHBoxLayout;
      profileRow->addWidget(m_profilePreview);
      profileRow->addWidget(profileButton);
      form->addRow(tr("Profile picture:"), profileRow);
      layout->addLayout(form);
      m_hashLabel = new QLabel(this);
      m_hostConfigLabel = new QLabel(this);
      m_hostConfigLabel->hide();
      m_statusLabel = new QLabel(tr("Choose the host pack before connecting."), this);
      m_rosterList = new QListWidget(this);
      m_connectButton = new QPushButton(tr("Connect"), this);
      layout->addWidget(m_hashLabel);
      layout->addWidget(m_hostConfigLabel);
      layout->addWidget(m_statusLabel);
      layout->addWidget(m_rosterList);
      layout->addWidget(m_connectButton);

      connect(packButton, &QPushButton::clicked, this,
              &MultiplayerJoinScreen::choosePack);
      connect(profileButton, &QPushButton::clicked, this,
              &MultiplayerJoinScreen::chooseProfile);
      connect(m_connectButton, &QPushButton::clicked, this,
              &MultiplayerJoinScreen::connectOrDisconnect);
}

void MultiplayerJoinScreen::choosePack()
{
      const QString path = QFileDialog::getExistingDirectory(this,
                                                               tr("Open pack"));
      if (path.isEmpty())
      {
            return;
      }
      if (!isValidPackDirectory(path))
      {
            QMessageBox::warning(this, tr("Invalid pack"),
                                 tr("The selected folder is not a valid game pack."));
            return;
      }
      m_packPath = QDir(path).absolutePath();
      m_packEdit->setText(m_packPath);
      QString error;
      const QString hash = packManifestHash(m_packPath, &error);
      m_hashLabel->setText(hash.isEmpty() ? tr("Pack hash: unavailable")
                                          : tr("Pack hash: %1").arg(hash));
      if (hash.isEmpty())
      {
            m_statusLabel->setText(error);
      }
}

void MultiplayerJoinScreen::chooseProfile()
{
      const QString path = QFileDialog::getOpenFileName(
            this, tr("Open profile picture"), QString(),
            tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp)"));
      if (path.isEmpty())
      {
            return;
      }
      const QImage image(path);
      if (image.isNull())
      {
            QMessageBox::warning(this, tr("Invalid profile picture"),
                                 tr("The selected file is not a valid image."));
            return;
      }
      m_profilePath = QFileInfo(path).absoluteFilePath();
      m_identity.profilePath = m_profilePath;
      m_identity.profilePng = loadProfilePng(m_profilePath);
      m_profilePreview->setPixmap(QPixmap::fromImage(image));
      m_identity.save();
}

bool MultiplayerJoinScreen::preparePack()
{
      if (m_packPath.isEmpty())
      {
            m_statusLabel->setText(tr("Select a valid pack first."));
            return false;
      }
      QString error;
      const QString hash = packManifestHash(m_packPath, &error);
      if (hash.isEmpty())
      {
            m_statusLabel->setText(error);
            return false;
      }
      m_identity.nickname = m_nicknameEdit->text();
      m_identity.save();
      QSettings settings(QStringLiteral("SiGame"), QStringLiteral("SiGame"));
      settings.setValue(QStringLiteral("join/address"), m_addressEdit->text());
      settings.setValue(QStringLiteral("join/port"), m_portSpin->value());
      settings.setValue(QStringLiteral("join/packPath"), m_packPath);
      settings.sync();
      return true;
}

void MultiplayerJoinScreen::connectOrDisconnect()
{
      if (m_client != nullptr && m_client->isConnected())
      {
            m_client->disconnectFromHost();
            return;
      }
      if (!preparePack())
      {
            return;
      }
      QHostAddress address;
      if (!address.setAddress(m_addressEdit->text().trimmed()))
      {
            m_statusLabel->setText(tr("Enter a valid IPv4 address."));
            return;
      }
      QString error;
      const QString hash = packManifestHash(m_packPath, &error);
      GameConfig config;
      config.gamepackPath = m_packPath;
      config.packHash = hash;
      m_client = new MultiplayerClient(this);
      connect(m_client, &MultiplayerClient::connected, this,
              [this](const PlayerId &, bool reconnected)
              { updateStatus(reconnected ? tr("Reconnected.")
                                           : tr("Connected to lobby.")); });
      connect(m_client, &MultiplayerClient::disconnected, this,
              [this](const QString &reason)
              {
                    m_connectButton->setText(tr("Connect"));
                    updateStatus(reason);
              });
      connect(m_client, &MultiplayerClient::lobbyChanged, this,
              &MultiplayerJoinScreen::updateRoster);
      connect(m_client, &MultiplayerClient::configurationReceived, this,
              [this](const GameConfig &config)
              {
                    m_hostConfigLabel->setText(
                          tr("Host settings: answer %1 s, question %2 s, pick %3 s, wait %4 s")
                                .arg(config.answerDurationMs / 1000U)
                                .arg(config.questionDurationMs / 1000U)
                                .arg(config.questionPickDurationMs / 1000U)
                                .arg(config.answerWaitDurationMs / 1000U));
                    m_hostConfigLabel->show();
              });
      connect(m_client, &MultiplayerClient::protocolError, this,
              &MultiplayerJoinScreen::showError);
      connect(m_client, &MultiplayerClient::gameStarted, this,
              [this]()
              {
                    m_connectButton->setEnabled(false);
                    emit gameStarted(m_client, m_packPath);
              });
      m_client->connectToHost(address, static_cast<quint16>(m_portSpin->value()),
                              config, m_identity);
      m_connectButton->setText(tr("Disconnect"));
      m_statusLabel->setText(tr("Connecting..."));
}

void MultiplayerJoinScreen::updateRoster(const QVector<PlayerState> &players)
{
      m_rosterList->clear();
      for (const PlayerState &state : players)
      {
            m_rosterList->addItem(
                  tr("%1 — %2")
                        .arg(state.nickname.isEmpty() ? tr("Unnamed")
                                                      : state.nickname)
                        .arg(state.connected ? tr("connected")
                                             : tr("reserved")));
      }
}

void MultiplayerJoinScreen::updateStatus(const QString &status)
{
      m_statusLabel->setText(status);
}

void MultiplayerJoinScreen::showError(QString code, QString message)
{
      m_statusLabel->setText(tr("%1: %2").arg(code, message));
}
