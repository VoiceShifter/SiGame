#include "mainwindow.h"
#include "gamescreen.h"
#include "singleplayerscreen.h"
#include "ui_mainwindow.h"
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), stack(new QStackedWidget) {
  ui->setupUi(this);
  connect(ui->singleButton, &QPushButton::clicked, this,
          &MainWindow::loadSingleSettings);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::loadSingleSettings() {
  qDebug() << "loading single";
  singleScreen = new SinglePlayerScreen();
  stack->addWidget(singleScreen);
  setCentralWidget(stack);
  connect(singleScreen, &SinglePlayerScreen::SingleGameStarted, this,
          &MainWindow::loadSingleGame);
}

void MainWindow::loadSingleGame(int PlayersCount) {
  qDebug() << "loading single game";
  qDebug() << PlayersCount << " - players countrer";
  gameScreen = new GameScreen(PlayersCount);
  stack->addWidget(gameScreen);
  setCentralWidget(stack);
  stack->setCurrentWidget(gameScreen);
}

void MainWindow::loadSettings() {}

void MainWindow::loadMultiplayer() {}
