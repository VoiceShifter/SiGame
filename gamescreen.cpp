#include "gamescreen.h"
#include "ui_gamescreen.h"

#include <QDebug>
#include <QDir>
#include <QScreen>
#include <QTimer>

#include <algorithm>

GameScreen::GameScreen(signed int PlayerCount, const QString &GamepackPath,
                       QWidget *parent)
      : QWidget(parent), ui(new Ui::GameScreen), m_tickTimer(new QTimer),
        m_globalTimer(new QElapsedTimer)
{
      ui->setupUi(this);

      const QString contentPath =
            GamepackPath.isEmpty()
                  ? QStringLiteral("content.xml")
                  : QDir(GamepackPath).filePath(QStringLiteral("content.xml"));
      QString parseError;
      if (!parseGameContent(contentPath, &m_game, &parseError))
      {
            qDebug() << "Failed to parse game content:" << parseError;
      }
      else
      {
            qDebug() << "Loaded game:" << m_game.name
                     << "rounds:" << m_game.rounds.size();
            printGameContent(m_game);
      }
      ui->splitter->setStretchFactor(0, 2);
      ui->splitter->setStretchFactor(1, 5);
      ui->splitter->widget(0)->setMaximumWidth(350);
      ui->crupie_photo->setScaledContents(true);
      ui->crupie_photo->setSizePolicy(QSizePolicy::Ignored,
                                      QSizePolicy::Ignored);
      ui->crupie_photo->setAlignment(Qt::AlignCenter);
      ui->JudgeLayout->setStretch(2, 1);
      ui->splitter->setChildrenCollapsible(false);
      ui->splitter->handle(1)->setEnabled(false);
      ui->splitter->handle(1)->setCursor(Qt::ArrowCursor);
      ui->PlayersLayout->setAlignment(Qt::AlignHCenter);
      QImage Image("Images/default.jpg");
      QPixmap pix                 = QPixmap::fromImage(Image);
      const QMargins judgeMargins = ui->JudgeLayout->contentsMargins();
      const QMargins photoMargins = ui->verticalLayout_2->contentsMargins();
      const int maximumPhotoSide  = ui->splitter->widget(0)->maximumWidth() -
                                   judgeMargins.left() - judgeMargins.right() -
                                   photoMargins.left() - photoMargins.right();
      ui->crupie_photo->setMaximumSize(maximumPhotoSide, maximumPhotoSide);
      ui->crupie_photo->setPixmap(pix);
      ui->tableWidget->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
      ui->tableWidget->horizontalHeader()->hide();
      ui->tableWidget->verticalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
      ui->tableWidget->verticalHeader()->setHighlightSections(false);
      ui->tableWidget->setSelectionMode(QAbstractItemView::NoSelection);
      QFont tableFont = ui->tableWidget->font();
      tableFont.setPixelSize(
            std::max(16, screen()->availableGeometry().height() / 45));
      ui->tableWidget->setFont(tableFont);
      ui->tableWidget->verticalHeader()->setFont(tableFont);
      ui->tableWidget->clearSelection();
      if (!m_game.rounds.empty())
      {
            const Round &round = m_game.rounds.front();
            std::size_t questionCount{0};
            for (const Theme &theme : round.themes)
            {
                  questionCount =
                        std::max(questionCount, theme.questions.size());
            }

            ui->tableWidget->setColumnCount(static_cast<int>(questionCount));
            ui->tableWidget->setRowCount(static_cast<int>(round.themes.size()));

            for (std::size_t row{0}; row < round.themes.size(); ++row)
            {
                  const Theme &theme = round.themes[row];
                  ui->tableWidget->setVerticalHeaderItem(
                        static_cast<int>(row),
                        new QTableWidgetItem(theme.name));

                  for (std::size_t column{0}; column < theme.questions.size();
                       ++column)
                  {
                        QPushButton *button = new QPushButton;
                        button->setFont(tableFont);
                        button->setText(
                              QString::number(theme.questions[column].price));
                        ui->tableWidget->setCellWidget(static_cast<int>(row),
                                                       static_cast<int>(column),
                                                       button);
                  }
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

            playerPfp->setPixmap(pix.scaled(200, 200, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
            playerPfp->setScaledContents(1);
            QLabel *playerName =
                  new QLabel("Player " + QString::number(Index + 1));
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
      connect(
            m_tickTimer, &QTimer::timeout, this,
            [this]()
            {
                  signed int value =
                        (m_globalTimeValue -
                         m_globalTimer->elapsed()); // / m_globalTimeValue * 100
                  if (value <= 0)
                  {
                        m_tickTimer->stop();
                        ui->progressBar->setValue(0);
                        qDebug() << "Timer ran out";
                  }
                  ui->progressBar->setValue(
                        int((value / float(m_globalTimeValue)) * 100));
            });
}
