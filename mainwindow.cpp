#include "mainwindow.h"
#include "gamescreen.h"
#include "singleplayerscreen.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QShortcut>

MainWindow::MainWindow(QWidget *parent)
      : QMainWindow(parent), ui(new Ui::MainWindow), stack(new QStackedWidget),
        exitPopup(new QMessageBox(this))
{
      ui->setupUi(this);
      exitPopup->setWindowTitle(tr("Exit Game"));
      exitPopup->setText(tr("Are you sure you want to exit? Any unsaved "
                            "changes will be lost."));
      exitPopup->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      exitPopup->setDefaultButton(QMessageBox::No);

      auto *quitShortcut = new QShortcut(QKeySequence("Ctrl+Q"), this);
      connect(quitShortcut, &QShortcut::activated, this,
              &MainWindow::showExitPopup);
      connect(ui->singleButton, &QPushButton::clicked, this,
              &MainWindow::loadSingleSettings);
      connect(ui->exitButton, &QPushButton::clicked, this,
              &MainWindow::showExitPopup);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::loadSingleSettings()
{
      qDebug() << "loading single";
      singleScreen = new SinglePlayerScreen();
      stack->addWidget(singleScreen);
      setCentralWidget(stack);
      connect(singleScreen, &SinglePlayerScreen::SingleGameStarted, this,
              &MainWindow::loadSingleGame);
}

void MainWindow::loadSingleGame(int PlayersCount, const QString &GamepackPath,
                                const QString &ProfilePicturePath,
                                int answerDuration, int questionDuration,
                                int questionPickDuration,
                                int answerWaitDuration)
{
      qDebug() << "loading single game";
      qDebug() << PlayersCount << " - players countrer";
      gameScreen = new GameScreen(PlayersCount, GamepackPath,
                                  ProfilePicturePath, answerDuration,
                                  questionDuration, questionPickDuration,
                                  answerWaitDuration);
      stack->addWidget(gameScreen);
      setCentralWidget(stack);
      stack->setCurrentWidget(gameScreen);
}

void MainWindow::loadSettings() {}

void MainWindow::loadMultiplayer() {}

void MainWindow::showExitPopup()
{
      if (exitPopup->exec() == QMessageBox::Yes)
            close();
}
