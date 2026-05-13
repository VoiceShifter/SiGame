#include "singleplayerscreen.h"
#include "ui_singleplayerscreen.h"
#include <QFileDialog>
SinglePlayerScreen::SinglePlayerScreen(QWidget *parent)
    : QWidget(parent), ui(new Ui::SinglePlayerScreen), GamepackPath{""} {
  ui->setupUi(this);
  connect(ui->horizontalSlider, &QSlider::sliderMoved, this, [this](int value) {
    ui->playerCount->setText(QString::number(value));
  });
  connect(
      ui->horizontalSlider, &QSlider::valueChanged, this,
      [this](int value) { ui->playerCount->setText(QString::number(value)); });
  connect(ui->pickPuckButton, &QPushButton::clicked, this,
          &SinglePlayerScreen::pickPack);
  connect(ui->createButton, &QPushButton::clicked, this,
          [this]() { emit SingleGameStarted(); });
}

SinglePlayerScreen::~SinglePlayerScreen() { delete ui; }

void SinglePlayerScreen::pickPack() {
  QString fileName = QFileDialog::getExistingDirectory(this, tr("Open pack"),
                                                       "/home/username");
  if (fileName.isEmpty()) {
    return;
  }
  GamepackPath = fileName;
  ui->pickPuckButton->setText(GamepackPath);
}

void SinglePlayerScreen::createGame() {
  if (GamepackPath.isEmpty()) {
    return;
  }
}
