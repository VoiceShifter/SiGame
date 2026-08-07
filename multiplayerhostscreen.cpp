#include "multiplayerhostscreen.h"

#include "packmanifest.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

MultiplayerHostScreen::MultiplayerHostScreen(QWidget *parent)
      : QWidget(parent), m_identity(PlayerIdentity::load())
{
      auto *layout = new QVBoxLayout(this);
      auto *form = new QFormLayout;
      QSettings settings(QStringLiteral("SiGame"), QStringLiteral("SiGame"));
      m_packPath =
            settings.value(QStringLiteral("multiplayer/packPath"),
                           settings.value(QStringLiteral("join/packPath")))
                  .toString();
      if (isValidPackDirectory(m_packPath))
      {
            m_packPath = QDir(m_packPath).absolutePath();
      }
      else
      {
            m_packPath.clear();
      }
      m_packEdit = new QLineEdit(m_packPath, this);
      m_packEdit->setReadOnly(true);
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
      m_maxPlayersSpin = new QSpinBox(this);
      m_maxPlayersSpin->setRange(1, 5);
      m_maxPlayersSpin->setValue(2);
      form->addRow(tr("Maximum players:"), m_maxPlayersSpin);
      m_answerDurationSpin = new QSpinBox(this);
      m_questionDurationSpin = new QSpinBox(this);
      m_pickDurationSpin = new QSpinBox(this);
      m_waitDurationSpin = new QSpinBox(this);
      for (QSpinBox *spin : {m_answerDurationSpin, m_questionDurationSpin,
                             m_pickDurationSpin, m_waitDurationSpin})
      {
            spin->setRange(5, 60);
            spin->setValue(5);
      }
      m_pickDurationSpin->setValue(15);
      form->addRow(tr("Answer duration (seconds):"), m_answerDurationSpin);
      form->addRow(tr("Question duration (seconds):"), m_questionDurationSpin);
      form->addRow(tr("Question pick duration (seconds):"), m_pickDurationSpin);
      form->addRow(tr("Answer wait duration (seconds):"), m_waitDurationSpin);
      layout->addLayout(form);
      m_hashLabel = new QLabel(this);
      m_statusLabel = new QLabel(this);
      if (m_packPath.isEmpty())
      {
            m_statusLabel->setText(tr("Choose a valid pack to create a lobby."));
      }
      else
      {
            QString error;
            const QString hash = packManifestHash(m_packPath, &error);
            m_hashLabel->setText(
                  hash.isEmpty() ? tr("Pack hash: unavailable")
                                 : tr("Pack hash: %1").arg(hash));
            m_statusLabel->setText(
                  hash.isEmpty() ? error : tr("Cached pack is ready."));
      }
      m_rosterList = new QListWidget(this);
      m_hostButton = new QPushButton(tr("Start hosting"), this);
      layout->addWidget(m_hashLabel);
      layout->addWidget(m_statusLabel);
      layout->addWidget(m_rosterList);
      layout->addWidget(m_hostButton);

      connect(packButton, &QPushButton::clicked, this,
              &MultiplayerHostScreen::choosePack);
      connect(profileButton, &QPushButton::clicked, this,
              &MultiplayerHostScreen::chooseProfile);
      connect(m_hostButton, &QPushButton::clicked, this,
              &MultiplayerHostScreen::hostOrStart);
}

void MultiplayerHostScreen::choosePack()
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
      QSettings settings(QStringLiteral("SiGame"), QStringLiteral("SiGame"));
      settings.setValue(QStringLiteral("multiplayer/packPath"), m_packPath);
      settings.sync();
      QString error;
      const QString hash = packManifestHash(m_packPath, &error);
      m_hashLabel->setText(hash.isEmpty() ? tr("Pack hash: unavailable")
                                          : tr("Pack hash: %1").arg(hash));
      if (hash.isEmpty())
      {
            m_statusLabel->setText(error);
      }
}

void MultiplayerHostScreen::chooseProfile()
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

bool MultiplayerHostScreen::createHost()
{
      if (m_packPath.isEmpty())
      {
            m_statusLabel->setText(tr("Select a valid pack first."));
            return false;
      }
      QString manifestError;
      const QString hash = packManifestHash(m_packPath, &manifestError);
      if (hash.isEmpty())
      {
            m_statusLabel->setText(manifestError);
            return false;
      }
      Game game;
      QString parseError;
      if (!parseGameContent(QDir(m_packPath).filePath(QStringLiteral("content.xml")),
                            &game, &parseError))
      {
            m_statusLabel->setText(parseError);
            return false;
      }
      m_identity.nickname = m_nicknameEdit->text();
      m_identity.save();
      const GameConfig config = gameConfigFromSeconds(
            m_packPath, hash, m_maxPlayersSpin->value(),
            m_answerDurationSpin->value(), m_questionDurationSpin->value(),
            m_pickDurationSpin->value(), m_waitDurationSpin->value());
      m_host = new MultiplayerHost(config, game, m_identity, this);
      connect(m_host, &MultiplayerHost::rosterChanged, this,
              &MultiplayerHostScreen::updateRoster);
      connect(m_host, &MultiplayerHost::listeningStarted, this,
              &MultiplayerHostScreen::showListening);
      connect(m_host, &MultiplayerHost::listeningFailed, this,
              &MultiplayerHostScreen::showError);
      connect(m_host, &MultiplayerHost::protocolError, this,
              [this](const PlayerId &, const QString &, const QString &message)
              { m_statusLabel->setText(message); });
      connect(m_host->session(), &GameSession::phaseStarted, this,
              [this](const PhaseState &)
              {
                    if (m_host != nullptr && m_host->isStarted() &&
                        !m_gameStartedEmitted)
                    {
                          m_gameStartedEmitted = true;
                          emit gameStarted(m_host, m_packPath);
                          m_hostButton->setEnabled(false);
                    }
              });
      if (!m_host->listen(MultiplayerProtocol::DefaultPort))
      {
            m_host->deleteLater();
            m_host = nullptr;
            return false;
      }
      m_hostButton->setText(tr("Start game"));
      m_statusLabel->setText(tr("Lobby is listening."));
      if (m_maxPlayersSpin->value() == 1)
      {
            m_host->startGame();
      }
      return true;
}

void MultiplayerHostScreen::hostOrStart()
{
      if (m_host == nullptr)
      {
            createHost();
            return;
      }
      m_host->startGame();
      if (m_host->isStarted() && !m_gameStartedEmitted)
      {
            m_gameStartedEmitted = true;
            emit gameStarted(m_host, m_packPath);
            m_hostButton->setEnabled(false);
      }
}

void MultiplayerHostScreen::updateRoster(const QVector<PlayerState> &players)
{
      const bool locked = players.size() > 1;
      m_packEdit->setEnabled(!locked);
      m_nicknameEdit->setEnabled(!locked);
      m_maxPlayersSpin->setEnabled(!locked);
      m_answerDurationSpin->setEnabled(!locked);
      m_questionDurationSpin->setEnabled(!locked);
      m_pickDurationSpin->setEnabled(!locked);
      m_waitDurationSpin->setEnabled(!locked);
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

void MultiplayerHostScreen::showListening(quint16 port,
                                          const QStringList &addresses)
{
      m_statusLabel->setText(
            tr("Listening on %1:%2").arg(addresses.join(tr(", "))).arg(port));
}

void MultiplayerHostScreen::showError(const QString &error)
{
      m_statusLabel->setText(error);
}
