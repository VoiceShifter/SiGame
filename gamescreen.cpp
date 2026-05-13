#include "gamescreen.h"
#include "ui_gamescreen.h"

GameScreen::GameScreen(QWidget *parent)
    : QWidget(parent), ui(new Ui::GameScreen) {
  ui->setupUi(this);
  ui->splitter->setStretchFactor(0, 1);
  ui->splitter->setStretchFactor(1, 5);
  ui->splitter->setChildrenCollapsible(false);
  ui->splitter->handle(1)->setEnabled(false);
  ui->splitter->handle(1)->setCursor(Qt::ArrowCursor);
}

GameScreen::~GameScreen() { delete ui; }
