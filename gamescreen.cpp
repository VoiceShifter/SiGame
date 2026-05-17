#include "gamescreen.h"
#include "ui_gamescreen.h"

#include <QTimer>

GameScreen::GameScreen(signed int PlayerCount, QWidget *parent)
      : QWidget(parent), ui(new Ui::GameScreen), m_tickTimer(new QTimer), m_globalTimer(new QElapsedTimer) {
      ui->setupUi(this);
      ui->splitter->setStretchFactor(0, 2);
      ui->splitter->setStretchFactor(1, 5);
      ui->splitter->setChildrenCollapsible(false);
      ui->splitter->handle(1)->setEnabled(false);
      ui->splitter->handle(1)->setCursor(Qt::ArrowCursor);
      ui->PlayersLayout->setAlignment(Qt::AlignHCenter);
      QImage Image("Images/default.jpg");
      QPixmap pix = QPixmap::fromImage(Image);
      ui->crupie_photo->setPixmap(pix.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      ui->tableWidget->setColumnCount(4);
      ui->tableWidget->setRowCount(4);
      ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      ui->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      for (uint Row{0}; Row < 4; ++Row)
      {
            for (uint Column{0}; Column < 4; ++Column)
            {
                  QPushButton *Button = new QPushButton;
                  Button->setText(QString::number((Column+1)*100));
                  ui->tableWidget->setCellWidget(Row, Column, Button);
            }
      }

      for (uint Index{0}; Index < PlayerCount; ++Index)
      {
            QVBoxLayout *playerLayout = new QVBoxLayout;
            if (Image.isNull())
            {
                  qDebug() << "Image not loaded";
                  delete playerLayout;
                  continue;
            }

            QLabel *playerPfp = new QLabel;

            playerPfp->setPixmap(pix.scaled(200, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            playerPfp->setScaledContents(1);
            QLabel *playerName = new QLabel("Player " + QString::number(Index + 1));
            playerLayout->addWidget(playerPfp);
            playerLayout->addWidget(playerName);
            playerLayout->setStretch(0, 0);
            ui->PlayersLayout->addLayout(playerLayout);
      }
      StartTimer();
}

GameScreen::~GameScreen() { delete ui; }

void GameScreen::StartTimer()
{
      m_globalTimer->start();
      m_tickTimer->start(250);
      connect(m_tickTimer, &QTimer::timeout, this, [this](){
            signed int value = (m_globalTimeValue - m_globalTimer->elapsed()); // / m_globalTimeValue * 100
            if (value <= 0)
            {
                  m_tickTimer->stop();
                  ui->progressBar->setValue(0);
                  qDebug() << "Timer ran out";
            }
            ui->progressBar->setValue(int((value / float(m_globalTimeValue)) * 100));
      });
}
