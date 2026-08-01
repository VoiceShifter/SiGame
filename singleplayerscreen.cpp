#include "singleplayerscreen.h"
#include "ui_singleplayerscreen.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

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
      GamepackPath = fileName;
      ui->pickPuckButton->setText(GamepackPath);
}

void SinglePlayerScreen::createGame()
{
      if (GamepackPath.isEmpty())
      {
            return;
      }
      emit SingleGameStarted(ui->playerCount->text().toInt(), GamepackPath);
}
