#include "mainwindow.h"
#include "gamescreen.h"
#include "singleplayerscreen.h"
#include "ui_mainwindow.h"
#include <QAbstractButton>
#include <QMessageBox>
#include <QShortcut>

#ifdef Q_OS_WASM
#include <emscripten.h>

EM_JS(void, closeSiGameBrowserPage, (), {
      try {
            window.close();
      } catch (error) {
            console.debug('The browser did not allow the SiGame tab to close',
                          error);
      }
      if (!window.closed)
            window.location.replace('about:blank');
});
#endif

MainWindow::MainWindow(QWidget *parent)
      : QMainWindow(parent), ui(new Ui::MainWindow), stack(new QStackedWidget),
        exitPopup(new QMessageBox(this))
{
      ui->setupUi(this);
      mainScreen = takeCentralWidget();
      stack->addWidget(mainScreen);
      setCentralWidget(stack);
      stack->setCurrentWidget(mainScreen);

      exitPopup->setWindowTitle(tr("Exit Game"));
      exitPopup->setText(tr("Are you sure you want to exit? Any unsaved "
                            "changes will be lost."));
      exitPopup->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      exitPopup->setDefaultButton(QMessageBox::No);
      connect(exitPopup, &QMessageBox::buttonClicked, this,
              [this](QAbstractButton *button)
              {
                    if (exitPopup->standardButton(button) == QMessageBox::Yes)
                    {
                          exitApplication();
                    }
              });

      auto *quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
      connect(quitShortcut, &QShortcut::activated, this,
              &MainWindow::showExitPopup);
      auto *backShortcut =
            new QShortcut(QKeySequence(Qt::Key_Escape), this);
      connect(backShortcut, &QShortcut::activated, this,
              [this]()
              {
                    const QWidget *current = stack->currentWidget();
                    if (current == singleScreen || current == hostScreen ||
                        current == joinScreen || current == validatorScreen)
                    {
                          returnToMainMenu();
                    }
              });
      connect(ui->joinButton, &QPushButton::clicked, this,
              &MainWindow::loadJoinSettings);
      connect(ui->singleButton, &QPushButton::clicked, this,
              &MainWindow::loadSingleSettings);
      connect(ui->hostButton, &QPushButton::clicked, this,
              &MainWindow::loadMultiplayer);
      connect(ui->validateButton, &QPushButton::clicked, this,
              &MainWindow::loadPackValidator);
      connect(ui->exitButton, &QPushButton::clicked, this,
              &MainWindow::showExitPopup);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::loadSingleSettings()
{
      qDebug() << "loading single";
      singleScreen = new SinglePlayerScreen();
      stack->addWidget(singleScreen);
      stack->setCurrentWidget(singleScreen);
      connect(singleScreen, &SinglePlayerScreen::SingleGameStarted, this,
              &MainWindow::loadSingleGame);
      connect(singleScreen, &SinglePlayerScreen::cancelled, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadSingleGame(int PlayersCount, const QString &GamepackPath,
                                const QString &ProfilePicturePath,
                                const QString &Nickname, int answerDuration,
                                int questionDuration,
                                int questionPickDuration,
                                int answerWaitDuration)
{
      qDebug() << "loading single game";
      qDebug() << PlayersCount << " - players countrer";
      gameScreen = new GameScreen(PlayersCount, GamepackPath,
                                  ProfilePicturePath, Nickname, answerDuration,
                                  questionDuration, questionPickDuration,
                                  answerWaitDuration);
      stack->addWidget(gameScreen);
      stack->setCurrentWidget(gameScreen);
      connect(gameScreen, &GameScreen::returnToMenuRequested, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadSettings() {}

void MainWindow::loadMultiplayer()
{
      hostScreen = new MultiplayerHostScreen(this);
      stack->addWidget(hostScreen);
      stack->setCurrentWidget(hostScreen);
      connect(hostScreen, &MultiplayerHostScreen::gameStarted, this,
              &MainWindow::loadHostGame);
      connect(hostScreen, &MultiplayerHostScreen::cancelled, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadJoinSettings()
{
      joinScreen = new MultiplayerJoinScreen(this);
      stack->addWidget(joinScreen);
      stack->setCurrentWidget(joinScreen);
      connect(joinScreen, &MultiplayerJoinScreen::gameStarted, this,
              &MainWindow::loadClientGame);
      connect(joinScreen, &MultiplayerJoinScreen::cancelled, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadPackValidator()
{
      validatorScreen = new PackValidatorScreen(this);
      stack->addWidget(validatorScreen);
      stack->setCurrentWidget(validatorScreen);
      connect(validatorScreen, &PackValidatorScreen::cancelled, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadHostGame(MultiplayerHost *host, const QString &packPath)
{
      if (host == nullptr)
      {
            return;
      }
      const GameConfig &config = host->config();
      gameScreen = new GameScreen(
            1, packPath, QString(), QString(),
            static_cast<int>(config.answerDurationMs / 1000U),
            static_cast<int>(config.questionDurationMs / 1000U),
            static_cast<int>(config.questionPickDurationMs / 1000U),
            static_cast<int>(config.answerWaitDurationMs / 1000U),
            GameScreenMode::MultiplayerHost);
      stack->addWidget(gameScreen);
      stack->setCurrentWidget(gameScreen);
      gameScreen->bindHost(host);
      connect(gameScreen, &GameScreen::returnToMenuRequested, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::loadClientGame(MultiplayerClient *client,
                                const QString &packPath)
{
      if (client == nullptr)
      {
            return;
      }
      const GameConfig &config = client->config();
      gameScreen = new GameScreen(
            1, packPath, QString(), QString(),
            static_cast<int>(config.answerDurationMs / 1000U),
            static_cast<int>(config.questionDurationMs / 1000U),
            static_cast<int>(config.questionPickDurationMs / 1000U),
            static_cast<int>(config.answerWaitDurationMs / 1000U),
            GameScreenMode::MultiplayerClient);
      stack->addWidget(gameScreen);
      stack->setCurrentWidget(gameScreen);
      gameScreen->bindClient(client);
      connect(gameScreen, &GameScreen::returnToMenuRequested, this,
              &MainWindow::returnToMainMenu);
}

void MainWindow::returnToMainMenu()
{
      QWidget *settingsScreen = stack->currentWidget();
      if (settingsScreen == gameScreen)
      {
            if (hostScreen != nullptr && hostScreen->host() != nullptr)
            {
                  hostScreen->host()->stop();
            }
            if (joinScreen != nullptr && joinScreen->client() != nullptr)
            {
                  joinScreen->client()->disconnectFromHost();
            }
            stack->setCurrentWidget(mainScreen);
            stack->removeWidget(gameScreen);
            gameScreen->deleteLater();
            gameScreen = nullptr;
            if (singleScreen != nullptr)
            {
                  stack->removeWidget(singleScreen);
                  singleScreen->deleteLater();
                  singleScreen = nullptr;
            }
            if (hostScreen != nullptr)
            {
                  stack->removeWidget(hostScreen);
                  hostScreen->deleteLater();
                  hostScreen = nullptr;
            }
            if (joinScreen != nullptr)
            {
                  stack->removeWidget(joinScreen);
                  joinScreen->deleteLater();
                  joinScreen = nullptr;
            }
            return;
      }
      if (settingsScreen == hostScreen)
      {
            if (hostScreen->host() != nullptr)
            {
                  hostScreen->host()->stop();
            }
      }
      else if (settingsScreen == joinScreen)
      {
            if (joinScreen->client() != nullptr)
            {
                  joinScreen->client()->disconnectFromHost();
            }
      }
      else if (settingsScreen != singleScreen &&
               settingsScreen != validatorScreen)
      {
            return;
      }

      stack->setCurrentWidget(mainScreen);
      stack->removeWidget(settingsScreen);
      if (settingsScreen == singleScreen)
      {
            singleScreen = nullptr;
      }
      else if (settingsScreen == hostScreen)
      {
            hostScreen = nullptr;
      }
      else if (settingsScreen == joinScreen)
      {
            joinScreen = nullptr;
      }
      else if (settingsScreen == validatorScreen)
      {
            validatorScreen = nullptr;
      }
      settingsScreen->deleteLater();
}

void MainWindow::showExitPopup()
{
      if (!exitPopup->isVisible())
      {
            exitPopup->open();
      }
}

void MainWindow::exitApplication()
{
      if (hostScreen != nullptr && hostScreen->host() != nullptr)
      {
            hostScreen->host()->stop();
      }
      if (joinScreen != nullptr && joinScreen->client() != nullptr)
      {
            joinScreen->client()->disconnectFromHost();
      }
#ifdef Q_OS_WASM
      hide();
      closeSiGameBrowserPage();
#else
      close();
#endif
}
