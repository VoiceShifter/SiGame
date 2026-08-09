#include "gamescreen.h"
#include "multiplayerclient.h"
#include "multiplayerhost.h"
#include "ui_gamescreen.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioOutput>
#endif
#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QDialog>
#include <QDir>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVideoWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace
{
unsigned int mediaDurationMilliseconds(std::size_t seconds)
{
      const std::size_t maximumSeconds =
            std::numeric_limits<unsigned int>::max() / 1000U;
      return static_cast<unsigned int>(std::min(seconds, maximumSeconds) *
                                       1000U);
}
} // namespace

GameScreen::GameScreen(signed int PlayerCount, const QString &GamepackPath,
                       const QString &ProfilePicturePath,
                       const QString &Nickname, int AnswerDuration,
                       int QuestionDuration, int QuestionPickDuration,
                       int AnswerWaitDuration, QWidget *parent)
      : GameScreen(PlayerCount, GamepackPath, ProfilePicturePath, Nickname,
                   AnswerDuration, QuestionDuration, QuestionPickDuration,
                   AnswerWaitDuration, GameScreenMode::SinglePlayer, parent)
{
}

GameScreen::GameScreen(signed int PlayerCount, const QString &GamepackPath,
                       const QString &ProfilePicturePath,
                       const QString &Nickname, int AnswerDuration,
                       int QuestionDuration,
                       int QuestionPickDuration, int AnswerWaitDuration,
                       GameScreenMode mode, QWidget *parent)
      : QWidget(parent), ui(new Ui::GameScreen), m_tickTimer(new QTimer(this)),
        m_globalTimer(new QElapsedTimer),
        m_progressAnimation(new QPropertyAnimation(this)),
        m_flashTimer(new QTimer(this)),
        m_mediaDurationTimer(new QTimer(this)),
        m_answerDuration(static_cast<unsigned int>(AnswerDuration) * 1000U),
        m_questionDuration(static_cast<unsigned int>(QuestionDuration) * 1000U),
        m_questionPickDuration(
              static_cast<unsigned int>(QuestionPickDuration) * 1000U),
        m_answerWaitDuration(
              static_cast<unsigned int>(AnswerWaitDuration) * 1000U)
{
      m_mode = mode;
      ui->setupUi(this);
      m_boardStatusLabel = new QLabel(ui->boardPage);
      m_boardStatusLabel->setAlignment(Qt::AlignCenter);
      QFont boardStatusFont = m_boardStatusLabel->font();
      boardStatusFont.setBold(true);
      m_boardStatusLabel->setFont(boardStatusFont);
      ui->boardPageLayout->insertWidget(0, m_boardStatusLabel);
      m_mediaPlayer = new QMediaPlayer(this);
      m_mediaPlayer->setObjectName(QStringLiteral("questionMediaPlayer"));
      m_videoWidget = new QVideoWidget(ui->questionFrame);
      m_videoWidget->setObjectName(QStringLiteral("questionVideoWidget"));
      m_videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
      m_videoWidget->setSizePolicy(QSizePolicy::Ignored,
                                   QSizePolicy::Expanding);
      m_videoWidget->hide();
      ui->questionFrameLayout->addWidget(m_videoWidget);
      m_mediaPlayer->setVideoOutput(m_videoWidget);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      m_audioOutput = new QAudioOutput(this);
      m_audioOutput->setVolume(1.0F);
      m_mediaPlayer->setAudioOutput(m_audioOutput);
      connect(m_mediaPlayer, &QMediaPlayer::errorOccurred, this,
              [](QMediaPlayer::Error, const QString &message)
              { qWarning() << "Unable to play media:" << message; });
#else
      connect(m_mediaPlayer,
              QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
              [this](QMediaPlayer::Error)
              { qWarning() << "Unable to play media:"
                           << m_mediaPlayer->errorString(); });
#endif
      m_mediaDurationTimer->setSingleShot(true);
      connect(m_mediaDurationTimer, &QTimer::timeout, this,
              &GameScreen::finishMediaDisplay);
      setupAppealPage();
      ui->progressBar->setRange(0, 1000);
      m_progressAnimation->setTargetObject(ui->progressBar);
      m_progressAnimation->setPropertyName("value");
      m_progressAnimation->setEasingCurve(QEasingCurve::Linear);
      ui->answerOptionsTable->horizontalHeader()->hide();
      ui->answerOptionsTable->verticalHeader()->hide();
      ui->answerOptionsTable->setFocusPolicy(Qt::NoFocus);
      ui->answerOptionsTable->setCurrentCell(-1, -1);
      ui->answerOptionsTable->clearSelection();
      ui->answerOptionsTable->setShowGrid(true);
      ui->answerOptionsTable->setStyleSheet(QStringLiteral(
            "QTableWidget { background: transparent; "
            "border: 2px solid #6c63ff; gridline-color: #6c63ff; }"));
      connect(ui->answerOptionsTable, &QTableWidget::cellClicked, this,
              [this](int row, int column)
              {
                    if ((m_phase != GamePhase::Answering &&
                         m_phase != GamePhase::ForAllAnswering) ||
                        (m_mode == GameScreenMode::SinglePlayer
                              ? m_answerResultApplied
                              : m_networkAnswerSubmitted) ||
                        ((m_mode != GameScreenMode::SinglePlayer &&
                          !m_networkQuestion.has_value()) ||
                         currentQuestion().answerType != AnswerType::Select))
                    {
                          return;
                    }
                    QTableWidgetItem *item =
                          ui->answerOptionsTable->item(row, column);
                    if (item == nullptr)
                    {
                          return;
                    }
                    ui->answerOptionsTable->viewport()->setAttribute(
                          Qt::WA_TransparentForMouseEvents, true);
                    ui->answerOptionsTable->setCurrentCell(-1, -1);
                    ui->answerOptionsTable->clearSelection();
                    handleSubmittedAnswer(
                          item->data(Qt::UserRole).toString());
              });

      m_gamepackPath = GamepackPath.isEmpty()
                              ? QDir::currentPath()
                              : QDir(GamepackPath).absolutePath();
      const QString contentPath =
            QDir(m_gamepackPath).filePath(QStringLiteral("content.xml"));
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

      connect(m_tickTimer, &QTimer::timeout, this,
              &GameScreen::updateTimerProgress);
      connect(this, &GameScreen::questionSelected, this,
              &GameScreen::showQuestion);
      connect(ui->AnswerBytton, &QPushButton::clicked, this,
              [this]()
              {
                    if (m_mode != GameScreenMode::SinglePlayer)
                    {
                          if (m_phase == GamePhase::AppealVoting)
                          {
                                submitAppealVote(true);
                                return;
                          }
                          if (m_phase == GamePhase::WaitingForReaction)
                          {
                                ui->AnswerBytton->setEnabled(false);
                                ui->passButton->setEnabled(false);
                                m_networkReactionClaimed = true;
                                stopReactionFlash();
                                const unsigned int elapsed =
                                      m_reactionElapsedTimer.isValid()
                                            ? static_cast<unsigned int>(
                                                  std::max<qint64>(
                                                        0,
                                                        m_reactionElapsedTimer
                                                              .elapsed()))
                                            : 0U;
                                emit reactionClaimRequested(elapsed);
                          }
                          else if ((m_phase == GamePhase::Answering ||
                                    m_phase == GamePhase::ForAllAnswering) &&
                                   m_canAnswer)
                          {
                                ui->AnswerBytton->setEnabled(false);
                                m_networkAnswerInputOpened = true;
                                openAnswerInput();
                          }
                          return;
                    }
                    if (m_phase != GamePhase::WaitingForReaction)
                    {
                          return;
                    }

                    ui->AnswerBytton->setEnabled(false);
                    ui->passButton->setEnabled(false);
                    stopReactionFlash();
                    setSinglePlayerGlow(PlayerGlow::Reaction, false);
                    const Question &question = currentQuestion();
                    m_answerResultApplied = false;
                    const unsigned int duration =
                          question.answerDuration > 0
                                ? static_cast<unsigned int>(
                                        question.answerDuration) *
                                        1000U
                                : m_answerDuration;
                    startPhaseTimer(GamePhase::Answering, duration);
                    openAnswerInput();
              });
      connect(ui->passButton, &QPushButton::clicked, this,
              [this]()
              {
                    if (m_mode != GameScreenMode::SinglePlayer)
                    {
                          if (m_phase == GamePhase::AppealVoting)
                          {
                                submitAppealVote(false);
                                return;
                          }
                          ui->passButton->setEnabled(false);
                          emit passRequested();
                          return;
                    }
                    if (m_players.empty() || m_currentThemeIndex < 0)
                    {
                          return;
                    }
                    if (m_singleFinalQuestionActive &&
                        m_phase == GamePhase::ForAllAnswering)
                    {
                          finishSingleFinalAnswer(false);
                          return;
                    }
                    m_players.front().hasPassed = true;
                    ui->passButton->setEnabled(false);
                    const bool allPlayersPassed = std::all_of(
                          m_players.cbegin(), m_players.cend(),
                          [](const Player &player)
                          { return player.hasPassed; });
                    if (allPlayersPassed)
                    {
                          showAnswer();
                    }
              });
      connect(ui->pushButton_3, &QPushButton::clicked, this,
              [this]()
              {
                    if (m_mode == GameScreenMode::SinglePlayer)
                    {
                          pauseSinglePlayer();
                    }
                    else if (m_canPause)
                    {
                          emit pauseRequested(true);
                    }
              });
      connect(ui->pushButton_5, &QPushButton::clicked, this,
              [this]()
              {
                    if (m_mode == GameScreenMode::SinglePlayer)
                    {
                          resumeSinglePlayer();
                    }
                    else if (m_networkPaused)
                    {
                          emit pauseRequested(false);
                    }
              });
      connect(ui->appealButton, &QPushButton::clicked, this,
              [this]() { emit appealRequested(); });

      m_questionFrameStyleSheet =
            ui->questionFrame->styleSheet() +
            QStringLiteral(
                  "\nQFrame#questionFrame { border: 5px solid transparent; }");
      ui->questionFrame->setStyleSheet(m_questionFrameStyleSheet);
      connect(m_flashTimer, &QTimer::timeout, this,
              [this]()
              {
                    ++m_flashStep;
                    if (m_flashStep >= 6)
                    {
                          stopReactionFlash();
                          return;
                    }
                    ui->questionFrame->setStyleSheet(
                          m_flashStep % 2 == 0
                                ? m_questionFrameStyleSheet +
                                        QStringLiteral(
                                              "\nQFrame#questionFrame { border: "
                                              "5px solid yellow; }")
                                : m_questionFrameStyleSheet);
              });

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
      QImage Image(QStringLiteral(":/Images/default.jpg"));
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
      buildBoard(0);

      for (uint Index{0}; Index < PlayerCount; ++Index)
      {
            QVBoxLayout *playerLayout = new QVBoxLayout;
            QPixmap playerPixmap = pix;
            if (Index == 0 && !ProfilePicturePath.isEmpty())
            {
                  const QPixmap selectedPicture(ProfilePicturePath);
                  if (!selectedPicture.isNull())
                  {
                        playerPixmap = selectedPicture;
                  }
                  else
                  {
                        qWarning() << "Unable to load profile picture:"
                                   << ProfilePicturePath;
                  }
            }
            if (playerPixmap.isNull())
            {
                  qDebug() << "Image not loaded";
                  delete playerLayout;
                  continue;
            }

            QLabel *playerPfp = new QLabel;

            playerPfp->setPixmap(
                  playerPixmap.scaled(200, 200, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
            playerPfp->setScaledContents(1);
            applyPlayerGlow(playerPfp, PlayerGlow::None);
            const QString playerDisplayName =
                  Index == 0 && !Nickname.isEmpty()
                        ? Nickname
                        : tr("Player %1").arg(Index + 1);
            QLabel *playerName = new QLabel(playerDisplayName);
            QLabel *balanceLabel = new QLabel;
            QLabel *answerBubble = createAnswerBubble();
            playerLayout->addWidget(playerPfp);
            playerLayout->addWidget(playerName);
            playerLayout->addWidget(balanceLabel);
            playerLayout->setStretch(0, 0);
            ui->PlayersLayout->addLayout(playerLayout);
            m_players.push_back({playerDisplayName, 0, false, playerPfp,
                                 balanceLabel, answerBubble});
            updateBalanceLabel(m_players.back());
      }

      ui->questionMediaLabel->installEventFilter(this);
      ui->answerOptionsContainer->installEventFilter(this);
      ui->questionMediaLabel->hide();
      ui->answerOptionsTable->hide();
      ui->answerOptionsContainer->hide();
      ui->passButton->setEnabled(false);
      ui->appealButton->hide();
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            ui->pushButton_3->setEnabled(true);
            ui->pushButton_5->setEnabled(false);
            if (m_game.rounds.empty())
            {
                  returnToBoard();
            }
            else
            {
                  skipToRound(static_cast<int>(m_game.rounds.size()) - 1);
            }
      }
      else
      {
            m_phase = SessionPhase::Lobby;
            m_tickTimer->stop();
            ui->gameContentStack->setCurrentWidget(ui->boardPage);
      }
}

GameScreen::~GameScreen()
{
      delete m_globalTimer;
      delete ui;
}

void GameScreen::buildBoard(int roundIndex)
{
      ui->tableWidget->setRowCount(0);
      ui->tableWidget->setColumnCount(0);
      m_boardRoundIndex = roundIndex;
      if (roundIndex < 0 ||
          roundIndex >= static_cast<int>(m_game.rounds.size()))
      {
            return;
      }

      const Round &round =
            m_game.rounds[static_cast<std::size_t>(roundIndex)];
      m_boardStatusLabel->setText(round.name);
      std::size_t questionCount{};
      for (const Theme &theme : round.themes)
      {
            questionCount = std::max(questionCount, theme.questions.size());
      }
      ui->tableWidget->setColumnCount(static_cast<int>(questionCount));
      ui->tableWidget->setRowCount(static_cast<int>(round.themes.size()));
      const QFont tableFont = ui->tableWidget->font();
      const bool finalRound = isFinalRound(roundIndex);

      for (std::size_t row = 0; row < round.themes.size(); ++row)
      {
            const Theme &theme = round.themes[row];
            ui->tableWidget->setVerticalHeaderItem(
                  static_cast<int>(row), new QTableWidgetItem(theme.name));
            for (std::size_t column = 0; column < theme.questions.size();
                 ++column)
            {
                  auto *button = new QPushButton;
                  button->setFont(tableFont);
                  button->setText(finalRound
                                        ? tr("Eliminate")
                                        : QString::number(
                                                theme.questions[column].price));
                  const int themeIndex = static_cast<int>(row);
                  const int questionIndex = static_cast<int>(column);
                  connect(button, &QPushButton::clicked, this,
                          [this, roundIndex, themeIndex, questionIndex]()
                          {
                                if (m_mode == GameScreenMode::SinglePlayer)
                                {
                                      emit questionSelected(themeIndex,
                                                            questionIndex);
                                }
                                else
                                {
                                      emit questionPickRequested(
                                            roundIndex, themeIndex,
                                            questionIndex);
                                }
                          });
                  if (m_mode != GameScreenMode::SinglePlayer)
                  {
                        button->setEnabled(false);
                  }
                  ui->tableWidget->setCellWidget(themeIndex, questionIndex,
                                                 button);
            }
      }
}

bool GameScreen::isFinalRound(int roundIndex) const
{
      return roundIndex >= 0 &&
             roundIndex < static_cast<int>(m_game.rounds.size()) &&
             m_game.rounds[static_cast<std::size_t>(roundIndex)]
                         .type.compare(QStringLiteral("final"),
                                       Qt::CaseInsensitive) == 0;
}

bool GameScreen::skipToRound(int roundIndex)
{
      if (m_mode != GameScreenMode::SinglePlayer || roundIndex < 0 ||
          roundIndex >= static_cast<int>(m_game.rounds.size()))
      {
            return false;
      }

      m_tickTimer->stop();
      m_progressAnimation->stop();
      stopReactionFlash();
      stopMediaPlayback();
      resetAnswerInputState();
      m_singlePlayerPaused = false;
      m_singlePlayerTimerWasActive = false;
      m_singlePlayerFlashWasActive = false;
      m_pageBeforePause.clear();
      m_singleFinalQuestionActive = false;
      m_singleFinalEliminatorIndex = 0;
      m_singleFinalWagers.clear();
      m_singleFinalCorrect.clear();
      m_forAllAnswering = false;
      m_answerResultApplied = false;
      m_currentThemeIndex = -1;
      m_currentQuestionIndex = -1;
      m_displayedPixmap = {};
      ui->questionTextLabel->clear();
      ui->questionMediaLabel->clear();
      ui->questionMediaLabel->hide();
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(false);
      ui->pushButton_3->setEnabled(true);
      ui->pushButton_5->setEnabled(false);
      for (Player &player : m_players)
      {
            player.hasPassed = false;
      }

      buildBoard(roundIndex);
      ui->gameContentStack->setCurrentWidget(ui->boardPage);
      if (!hasAvailableQuestions())
      {
            advanceSinglePlayerRound();
            return true;
      }
      if (isFinalRound(roundIndex) && !m_players.empty())
      {
            m_boardStatusLabel->setText(
                  tr("%1 eliminates a topic").arg(m_players.front().name));
            if (beginSingleFinalWagersIfReady())
            {
                  return true;
            }
      }
      startPhaseTimer(GamePhase::PickingQuestion, m_questionPickDuration);
      return true;
}

void GameScreen::setupAppealPage()
{
      m_appealPage = new QWidget(ui->gameContentStack);
      m_appealPage->setObjectName(QStringLiteral("appealPage"));
      auto *layout = new QVBoxLayout(m_appealPage);
      layout->setContentsMargins(24, 24, 24, 24);
      layout->setSpacing(12);

      auto *title = new QLabel(tr("Appeal in progress"), m_appealPage);
      title->setObjectName(QStringLiteral("appealTitleLabel"));
      QFont titleFont = title->font();
      titleFont.setPointSize(22);
      titleFont.setBold(true);
      title->setFont(titleFont);
      title->setAlignment(Qt::AlignCenter);
      title->setStyleSheet(QStringLiteral(
            "padding: 10px; border: 2px solid #6c63ff; border-radius: 6px;"));
      layout->addWidget(title);

      auto addHeading = [this, layout](const QString &text)
      {
            auto *heading = new QLabel(text, m_appealPage);
            QFont font = heading->font();
            font.setBold(true);
            heading->setFont(font);
            layout->addWidget(heading);
            return heading;
      };
      addHeading(tr("Question"));
      m_appealQuestionLabel = new QLabel(m_appealPage);
      m_appealQuestionLabel->setObjectName(
            QStringLiteral("appealQuestionLabel"));
      m_appealQuestionLabel->setAlignment(Qt::AlignCenter);
      m_appealQuestionLabel->setWordWrap(true);
      layout->addWidget(m_appealQuestionLabel);

      m_appealQuestionMediaLabel = new QLabel(m_appealPage);
      m_appealQuestionMediaLabel->setObjectName(
            QStringLiteral("appealQuestionMediaLabel"));
      m_appealQuestionMediaLabel->setAlignment(Qt::AlignCenter);
      m_appealQuestionMediaLabel->setMinimumHeight(100);
      m_appealQuestionMediaLabel->setSizePolicy(QSizePolicy::Expanding,
                                                QSizePolicy::Expanding);
      m_appealQuestionMediaLabel->hide();
      layout->addWidget(m_appealQuestionMediaLabel, 1);

      m_appealSubmittedHeadingLabel = addHeading(tr("Player answer"));
      m_appealSubmittedHeadingLabel->setObjectName(
            QStringLiteral("appealSubmittedHeadingLabel"));
      m_appealSubmittedLabel = new QLabel(m_appealPage);
      m_appealSubmittedLabel->setObjectName(
            QStringLiteral("appealSubmittedLabel"));
      m_appealSubmittedLabel->setWordWrap(true);
      m_appealSubmittedLabel->setStyleSheet(
            QStringLiteral("padding: 8px; border: 1px solid #c62828;"));
      layout->addWidget(m_appealSubmittedLabel);

      addHeading(tr("Correct answer"));
      m_appealCorrectAnswerLabel = new QLabel(m_appealPage);
      m_appealCorrectAnswerLabel->setObjectName(
            QStringLiteral("appealCorrectAnswerLabel"));
      m_appealCorrectAnswerLabel->setWordWrap(true);
      m_appealCorrectAnswerLabel->setStyleSheet(
            QStringLiteral("padding: 8px; border: 1px solid #2e7d32;"));
      layout->addWidget(m_appealCorrectAnswerLabel);

      m_appealCorrectAnswerMediaLabel = new QLabel(m_appealPage);
      m_appealCorrectAnswerMediaLabel->setObjectName(
            QStringLiteral("appealCorrectAnswerMediaLabel"));
      m_appealCorrectAnswerMediaLabel->setAlignment(Qt::AlignCenter);
      m_appealCorrectAnswerMediaLabel->setMinimumHeight(100);
      m_appealCorrectAnswerMediaLabel->setSizePolicy(QSizePolicy::Expanding,
                                                     QSizePolicy::Expanding);
      m_appealCorrectAnswerMediaLabel->hide();
      layout->addWidget(m_appealCorrectAnswerMediaLabel, 1);
      ui->gameContentStack->addWidget(m_appealPage);
}

void GameScreen::fitAppealPixmaps()
{
      const auto fit = [](const QPixmap &source, QLabel *label)
      {
            if (source.isNull() || label == nullptr || !label->isVisible())
            {
                  return;
            }
            label->setPixmap(source.scaled(label->contentsRect().size(),
                                            Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
      };
      fit(m_appealQuestionPixmap, m_appealQuestionMediaLabel);
      fit(m_appealCorrectAnswerPixmap, m_appealCorrectAnswerMediaLabel);
}

QString GameScreen::displayAnswerText(const QString &answer) const
{
      const Question *question = nullptr;
      if (m_mode == GameScreenMode::SinglePlayer && m_currentThemeIndex >= 0 &&
          m_currentQuestionIndex >= 0)
      {
            question = &currentQuestion();
      }
      else if (m_networkQuestion.has_value())
      {
            question = &*m_networkQuestion;
      }
      if (question == nullptr || question->answerType != AnswerType::Select)
      {
            return answer;
      }
      const auto option = std::find_if(
            question->answerOptions.cbegin(), question->answerOptions.cend(),
            [&answer](const AnswerOption &candidate)
            {
                  return candidate.id.compare(answer.trimmed(),
                                              Qt::CaseInsensitive) == 0;
            });
      return option == question->answerOptions.cend()
                   ? answer
                   : QStringLiteral("%1 — %2").arg(option->id, option->text);
}

void GameScreen::submitAppealVote(bool accepted)
{
      if (m_phase != SessionPhase::AppealVoting || m_appealId == 0 ||
          m_appealAppellant == m_localPlayerId || m_appealVoteSubmitted ||
          m_networkPaused)
      {
            return;
      }
      m_appealVoteSubmitted = true;
      setNetworkControls();
      emit appealVoteSubmitted(accepted);
}

bool GameScreen::eventFilter(QObject *watched, QEvent *event)
{
      if (watched == ui->answerOptionsContainer &&
          event->type() == QEvent::Resize)
      {
            fitAnswerOptionsTable();
      }
      if (event->type() == QEvent::MouseButtonPress)
      {
            const PlayerId targetId =
                  watched->property("secretTargetId").toString();
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const auto target = std::find_if(
                  m_networkPlayers.cbegin(), m_networkPlayers.cend(),
                  [&targetId](const PlayerState &state)
                  { return state.id == targetId; });
            if (!targetId.isEmpty() &&
                mouseEvent->button() == Qt::LeftButton &&
                m_secretTargetSelection && targetId != m_localPlayerId &&
                target != m_networkPlayers.cend() && target->isPicker)
            {
                  emit secretTargetRequested(targetId);
                  return true;
            }
      }
      if (watched == ui->questionMediaLabel &&
          event->type() == QEvent::MouseButtonPress &&
          (m_phase == GamePhase::Answering ||
           m_phase == GamePhase::ForAllAnswering) &&
          m_pointInputEnabled && !m_displayedPixmapRect.isEmpty())
      {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint clickPosition = mouseEvent->position().toPoint();
            if (mouseEvent->button() == Qt::LeftButton &&
                m_displayedPixmapRect.contains(clickPosition))
            {
                  const double x = std::clamp(
                        (clickPosition.x() - m_displayedPixmapRect.left()) /
                              static_cast<double>(
                                    m_displayedPixmapRect.width()),
                        0.0, 1.0);
                  const double y = std::clamp(
                        (clickPosition.y() - m_displayedPixmapRect.top()) /
                              static_cast<double>(
                                    m_displayedPixmapRect.height()),
                        0.0, 1.0);
                  m_submittedPoint = QPointF(x, y);
                  m_pointInputEnabled = false;
                  ui->questionMediaLabel->setCursor(Qt::ArrowCursor);
                  fitDisplayedPixmap();
                  const QString submittedAnswer =
                        QStringLiteral("%1,%2")
                              .arg(x, 0, 'f', 6)
                              .arg(y, 0, 'f', 6);
                  if (m_mode != GameScreenMode::SinglePlayer)
                  {
                        AnswerSubmission submission;
                        submission.answerType = AnswerType::Point;
                        submission.point = QPointF(x, y);
                        submission.hasPoint = true;
                        submission.mode = m_forAllAnswering
                                                ? QStringLiteral("ForAll")
                                                : (m_networkQuestion.has_value() &&
                                                           m_networkQuestion->type ==
                                                                 QuestionType::SecretPublicPrice
                                                         ? QStringLiteral("Secret")
                                                         : QString());
                        m_networkAnswerSubmitted = true;
                        emitNetworkAnswer(submission);
                  }
                  else
                  {
                        if (!m_correctPoint.has_value())
                        {
                              return true;
                        }
                        const double dx =
                              (x - m_correctPoint->x()) *
                              m_correctPointAspectRatio;
                        const double dy = y - m_correctPoint->y();
                        const double allowedDeviation =
                              std::max(0.02, currentQuestion().answerDeviation);
                        const bool isCorrect =
                              std::hypot(dx, dy) <= allowedDeviation;
                        if (m_singleFinalQuestionActive &&
                            m_phase == GamePhase::ForAllAnswering)
                        {
                              finishSingleFinalAnswer(isCorrect);
                        }
                        else
                        {
                              applyAnswerResult(isCorrect, submittedAnswer);
                        }
                  }
                  return true;
            }
      }
      return QWidget::eventFilter(watched, event);
}

void GameScreen::resizeEvent(QResizeEvent *event)
{
      QWidget::resizeEvent(event);
      fitDisplayedPixmap();
      fitAnswerOptionsTable();
      fitAppealPixmaps();
      QTimer::singleShot(0, this, &GameScreen::positionAnswerBubbles);
}

void GameScreen::startPhaseTimer(GamePhase phase, unsigned int durationMs)
{
      if (phase != GamePhase::WaitingForReaction)
      {
            stopReactionFlash();
      }
      m_phase         = phase;
      m_phaseDuration = durationMs;
      setProgressBarColor(phase);
      m_globalTimer->restart();
      m_progressAnimation->stop();
      m_progressAnimation->setStartValue(ui->progressBar->maximum());
      m_progressAnimation->setEndValue(ui->progressBar->minimum());
      m_progressAnimation->setDuration(static_cast<int>(durationMs));
      m_progressAnimation->start();
      m_tickTimer->start(250);
}

void GameScreen::setProgressBarColor(GamePhase phase)
{
      QString color;
      switch (phase)
      {
      case GamePhase::PickingQuestion:
      case GamePhase::ReadingQuestion:
      case GamePhase::SecretTargetSelection:
      case GamePhase::Lobby:
      case GamePhase::Finished:
            color = QStringLiteral("#2196f3");
            break;
      case GamePhase::WaitingForReaction:
            color = QStringLiteral("#ffeb3b");
            break;
      case GamePhase::Answering:
      case GamePhase::ForAllAnswering:
      case GamePhase::ShowingAnswer:
      case GamePhase::SecretWager:
      case GamePhase::FinalWager:
            color = QStringLiteral("#9c27b0");
            break;
      case GamePhase::AppealVoting:
            color = QStringLiteral("#f44336");
            break;
      }
      ui->progressBar->setStyleSheet(
            QStringLiteral("QProgressBar::chunk { background-color: %1; }")
                  .arg(color));
}

void GameScreen::updateTimerProgress()
{
      if (m_mode != GameScreenMode::SinglePlayer)
      {
            m_tickTimer->stop();
            m_progressAnimation->stop();
            ui->progressBar->setValue(ui->progressBar->minimum());
            setNetworkControls();
            return;
      }
      if (m_singlePlayerPaused)
      {
            return;
      }
      const qint64 remaining = std::max<qint64>(
            0, static_cast<qint64>(m_phaseDuration) -
                     m_globalTimer->elapsed());
      if (remaining == 0)
      {
            m_tickTimer->stop();
            m_progressAnimation->stop();
            ui->progressBar->setValue(ui->progressBar->minimum());
            handlePhaseTimeout();
      }
}

void GameScreen::handlePhaseTimeout()
{
      switch (m_phase)
      {
      case GamePhase::PickingQuestion:
            pickRandomQuestion();
            break;
      case GamePhase::ReadingQuestion:
            finishMediaDisplay();
            if (m_singleFinalQuestionActive)
            {
                  beginSingleFinalAnswers();
            }
            else
            {
                  startReactionFlash();
                  ui->AnswerBytton->setEnabled(true);
                  startPhaseTimer(GamePhase::WaitingForReaction,
                                  m_answerWaitDuration);
            }
            break;
      case GamePhase::WaitingForReaction:
      case GamePhase::Answering:
            showAnswer();
            break;
      case GamePhase::ShowingAnswer:
            returnToBoard();
            break;
      case GamePhase::ForAllAnswering:
            if (m_singleFinalQuestionActive)
            {
                  const QString answer =
                        m_answerDialog == nullptr
                              ? QString()
                              : m_answerDialog->textValue();
                  const QString normalized = answer.trimmed();
                  const bool correct = std::any_of(
                        currentQuestion().rightAnswers.cbegin(),
                        currentQuestion().rightAnswers.cend(),
                        [&normalized](const QString &rightAnswer)
                        {
                              return normalized.compare(
                                           rightAnswer.trimmed(),
                                           Qt::CaseInsensitive) == 0;
                        });
                  finishSingleFinalAnswer(correct);
            }
            break;
      case GamePhase::FinalWager:
            submitSingleFinalWager(m_answerDialog == nullptr
                                         ? 0
                                         : m_answerDialog->intValue());
            break;
      case GamePhase::Lobby:
      case GamePhase::SecretTargetSelection:
      case GamePhase::SecretWager:
      case GamePhase::AppealVoting:
      case GamePhase::Finished:
            break;
      }
}

void GameScreen::pauseSinglePlayer()
{
      if (m_mode != GameScreenMode::SinglePlayer || m_singlePlayerPaused)
      {
            return;
      }

      m_singlePlayerPaused = true;
      m_singlePlayerTimerWasActive = m_tickTimer->isActive();
      m_singlePlayerRemainingMs =
            m_singlePlayerTimerWasActive
                  ? static_cast<unsigned int>(std::max<qint64>(
                          0, static_cast<qint64>(m_phaseDuration) -
                                   m_globalTimer->elapsed()))
                  : 0U;
      m_pageBeforePause = ui->gameContentStack->currentWidget();
      m_singlePlayerFlashWasActive = m_flashTimer->isActive();
      m_singlePlayerAnswerWasEnabled = ui->AnswerBytton->isEnabled();
      m_singlePlayerPassWasEnabled = ui->passButton->isEnabled();
      m_singlePlayerAnswerDialogWasVisible =
            m_answerDialog != nullptr && m_answerDialog->isVisible();

      m_tickTimer->stop();
      if (m_progressAnimation->state() == QAbstractAnimation::Running)
      {
            m_progressAnimation->pause();
      }
      if (m_singlePlayerFlashWasActive)
      {
            m_flashTimer->stop();
      }
      if (m_singlePlayerAnswerDialogWasVisible)
      {
            m_answerDialog->hide();
      }
      pauseMediaPlayback();
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(false);
      ui->pushButton_3->setEnabled(false);
      ui->pushButton_5->setEnabled(true);
      ui->gameContentStack->setCurrentWidget(ui->pausePage);
}

void GameScreen::resumeSinglePlayer()
{
      if (m_mode != GameScreenMode::SinglePlayer || !m_singlePlayerPaused)
      {
            return;
      }

      m_singlePlayerPaused = false;
      if (m_pageBeforePause != nullptr)
      {
            ui->gameContentStack->setCurrentWidget(m_pageBeforePause);
      }
      resumeMediaPlayback();
      if (m_singlePlayerAnswerDialogWasVisible && m_answerDialog != nullptr)
      {
            m_answerDialog->show();
            m_answerDialog->raise();
            m_answerDialog->activateWindow();
      }
      if (m_singlePlayerFlashWasActive &&
          m_phase == GamePhase::WaitingForReaction)
      {
            m_flashTimer->start(120);
      }
      ui->AnswerBytton->setEnabled(m_singlePlayerAnswerWasEnabled);
      ui->passButton->setEnabled(m_singlePlayerPassWasEnabled);
      ui->pushButton_3->setEnabled(true);
      ui->pushButton_5->setEnabled(false);

      const bool restartTimer = m_singlePlayerTimerWasActive;
      const unsigned int remainingMs = m_singlePlayerRemainingMs;
      m_singlePlayerTimerWasActive = false;
      m_singlePlayerFlashWasActive = false;
      m_singlePlayerAnswerDialogWasVisible = false;
      m_pageBeforePause.clear();

      if (!restartTimer)
      {
            return;
      }
      m_phaseDuration = remainingMs;
      m_globalTimer->restart();
      if (m_progressAnimation->state() == QAbstractAnimation::Paused)
      {
            m_progressAnimation->resume();
      }
      if (remainingMs > 0)
      {
            m_tickTimer->start(250);
      }
      else
      {
            m_progressAnimation->stop();
            ui->progressBar->setValue(ui->progressBar->minimum());
            handlePhaseTimeout();
      }
}

void GameScreen::showQuestion(int themeIndex, int questionIndex)
{
      if (m_phase != GamePhase::PickingQuestion || m_game.rounds.empty() ||
          themeIndex < 0 || questionIndex < 0)
      {
            return;
      }

      if (m_boardRoundIndex < 0 ||
          m_boardRoundIndex >= static_cast<int>(m_game.rounds.size()))
      {
            return;
      }
      const Round &round =
            m_game.rounds[static_cast<std::size_t>(m_boardRoundIndex)];
      if (static_cast<std::size_t>(themeIndex) >= round.themes.size())
      {
            return;
      }
      const Theme &theme = round.themes[static_cast<std::size_t>(themeIndex)];
      if (static_cast<std::size_t>(questionIndex) >= theme.questions.size())
      {
            return;
      }

      QPushButton *button = qobject_cast<QPushButton *>(
            ui->tableWidget->cellWidget(themeIndex, questionIndex));
      if (button == nullptr || !button->isEnabled())
      {
            return;
      }
      if (m_mode == GameScreenMode::SinglePlayer &&
          isFinalRound(m_boardRoundIndex) &&
          !m_singleFinalQuestionActive)
      {
            eliminateSingleFinalTheme(themeIndex);
            return;
      }

      const Question &question =
            theme.questions[static_cast<std::size_t>(questionIndex)];
      button->setText(QString());
      button->setEnabled(false);
      m_currentThemeIndex    = themeIndex;
      m_currentQuestionIndex = questionIndex;

      switch (question.type)
      {
      case QuestionType::ForAll:
            emit forAllQuestionSelected(m_boardRoundIndex, themeIndex,
                                        questionIndex);
            break;
      case QuestionType::SecretPublicPrice:
            if (question.secretParameters.has_value())
            {
                  const SecretQuestionParameters &parameters =
                        *question.secretParameters;
                  emit secretPublicPriceQuestionSelected(
                        m_boardRoundIndex, themeIndex, questionIndex,
                        parameters.selectionMode, parameters.price.minimum,
                        parameters.price.maximum, parameters.price.step,
                        parameters.theme);
            }
            else
            {
                  qWarning() << "Secret public price question has no metadata";
                  emit secretPublicPriceQuestionSelected(
                        m_boardRoundIndex, themeIndex, questionIndex, QString(),
                        0, 0, 0, QString());
            }
            break;
      case QuestionType::Default:
            break;
      case QuestionType::Unknown:
            qWarning() << "Unknown question type";
            break;
      }

      resetAnswerInputState();
      const unsigned int mediaDuration =
            mediaDurationMilliseconds(question.mediaDuration);
      displayContent(question.text, question.mediaType, question.mediaPath,
                     mediaDuration);
      if (question.answerType == AnswerType::Select)
      {
            if (question.answerOptions.size() >= 2)
            {
                  populateAnswerOptions(question);
            }
            else
            {
                  qWarning() << "Select question has fewer than two options";
            }
      }
      else if (question.answerType == AnswerType::Point)
      {
            for (const QString &answer : question.rightAnswers)
            {
                  QPointF point;
                  double aspectRatio{1.0};
                  if (parsePointAnswer(answer, &point, &aspectRatio))
                  {
                        m_correctPoint = point;
                        m_correctPointAspectRatio = aspectRatio;
                        break;
                  }
            }
            if (!m_correctPoint.has_value())
            {
                  qWarning() << "Point question has no coordinate answer";
            }
            if (question.mediaType != MediaType::Image ||
                m_displayedPixmap.isNull())
            {
                  qWarning() << "Point question has no valid image";
            }
      }
      for (Player &player : m_players)
      {
            player.hasPassed = false;
      }
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(!m_players.empty());
      ui->gameContentStack->setCurrentWidget(ui->questionPage);
      QTimer::singleShot(0, this, &GameScreen::fitDisplayedPixmap);
      startPhaseTimer(GamePhase::ReadingQuestion,
                      std::max(m_questionDuration, mediaDuration));
}

void GameScreen::showAnswer()
{
      finishMediaDisplay();
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(false);
      const Question &question = currentQuestion();
      if (m_phase == GamePhase::Answering && !m_answerResultApplied &&
          (question.answerType != AnswerType::Point ||
           (m_correctPoint.has_value() && !m_displayedPixmap.isNull())))
      {
            if (m_answerDialog != nullptr)
            {
                  handleSubmittedAnswer(m_answerDialog->textValue());
                  if (m_phase == GamePhase::ShowingAnswer)
                  {
                        return;
                  }
            }
            else
            {
                  applyAnswerResult(false, QString());
            }
      }
      if (m_answerDialog != nullptr && m_answerDialog->isVisible())
      {
            m_answerDialog->reject();
      }
      m_pointInputEnabled = false;
      ui->answerOptionsTable->viewport()->setAttribute(
            Qt::WA_TransparentForMouseEvents, true);
      ui->questionMediaLabel->setCursor(Qt::ArrowCursor);
      stopReactionFlash();

      QStringList answers;
      switch (question.answerType)
      {
      case AnswerType::Text:
            for (const QString &answer : question.rightAnswers)
            {
                  answers.push_back(answer);
            }
            break;
      case AnswerType::Select:
            for (const QString &rightAnswer : question.rightAnswers)
            {
                  const auto option = std::find_if(
                        question.answerOptions.cbegin(),
                        question.answerOptions.cend(),
                        [&rightAnswer](const AnswerOption &candidate)
                        {
                              return candidate.id.compare(
                                           rightAnswer.trimmed(),
                                           Qt::CaseInsensitive) == 0;
                        });
                  answers.push_back(
                        option == question.answerOptions.cend()
                              ? rightAnswer
                              : QStringLiteral("%1 — %2")
                                      .arg(option->id, option->text));
            }
            highlightSelectAnswers(question);
            break;
      case AnswerType::Point:
            for (const QString &answer : question.rightAnswers)
            {
                  QPointF point;
                  double aspectRatio;
                  if (!parsePointAnswer(answer, &point, &aspectRatio))
                  {
                        answers.push_back(answer);
                  }
            }
            break;
      case AnswerType::Unknown:
            qWarning() << "Unknown answer type; revealing raw answers";
            for (const QString &answer : question.rightAnswers)
            {
                  answers.push_back(answer);
            }
            break;
      }

      const unsigned int answerMediaDuration =
            mediaDurationMilliseconds(question.answerMediaDuration);
      displayContent(answers.join(QLatin1Char('\n')),
                     question.answerMediaType, question.answerMediaPath,
                     answerMediaDuration);
      startPhaseTimer(GamePhase::ShowingAnswer,
                      std::max(AnswerRevealDuration,
                               answerMediaDuration));
}

void GameScreen::returnToBoard()
{
      stopReactionFlash();
      stopMediaPlayback();
      clearAnswerBubbles();
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(false);
      resetAnswerInputState();
      ui->questionTextLabel->clear();
      ui->questionMediaLabel->clear();
      ui->questionMediaLabel->hide();
      m_displayedPixmap = {};
      m_currentThemeIndex = -1;
      m_currentQuestionIndex = -1;
      ui->gameContentStack->setCurrentWidget(ui->boardPage);
      if (m_mode == GameScreenMode::SinglePlayer &&
          !hasAvailableQuestions())
      {
            advanceSinglePlayerRound();
            return;
      }
      if (m_mode == GameScreenMode::SinglePlayer &&
          isFinalRound(m_boardRoundIndex) &&
          beginSingleFinalWagersIfReady())
      {
            return;
      }
      startPhaseTimer(GamePhase::PickingQuestion, m_questionPickDuration);
}

bool GameScreen::hasAvailableQuestions() const
{
      for (int row = 0; row < ui->tableWidget->rowCount(); ++row)
      {
            for (int column = 0; column < ui->tableWidget->columnCount();
                 ++column)
            {
                  const auto *button = qobject_cast<QPushButton *>(
                        ui->tableWidget->cellWidget(row, column));
                  if (button != nullptr && button->isEnabled())
                  {
                        return true;
                  }
            }
      }
      return false;
}

void GameScreen::advanceSinglePlayerRound()
{
      m_singleFinalQuestionActive = false;
      m_singleFinalWagers.clear();
      m_singleFinalCorrect.clear();
      int nextRound = m_boardRoundIndex + 1;
      while (nextRound < static_cast<int>(m_game.rounds.size()))
      {
            buildBoard(nextRound);
            if (ui->tableWidget->rowCount() > 0 &&
                ui->tableWidget->columnCount() > 0)
            {
                  m_singleFinalEliminatorIndex = 0;
                  if (isFinalRound(nextRound) && !m_players.empty())
                  {
                        m_boardStatusLabel->setText(
                              tr("%1 eliminates a topic")
                                    .arg(m_players.front().name));
                  }
                  ui->gameContentStack->setCurrentWidget(ui->boardPage);
                  if (isFinalRound(nextRound) &&
                      beginSingleFinalWagersIfReady())
                  {
                        return;
                  }
                  startPhaseTimer(GamePhase::PickingQuestion,
                                  m_questionPickDuration);
                  return;
            }
            ++nextRound;
      }
      m_tickTimer->stop();
      m_progressAnimation->stop();
      m_phase = GamePhase::Finished;
      ui->progressBar->setValue(ui->progressBar->minimum());
      ui->AnswerBytton->setEnabled(false);
      ui->passButton->setEnabled(false);
      ui->gameContentStack->setCurrentWidget(ui->boardPage);
}

void GameScreen::eliminateSingleFinalTheme(int themeIndex)
{
      if (!isFinalRound(m_boardRoundIndex) || themeIndex < 0 ||
          themeIndex >= ui->tableWidget->rowCount())
      {
            return;
      }
      for (int question = 0; question < ui->tableWidget->columnCount();
           ++question)
      {
            if (auto *button = qobject_cast<QPushButton *>(
                      ui->tableWidget->cellWidget(themeIndex, question)))
            {
                  button->setEnabled(false);
                  button->setText(QString());
            }
      }

      if (beginSingleFinalWagersIfReady())
      {
            return;
      }
      if (!hasAvailableQuestions())
      {
            advanceSinglePlayerRound();
            return;
      }
      if (!m_players.empty())
      {
            m_singleFinalEliminatorIndex =
                  (m_singleFinalEliminatorIndex + 1) %
                  static_cast<int>(m_players.size());
            m_boardStatusLabel->setText(
                  tr("%1 eliminates a topic")
                        .arg(m_players[static_cast<std::size_t>(
                              m_singleFinalEliminatorIndex)]
                                   .name));
      }
      startPhaseTimer(GamePhase::PickingQuestion, m_questionPickDuration);
}

bool GameScreen::beginSingleFinalWagersIfReady()
{
      int remainingThemes = 0;
      int finalTheme = -1;
      int finalQuestion = -1;
      for (int theme = 0; theme < ui->tableWidget->rowCount(); ++theme)
      {
            for (int question = 0;
                 question < ui->tableWidget->columnCount(); ++question)
            {
                  const auto *button = qobject_cast<QPushButton *>(
                        ui->tableWidget->cellWidget(theme, question));
                  if (button != nullptr && button->isEnabled())
                  {
                        ++remainingThemes;
                        finalTheme = theme;
                        finalQuestion = question;
                        break;
                  }
            }
      }
      if (remainingThemes != 1)
      {
            return false;
      }
      beginSingleFinalWagers(finalTheme, finalQuestion);
      return true;
}

void GameScreen::beginSingleFinalWagers(int themeIndex, int questionIndex)
{
      if (m_players.empty())
      {
            advanceSinglePlayerRound();
            return;
      }
      m_currentThemeIndex = themeIndex;
      m_currentQuestionIndex = questionIndex;
      if (auto *button = qobject_cast<QPushButton *>(
                ui->tableWidget->cellWidget(themeIndex, questionIndex)))
      {
            button->setEnabled(false);
      }
      m_singleFinalWagerPlayerIndex = 0;
      m_singleFinalWagers.assign(m_players.size(), 0);
      m_singleFinalCorrect.assign(m_players.size(), false);
      const Round &round =
            m_game.rounds[static_cast<std::size_t>(m_boardRoundIndex)];
      m_boardStatusLabel->setText(
            tr("Final topic: %1")
                  .arg(round.themes[static_cast<std::size_t>(themeIndex)].name));
      promptSingleFinalWager();
}

void GameScreen::promptSingleFinalWager()
{
      if (m_singleFinalWagerPlayerIndex >=
          static_cast<int>(m_players.size()))
      {
            showSingleFinalQuestion();
            return;
      }
      startPhaseTimer(GamePhase::FinalWager, m_answerDuration);
      m_answerDialog = new QInputDialog(this);
      m_answerDialog->setAttribute(Qt::WA_DeleteOnClose);
      m_answerDialog->setInputMode(QInputDialog::IntInput);
      m_answerDialog->setWindowTitle(tr("Final wager"));
      const Player &player =
            m_players[static_cast<std::size_t>(m_singleFinalWagerPlayerIndex)];
      const int maximum =
            singlePlayerWagerLimit(m_singleFinalWagerPlayerIndex);
      m_answerDialog->setLabelText(
            tr("%1, choose a wager (0–%2):").arg(player.name).arg(maximum));
      m_answerDialog->setIntRange(0, maximum);
      m_answerDialog->setIntValue(0);
      connect(m_answerDialog, &QInputDialog::intValueSelected, this,
              &GameScreen::submitSingleFinalWager);
      connect(m_answerDialog, &QDialog::rejected, this,
              [this]() { submitSingleFinalWager(0); });
      m_answerDialog->open();
}

void GameScreen::submitSingleFinalWager(int amount)
{
      if (m_phase != GamePhase::FinalWager ||
          m_singleFinalWagerPlayerIndex < 0 ||
          m_singleFinalWagerPlayerIndex >=
                static_cast<int>(m_players.size()))
      {
            return;
      }
      const int maximum =
            singlePlayerWagerLimit(m_singleFinalWagerPlayerIndex);
      m_singleFinalWagers[static_cast<std::size_t>(
            m_singleFinalWagerPlayerIndex)] = std::clamp(amount, 0, maximum);
      if (m_answerDialog != nullptr)
      {
            disconnect(m_answerDialog, nullptr, this, nullptr);
            m_answerDialog->close();
            m_answerDialog = nullptr;
      }
      ++m_singleFinalWagerPlayerIndex;
      QTimer::singleShot(0, this, &GameScreen::promptSingleFinalWager);
}

void GameScreen::showSingleFinalQuestion()
{
      m_singleFinalQuestionActive = true;
      m_phase = GamePhase::PickingQuestion;
      if (auto *button = qobject_cast<QPushButton *>(
                ui->tableWidget->cellWidget(m_currentThemeIndex,
                                             m_currentQuestionIndex)))
      {
            button->setEnabled(true);
      }
      showQuestion(m_currentThemeIndex, m_currentQuestionIndex);
}

void GameScreen::beginSingleFinalAnswers()
{
      if (m_players.empty())
      {
            showAnswer();
            return;
      }
      m_singleFinalAnswerPlayerIndex = 0;
      m_forAllAnswering = true;
      promptSingleFinalAnswer();
}

void GameScreen::promptSingleFinalAnswer()
{
      if (m_singleFinalAnswerPlayerIndex >=
          static_cast<int>(m_players.size()))
      {
            for (std::size_t index = 0; index < m_players.size(); ++index)
            {
                  Player &player = m_players[index];
                  const int wager = m_singleFinalWagers[index];
                  player.balance +=
                        m_singleFinalCorrect[index] ? wager : -wager;
                  player.glow = m_singleFinalCorrect[index]
                                      ? PlayerGlow::Correct
                                      : PlayerGlow::Incorrect;
                  applyPlayerGlow(player.avatarLabel, player.glow);
                  updateBalanceLabel(player);
            }
            m_forAllAnswering = false;
            showAnswer();
            return;
      }
      m_answerResultApplied = false;
      m_submittedPoint.reset();
      ui->answerOptionsTable->setCurrentCell(-1, -1);
      ui->answerOptionsTable->clearSelection();
      const unsigned int duration =
            currentQuestion().answerDuration > 0
                  ? mediaDurationMilliseconds(currentQuestion().answerDuration)
                  : m_answerDuration;
      startPhaseTimer(GamePhase::ForAllAnswering, duration);
      openAnswerInput();
}

void GameScreen::finishSingleFinalAnswer(bool correct)
{
      if (!m_singleFinalQuestionActive ||
          m_phase != GamePhase::ForAllAnswering ||
          m_singleFinalAnswerPlayerIndex < 0 ||
          m_singleFinalAnswerPlayerIndex >=
                static_cast<int>(m_players.size()))
      {
            return;
      }
      const std::size_t index =
            static_cast<std::size_t>(m_singleFinalAnswerPlayerIndex);
      m_singleFinalCorrect[index] = correct;
      if (m_answerDialog != nullptr)
      {
            disconnect(m_answerDialog, nullptr, this, nullptr);
            m_answerDialog->close();
            m_answerDialog = nullptr;
      }
      m_pointInputEnabled = false;
      ui->answerOptionsTable->viewport()->setAttribute(
            Qt::WA_TransparentForMouseEvents, true);
      ++m_singleFinalAnswerPlayerIndex;
      QTimer::singleShot(0, this, &GameScreen::promptSingleFinalAnswer);
}

int GameScreen::singlePlayerWagerLimit(int playerIndex) const
{
      if (playerIndex < 0 ||
          playerIndex >= static_cast<int>(m_players.size()))
      {
            return 0;
      }
      const qint64 balance =
            m_players[static_cast<std::size_t>(playerIndex)].balance;
      const quint64 magnitude =
            balance < 0 ? static_cast<quint64>(-balance)
                        : static_cast<quint64>(balance);
      return static_cast<int>(std::min<quint64>(
            magnitude,
            static_cast<quint64>(std::numeric_limits<int>::max())));
}

const Question &GameScreen::currentQuestion() const
{
      if (m_mode != GameScreenMode::SinglePlayer &&
          m_networkQuestion.has_value())
      {
            return *m_networkQuestion;
      }
      return m_game.rounds[static_cast<std::size_t>(m_boardRoundIndex)]
            .themes[static_cast<std::size_t>(m_currentThemeIndex)]
            .questions[static_cast<std::size_t>(m_currentQuestionIndex)];
}

void GameScreen::pickRandomQuestion()
{
      std::vector<std::pair<int, int>> availableQuestions;
      if (m_boardRoundIndex >= 0 &&
          m_boardRoundIndex < static_cast<int>(m_game.rounds.size()))
      {
            const Round &round =
                  m_game.rounds[static_cast<std::size_t>(m_boardRoundIndex)];
            for (std::size_t themeIndex = 0;
                 themeIndex < round.themes.size(); ++themeIndex)
            {
                  for (std::size_t questionIndex = 0;
                       questionIndex < round.themes[themeIndex].questions.size();
                       ++questionIndex)
                  {
                        QPushButton *button = qobject_cast<QPushButton *>(
                              ui->tableWidget->cellWidget(
                                    static_cast<int>(themeIndex),
                                    static_cast<int>(questionIndex)));
                        if (button != nullptr && button->isEnabled())
                        {
                              availableQuestions.emplace_back(
                                    static_cast<int>(themeIndex),
                                    static_cast<int>(questionIndex));
                        }
                  }
            }
      }

      if (availableQuestions.empty())
      {
            advanceSinglePlayerRound();
            return;
      }

      const int selectedIndex = QRandomGenerator::global()->bounded(
            static_cast<int>(availableQuestions.size()));
      const auto [themeIndex, questionIndex] =
            availableQuestions[static_cast<std::size_t>(selectedIndex)];
      emit questionSelected(themeIndex, questionIndex);
}

void GameScreen::displayContent(const QString &text, MediaType mediaType,
                                const QString &mediaPath,
                                unsigned int mediaDurationMs)
{
      stopMediaPlayback();
      ui->questionTextLabel->setText(text);
      ui->questionMediaLabel->clear();
      m_videoWidget->hide();
      m_displayedPixmap = {};
      m_displayedPixmapRect = {};

      if (mediaType == MediaType::Image && !mediaPath.isEmpty())
      {
            const QString absolutePath =
                  QDir(m_gamepackPath).filePath(mediaPath);
            const QPixmap image(absolutePath);
            if (image.isNull())
            {
                  qWarning() << "Unable to load question image:"
                             << absolutePath;
            }
            else
            {
                  m_displayedPixmap = image;
                  m_activeMediaType = MediaType::Image;
            }
      }
      else if ((mediaType == MediaType::Audio ||
                mediaType == MediaType::Video) &&
               !mediaPath.isEmpty())
      {
            const QString absolutePath =
                  QDir(m_gamepackPath).filePath(mediaPath);
            if (!QFileInfo(absolutePath).isFile())
            {
                  qWarning() << "Unable to load question media:"
                             << absolutePath;
            }
            else
            {
                  m_activeMediaType = mediaType;
                  if (mediaType == MediaType::Video)
                  {
                        m_videoWidget->show();
                  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                  m_mediaPlayer->setSource(QUrl::fromLocalFile(absolutePath));
#else
                  m_mediaPlayer->setMedia(QUrl::fromLocalFile(absolutePath));
#endif
                  m_mediaPlayer->play();
            }
      }

      if (m_displayedPixmap.isNull())
      {
            ui->questionMediaLabel->hide();
            if (m_activeMediaType == MediaType::Video)
            {
                  ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                                       QSizePolicy::Maximum);
                  ui->questionTextLabel->setAlignment(Qt::AlignHCenter |
                                                      Qt::AlignTop);
            }
            else
            {
                  ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                                       QSizePolicy::Expanding);
                  ui->questionTextLabel->setAlignment(Qt::AlignCenter);
            }
      }
      else
      {
            ui->questionMediaLabel->show();
            ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                                 QSizePolicy::Maximum);
            ui->questionTextLabel->setAlignment(Qt::AlignHCenter |
                                                Qt::AlignTop);
            QTimer::singleShot(0, this, &GameScreen::fitDisplayedPixmap);
      }
      if (m_activeMediaType != MediaType::None && mediaDurationMs > 0)
      {
            m_mediaRemainingMs = mediaDurationMs;
            m_mediaDurationElapsedTimer.restart();
            m_mediaDurationTimer->start(static_cast<int>(std::min<unsigned int>(
                  mediaDurationMs,
                  static_cast<unsigned int>(
                        std::numeric_limits<int>::max()))));
      }
}

void GameScreen::finishMediaDisplay()
{
      const MediaType finishedType = m_activeMediaType;
      stopMediaPlayback();
      if (finishedType == MediaType::Image ||
          finishedType == MediaType::Video)
      {
            ui->questionMediaLabel->clear();
            ui->questionMediaLabel->hide();
            m_displayedPixmapRect = {};
            ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                                 QSizePolicy::Expanding);
            ui->questionTextLabel->setAlignment(Qt::AlignCenter);
      }
}

void GameScreen::stopMediaPlayback()
{
      m_mediaDurationTimer->stop();
      m_mediaDurationElapsedTimer.invalidate();
      m_mediaRemainingMs = 0;
      m_mediaDurationPaused = false;
      m_activeMediaType = MediaType::None;
      if (m_videoWidget != nullptr)
      {
            m_videoWidget->hide();
      }
      if (m_mediaPlayer == nullptr)
      {
            return;
      }
      m_mediaPausedByGame = false;
      m_mediaPlayer->stop();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      m_mediaPlayer->setSource({});
#endif
}

void GameScreen::pauseMediaPlayback()
{
      if (m_mediaDurationTimer->isActive() &&
          m_mediaDurationElapsedTimer.isValid())
      {
            const unsigned int elapsed = static_cast<unsigned int>(
                  std::max<qint64>(0, m_mediaDurationElapsedTimer.elapsed()));
            m_mediaRemainingMs =
                  elapsed >= m_mediaRemainingMs ? 0U
                                                : m_mediaRemainingMs - elapsed;
            m_mediaDurationTimer->stop();
            m_mediaDurationElapsedTimer.invalidate();
            m_mediaDurationPaused = m_mediaRemainingMs > 0;
      }
      if (m_mediaPlayer == nullptr)
      {
            return;
      }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      const QMediaPlayer::MediaStatus status = m_mediaPlayer->mediaStatus();
      m_mediaPausedByGame = !m_mediaPlayer->source().isEmpty() &&
                            status != QMediaPlayer::NoMedia &&
                            status != QMediaPlayer::InvalidMedia &&
                            status != QMediaPlayer::EndOfMedia;
#else
      m_mediaPausedByGame =
            m_mediaPlayer->state() == QMediaPlayer::PlayingState;
#endif
      if (m_mediaPausedByGame)
      {
            m_mediaPlayer->pause();
      }
}

void GameScreen::resumeMediaPlayback()
{
      if (m_mediaDurationPaused && m_mediaRemainingMs > 0)
      {
            m_mediaDurationPaused = false;
            m_mediaDurationElapsedTimer.restart();
            m_mediaDurationTimer->start(static_cast<int>(std::min<unsigned int>(
                  m_mediaRemainingMs,
                  static_cast<unsigned int>(
                        std::numeric_limits<int>::max()))));
      }
      if (m_mediaPlayer != nullptr && m_mediaPausedByGame)
      {
            m_mediaPausedByGame = false;
            m_mediaPlayer->play();
      }
}

void GameScreen::fitDisplayedPixmap()
{
      if (m_displayedPixmap.isNull() ||
          !ui->questionMediaLabel->isVisible())
      {
            return;
      }
      const QSize maximumImageSize =
            ui->questionMediaLabel->size().boundedTo(
                  ui->questionFrame->contentsRect().size());
      if (maximumImageSize.isEmpty())
      {
            return;
      }
      QPixmap fittedPixmap =
            m_displayedPixmap.scaled(maximumImageSize, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation);
      m_displayedPixmapRect = QRect(
            (ui->questionMediaLabel->width() - fittedPixmap.width()) / 2,
            (ui->questionMediaLabel->height() - fittedPixmap.height()) / 2,
            fittedPixmap.width(), fittedPixmap.height());
      if (m_submittedPoint.has_value())
      {
            QPainter painter(&fittedPixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen markerPen(Qt::red);
            markerPen.setWidth(3);
            painter.setPen(markerPen);
            const QPointF marker(
                  m_submittedPoint->x() * fittedPixmap.width(),
                  m_submittedPoint->y() * fittedPixmap.height());
            const int radius =
                  std::max(6, std::min(fittedPixmap.width(),
                                      fittedPixmap.height()) /
                                    50);
            painter.drawEllipse(marker, radius, radius);
      }
      ui->questionMediaLabel->setPixmap(fittedPixmap);
}

void GameScreen::fitAnswerOptionsTable()
{
      if (!ui->answerOptionsTable->isVisible())
      {
            return;
      }

      const QMargins margins = ui->questionFrameLayout->contentsMargins();
      const QSize frameSize = ui->questionFrame->contentsRect().size();
      const int availableWidth =
            frameSize.width() - margins.left() - margins.right();
      const int frameHeight =
            frameSize.height() - margins.top() - margins.bottom();
      const QFontMetrics textMetrics(ui->questionTextLabel->font());
      const int maximumTextHeight = std::max(
            textMetrics.lineSpacing() * 2, frameHeight / 3);
      const int requiredTextHeight =
            textMetrics
                  .boundingRect(QRect(0, 0, availableWidth,
                                      maximumTextHeight),
                                Qt::AlignCenter | Qt::TextWordWrap,
                                currentQuestion().text)
                  .height();
      const int textHeight = std::min(
            maximumTextHeight,
            std::max(textMetrics.lineSpacing(), requiredTextHeight));
      ui->questionTextLabel->setFixedHeight(textHeight);

      const int columnCount = ui->answerOptionsTable->columnCount();
      const int rowCount = ui->answerOptionsTable->rowCount();
      const QSize containerSize =
            ui->answerOptionsContainer->contentsRect().size();
      if (columnCount == 0 || rowCount == 0 || containerSize.isEmpty())
      {
            return;
      }

      const int tableBorderExtent = 4;
      const int tileSide = std::max(
            1, std::min((containerSize.width() - tableBorderExtent) /
                              columnCount,
                        (containerSize.height() - tableBorderExtent) /
                              rowCount));
      ui->answerOptionsTable->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
      ui->answerOptionsTable->verticalHeader()->setSectionResizeMode(
            QHeaderView::Fixed);
      for (int column = 0; column < columnCount; ++column)
      {
            ui->answerOptionsTable->horizontalHeader()->resizeSection(
                  column, tileSide);
      }
      for (int row = 0; row < rowCount; ++row)
      {
            ui->answerOptionsTable->verticalHeader()->resizeSection(row,
                                                                    tileSide);
      }

      const QSize tableSize(tileSide * columnCount + tableBorderExtent,
                            tileSide * rowCount + tableBorderExtent);
      ui->answerOptionsTable->setSizeAdjustPolicy(
            QAbstractScrollArea::AdjustIgnored);
      ui->answerOptionsTable->setGeometry(
            (containerSize.width() - tableSize.width()) / 2,
            (containerSize.height() - tableSize.height()) / 2,
            tableSize.width(), tableSize.height());
}

void GameScreen::startReactionFlash()
{
      stopReactionFlash();
      m_flashStep = 0;
      ui->questionFrame->setStyleSheet(
            m_questionFrameStyleSheet +
            QStringLiteral(
                  "\nQFrame#questionFrame { border: 5px solid yellow; }"));
      m_flashTimer->start(120);
}

void GameScreen::stopReactionFlash()
{
      m_flashTimer->stop();
      m_flashStep = 0;
      ui->questionFrame->setStyleSheet(m_questionFrameStyleSheet);
}

void GameScreen::openAnswerInput()
{
      switch (currentQuestion().answerType)
      {
      case AnswerType::Text:
            openTextAnswerDialog();
            break;
      case AnswerType::Select:
            if (currentQuestion().answerOptions.size() >= 2)
            {
                  enableSelectAnswerInput();
            }
            else
            {
                  qWarning() << "Invalid select question; using text input";
                  openTextAnswerDialog();
            }
            break;
      case AnswerType::Point:
            enablePointAnswerInput();
            break;
      case AnswerType::Unknown:
            qWarning() << "Unknown answer type; using text input";
            openTextAnswerDialog();
            break;
      }
}

void GameScreen::openTextAnswerDialog()
{
      m_answerDialog = new QInputDialog(this);
      m_answerDialog->setAttribute(Qt::WA_DeleteOnClose);
      m_answerDialog->setInputMode(QInputDialog::TextInput);
      m_answerDialog->setWindowTitle(tr("Answer question"));
      if (m_mode == GameScreenMode::SinglePlayer &&
          m_singleFinalQuestionActive &&
          m_singleFinalAnswerPlayerIndex >= 0 &&
          m_singleFinalAnswerPlayerIndex < static_cast<int>(m_players.size()))
      {
            m_answerDialog->setLabelText(
                  tr("%1, enter your answer:")
                        .arg(m_players[static_cast<std::size_t>(
                              m_singleFinalAnswerPlayerIndex)]
                                   .name));
      }
      else
      {
            m_answerDialog->setLabelText(tr("Enter your answer:"));
      }
      connect(m_answerDialog, &QInputDialog::textValueSelected, this,
              &GameScreen::handleSubmittedAnswer);
      connect(m_answerDialog, &QInputDialog::textValueChanged, this,
              [this](const QString &answer)
              {
                    if (m_mode != GameScreenMode::SinglePlayer &&
                        (currentQuestion().answerType == AnswerType::Text ||
                         currentQuestion().answerType == AnswerType::Unknown))
                    {
                          emit answerDraftChanged(answer);
                    }
              });
      connect(m_answerDialog, &QDialog::rejected, this,
              &GameScreen::handleAnswerDeclined);
      m_answerDialog->open();
}

void GameScreen::enableSelectAnswerInput()
{
      ui->answerOptionsTable->viewport()->setAttribute(
            Qt::WA_TransparentForMouseEvents, false);
}

void GameScreen::enablePointAnswerInput()
{
      if (m_displayedPixmap.isNull() ||
          (m_mode == GameScreenMode::SinglePlayer &&
           !m_correctPoint.has_value()))
      {
            qWarning() << "Point input cannot be enabled";
            return;
      }
      ui->questionMediaLabel->show();
      QTimer::singleShot(0, this, &GameScreen::fitDisplayedPixmap);
      m_pointInputEnabled = true;
      ui->questionMediaLabel->setCursor(Qt::CrossCursor);
}

void GameScreen::handleSubmittedAnswer(const QString &answer)
{
      if ((m_phase != GamePhase::Answering &&
           m_phase != GamePhase::ForAllAnswering) ||
          (m_mode == GameScreenMode::SinglePlayer && m_players.empty()))
      {
            return;
      }
      if (m_mode != GameScreenMode::SinglePlayer)
      {
            if (m_networkAnswerSubmitted)
            {
                  return;
            }
            AnswerSubmission submission;
            submission.answerType = currentQuestion().answerType;
            submission.mode = m_forAllAnswering
                                    ? QStringLiteral("ForAll")
                                    : (currentQuestion().type ==
                                             QuestionType::SecretPublicPrice
                                           ? QStringLiteral("Secret")
                                           : QString());
            if (submission.answerType == AnswerType::Select)
            {
                  submission.optionId = answer.trimmed();
            }
            else
            {
                  submission.answer = answer;
            }
            m_networkAnswerSubmitted = true;
            emitNetworkAnswer(submission);
            return;
      }

      const QString normalizedAnswer = answer.trimmed();
      const bool isCorrect = std::any_of(
            currentQuestion().rightAnswers.cbegin(),
            currentQuestion().rightAnswers.cend(),
            [&normalizedAnswer](const QString &rightAnswer)
            {
                  return normalizedAnswer.compare(rightAnswer.trimmed(),
                                                  Qt::CaseInsensitive) == 0;
            });
      if (m_singleFinalQuestionActive &&
          m_phase == GamePhase::ForAllAnswering)
      {
            finishSingleFinalAnswer(isCorrect);
            return;
      }
      if (currentQuestion().answerType == AnswerType::Select)
      {
            const QColor resultColor(
                  isCorrect ? QStringLiteral("#2e7d32")
                            : QStringLiteral("#c62828"));
            for (int row = 0; row < ui->answerOptionsTable->rowCount(); ++row)
            {
                  for (int column = 0;
                       column < ui->answerOptionsTable->columnCount(); ++column)
                  {
                        QTableWidgetItem *item =
                              ui->answerOptionsTable->item(row, column);
                        if (item != nullptr &&
                            item->data(Qt::UserRole)
                                        .toString()
                                        .compare(normalizedAnswer,
                                                 Qt::CaseInsensitive) == 0)
                        {
                              item->setBackground(resultColor);
                              item->setForeground(Qt::white);
                              ui->answerOptionsTable->viewport()->update();
                        }
                  }
            }
      }
      applyAnswerResult(isCorrect, answer);
}

void GameScreen::handleAnswerDeclined()
{
      if (m_mode != GameScreenMode::SinglePlayer)
      {
            if ((m_phase == GamePhase::Answering ||
                 m_phase == GamePhase::ForAllAnswering) &&
                !m_networkAnswerSubmitted)
            {
                  AnswerSubmission submission;
                  submission.answerType = currentQuestion().answerType;
                  submission.mode = m_forAllAnswering
                                          ? QStringLiteral("ForAll")
                                          : (currentQuestion().type ==
                                                   QuestionType::SecretPublicPrice
                                                 ? QStringLiteral("Secret")
                                                 : QString());
                  m_networkAnswerSubmitted = true;
                  emitNetworkAnswer(submission);
            }
            return;
      }
      if (m_singleFinalQuestionActive &&
          m_phase == GamePhase::ForAllAnswering)
      {
            finishSingleFinalAnswer(false);
      }
      else if (m_phase == GamePhase::Answering && !m_answerResultApplied)
      {
            applyAnswerResult(false, QString());
      }
}

void GameScreen::applyAnswerResult(bool isCorrect,
                                   const QString &submittedAnswer)
{
      if (m_answerResultApplied || m_players.empty())
      {
            return;
      }

      m_answerResultApplied = true;
      m_submittedAnswer = submittedAnswer;
      Player &player = m_players.front();
      const QString displayedAnswer = displayAnswerText(submittedAnswer).trimmed();
      if (player.answerBubble != nullptr && !displayedAnswer.isEmpty())
      {
            player.answerBubble->setText(displayedAnswer);
            player.answerBubble->show();
            positionAnswerBubbles();
      }
      setSinglePlayerGlow(isCorrect ? PlayerGlow::Correct
                                    : PlayerGlow::Incorrect,
                          true);
      if (isCorrect)
      {
            player.balance += currentQuestion().price;
            updateBalanceLabel(player);
            m_answerDialog = nullptr;
            showAnswer();
            return;
      }

      player.balance -= currentQuestion().price;
      updateBalanceLabel(player);
      emit incorrectAnswerSubmitted(m_localPlayerId.isEmpty()
                                          ? QStringLiteral("player-1")
                                          : m_localPlayerId,
                                    submittedAnswer);
}

void GameScreen::applyAuthoritativeAnswerResult(const AnswerResult &result)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_submittedAnswer = result.submitted;
      if (result.playerId == m_localPlayerId)
      {
            m_networkAnswerSubmitted = true;
      }
      if (m_answerDialog != nullptr && m_answerDialog->isVisible())
      {
            m_answerDialog->reject();
      }
      if (result.answerKind == AnswerType::Select &&
          !result.submitted.isEmpty())
      {
            const QColor color(result.correct ? QStringLiteral("#2e7d32")
                                             : QStringLiteral("#c62828"));
            for (int row = 0; row < ui->answerOptionsTable->rowCount(); ++row)
            {
                  for (int column = 0;
                       column < ui->answerOptionsTable->columnCount(); ++column)
                  {
                        QTableWidgetItem *item =
                              ui->answerOptionsTable->item(row, column);
                        if (item != nullptr &&
                            item->data(Qt::UserRole).toString().compare(
                                  result.submitted, Qt::CaseInsensitive) == 0)
                        {
                              item->setBackground(color);
                              item->setForeground(Qt::white);
                        }
                  }
            }
      }
}

void GameScreen::populateAnswerOptions(const Question &question)
{
      clearAnswerOptions();
      const int columnCount = 2;
      const int rowCount = static_cast<int>(
            (question.answerOptions.size() + columnCount - 1) / columnCount);
      ui->answerOptionsTable->setColumnCount(columnCount);
      ui->answerOptionsTable->setRowCount(rowCount);
      ui->answerOptionsTable->horizontalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
      ui->answerOptionsTable->verticalHeader()->setSectionResizeMode(
            QHeaderView::Stretch);
      QFont optionFont = ui->questionTextLabel->font();
      optionFont.setPixelSize(
            std::max(16, screen()->availableGeometry().height() / 45));
      ui->answerOptionsTable->setFont(optionFont);

      for (std::size_t index = 0; index < question.answerOptions.size();
           ++index)
      {
            const AnswerOption &option = question.answerOptions[index];
            auto *item = new QTableWidgetItem(
                  QStringLiteral("%1\n%2").arg(option.id, option.text));
            item->setData(Qt::UserRole, option.id);
            item->setBackground(QColor(QStringLiteral("#2d2d3a")));
            item->setForeground(Qt::white);
            item->setTextAlignment(Qt::AlignCenter);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            ui->answerOptionsTable->setItem(
                  static_cast<int>(index) / columnCount,
                  static_cast<int>(index) % columnCount, item);
      }
      ui->answerOptionsTable->setEnabled(true);
      ui->answerOptionsTable->setCurrentCell(-1, -1);
      ui->answerOptionsTable->clearSelection();
      ui->answerOptionsTable->viewport()->setAttribute(
            Qt::WA_TransparentForMouseEvents, true);
      ui->answerOptionsContainer->show();
      ui->answerOptionsTable->show();
      ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                           QSizePolicy::Maximum);
      QTimer::singleShot(0, this, &GameScreen::fitAnswerOptionsTable);
}

void GameScreen::clearAnswerOptions()
{
      ui->answerOptionsTable->clear();
      ui->answerOptionsTable->setRowCount(0);
      ui->answerOptionsTable->setColumnCount(0);
      ui->answerOptionsTable->viewport()->setAttribute(
            Qt::WA_TransparentForMouseEvents, true);
      ui->answerOptionsTable->hide();
      ui->answerOptionsContainer->hide();
}

void GameScreen::highlightSelectAnswers(const Question &question)
{
      for (int row = 0; row < ui->answerOptionsTable->rowCount(); ++row)
      {
            for (int column = 0;
                 column < ui->answerOptionsTable->columnCount(); ++column)
            {
                  QTableWidgetItem *item =
                        ui->answerOptionsTable->item(row, column);
                  if (item == nullptr)
                  {
                        continue;
                  }
                  const QString optionId =
                        item->data(Qt::UserRole).toString();
                  const bool isCorrect = std::any_of(
                        question.rightAnswers.cbegin(),
                        question.rightAnswers.cend(),
                        [&optionId](const QString &rightAnswer)
                        {
                              return optionId.compare(
                                           rightAnswer.trimmed(),
                                           Qt::CaseInsensitive) == 0;
                        });
                  if (isCorrect)
                  {
                        item->setBackground(QColor(QStringLiteral("#2e7d32")));
                        item->setForeground(Qt::white);
                  }
                  else if (!m_submittedAnswer.isEmpty() &&
                           optionId.compare(m_submittedAnswer,
                                            Qt::CaseInsensitive) == 0)
                  {
                        item->setBackground(QColor(QStringLiteral("#c62828")));
                        item->setForeground(Qt::white);
                  }
            }
      }
}

void GameScreen::resetAnswerInputState()
{
      if (m_answerDialog != nullptr)
      {
            disconnect(m_answerDialog, nullptr, this, nullptr);
            m_answerDialog->reject();
            m_answerDialog = nullptr;
      }
      m_submittedAnswer.clear();
      m_networkAnswerSubmitted = false;
      m_networkAnswerInputOpened = false;
      m_networkReactionClaimed = false;
      clearAnswerOptions();
      ui->questionTextLabel->setMinimumHeight(0);
      ui->questionTextLabel->setMaximumHeight(QWIDGETSIZE_MAX);
      m_pointInputEnabled = false;
      m_correctPoint.reset();
      m_submittedPoint.reset();
      m_correctPointAspectRatio = 1.0;
      m_displayedPixmapRect = {};
      ui->questionMediaLabel->setCursor(Qt::ArrowCursor);
      ui->appealButton->hide();
      m_appealVoteSubmitted = false;
      m_appealAppellant.clear();
      m_appealId = 0;
}

bool GameScreen::parsePointAnswer(const QString &value, QPointF *point,
                                  double *aspectRatio) const
{
      const QStringList parts = value.split(QLatin1Char(','),
                                            Qt::KeepEmptyParts);
      if (parts.size() != 2 && parts.size() != 3)
      {
            return false;
      }

      bool xValid{false};
      bool yValid{false};
      const double x = parts[0].trimmed().toDouble(&xValid);
      const double y = parts[1].trimmed().toDouble(&yValid);
      if (!xValid || !yValid || !std::isfinite(x) || !std::isfinite(y))
      {
            return false;
      }

      double parsedAspectRatio{1.0};
      if (parts.size() == 3)
      {
            bool aspectRatioValid{false};
            const double candidate =
                  parts[2].trimmed().toDouble(&aspectRatioValid);
            if (!aspectRatioValid || !std::isfinite(candidate))
            {
                  return false;
            }
            if (candidate > 0.0)
            {
                  parsedAspectRatio = candidate;
            }
      }
      *point = QPointF(x, y);
      *aspectRatio = parsedAspectRatio;
      return true;
}

void GameScreen::updateBalanceLabel(Player &player)
{
      player.balanceLabel->setText(tr("Balance: %1").arg(player.balance));
}

void GameScreen::bindHost(MultiplayerHost *host)
{
      if (host == nullptr || m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_host = host;
      m_mode = GameScreenMode::MultiplayerHost;
      setLocalPlayerId(host->localPlayerId());
      connectHostSignals(host);
      connect(this, &GameScreen::questionPickRequested, this,
              [this](int round, int theme, int question)
              {
                    m_host->onQuestionSelected(m_localPlayerId, round, theme,
                                               question, nextLocalActionId());
              });
      connect(this, &GameScreen::reactionClaimRequested, this,
              [this](unsigned int elapsed)
              {
                    m_host->onReactionClaim(m_localPlayerId,
                                            m_networkQuestionSequence,
                                            m_networkPhaseSequence,
                                            nextLocalActionId(), elapsed);
              });
      connect(this, &GameScreen::answerDraftChanged, this,
              [this](const QString &answer)
              {
                    m_host->onAnswerDraftChanged(
                          m_localPlayerId, m_networkQuestionSequence,
                          m_networkPhaseSequence, nextLocalActionId(), answer);
              });
      connect(this, &GameScreen::answerSubmitted, this,
              [this](const AnswerSubmission &submission)
              {
                    m_host->onAnswerSubmitted(m_localPlayerId,
                                              m_networkQuestionSequence,
                                              m_networkPhaseSequence,
                                              nextLocalActionId(), submission);
              });
      connect(this, &GameScreen::passRequested, this,
              [this]()
              {
                    m_host->onPass(m_localPlayerId, m_networkQuestionSequence,
                                   m_networkPhaseSequence, nextLocalActionId());
              });
      connect(this, &GameScreen::secretTargetRequested, this,
              [this](const PlayerId &target)
              {
                    m_host->onSecretTargetSelected(
                          m_localPlayerId, target, m_networkQuestionSequence,
                          nextLocalActionId());
              });
      connect(this, &GameScreen::secretWagerSubmitted, this,
              [this](int amount)
              {
                    m_host->onSecretWagerSubmitted(
                          m_localPlayerId, amount, m_networkQuestionSequence,
                          nextLocalActionId());
              });
      connect(this, &GameScreen::pauseRequested, this,
              [this](bool paused)
              {
                    m_host->onPauseRequested(m_localPlayerId, paused,
                                             m_networkPhaseSequence,
                                             nextLocalActionId());
              });
      connect(this, &GameScreen::appealRequested, this,
              [this]()
              {
                    m_host->onAppealRequested(m_localPlayerId,
                                              m_networkQuestionSequence,
                                              nextLocalActionId());
              });
      connect(this, &GameScreen::appealVoteSubmitted, this,
              [this](bool accepted)
              {
                    m_host->onAppealVote(m_localPlayerId, m_appealId, accepted,
                                         nextLocalActionId());
              });
      applySnapshot(host->session()->snapshot());
}

void GameScreen::bindClient(MultiplayerClient *client)
{
      if (client == nullptr || m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_client = client;
      m_mode = GameScreenMode::MultiplayerClient;
      setLocalPlayerId(client->localPlayerId());
      connectClientSignals(client);
      connect(this, &GameScreen::questionPickRequested, this,
              [this](int round, int theme, int question)
              { m_client->selectQuestion(round, theme, question); });
      connect(this, &GameScreen::reactionClaimRequested, this,
              [this](unsigned int elapsed)
              {
                    m_client->submitReaction(m_networkQuestionSequence,
                                             m_networkPhaseSequence, elapsed);
              });
      connect(this, &GameScreen::answerDraftChanged, this,
              [this](const QString &answer)
              { m_client->updateAnswerDraft(m_networkQuestionSequence,
                                            m_networkPhaseSequence, answer); });
      connect(this, &GameScreen::answerSubmitted, this,
              [this](const AnswerSubmission &submission)
              { m_client->submitAnswer(m_networkQuestionSequence,
                                       m_networkPhaseSequence, submission); });
      connect(this, &GameScreen::passRequested, this,
              [this]()
              { m_client->pass(m_networkQuestionSequence, m_networkPhaseSequence); });
      connect(this, &GameScreen::secretTargetRequested, this,
              [this](const PlayerId &target)
              { m_client->selectSecretTarget(m_networkQuestionSequence, target); });
      connect(this, &GameScreen::secretWagerSubmitted, this,
              [this](int amount)
              { m_client->submitSecretWager(m_networkQuestionSequence, amount); });
      connect(this, &GameScreen::pauseRequested, this,
              [this](bool paused)
              { m_client->requestPause(m_networkPhaseSequence, paused); });
      connect(this, &GameScreen::appealRequested, this,
              [this]() { m_client->requestAppeal(m_networkQuestionSequence); });
      connect(this, &GameScreen::appealVoteSubmitted, this,
              [this](bool accepted) { m_client->voteAppeal(m_appealId, accepted); });
}

void GameScreen::setLocalPlayerId(const PlayerId &playerId)
{
      m_localPlayerId = playerId;
      setNetworkControls();
}

void GameScreen::setPlayerStates(const QVector<PlayerState> &players)
{
      applyPlayers(players);
}

void GameScreen::setPicker(const PlayerId &playerId)
{
      m_pickerId = playerId;
      for (PlayerState &state : m_networkPlayers)
      {
            state.isPicker = state.id == playerId;
      }
      setNetworkControls();
      rebuildNetworkPlayerCards();
}

void GameScreen::setAnswerOwner(const PlayerId &playerId)
{
      applyAnswerOwner(playerId);
}

void GameScreen::setQuestionPermissions(bool canAnswer, bool canPass)
{
      m_canAnswer = canAnswer;
      m_canPass = canPass;
      setNetworkControls();
}

void GameScreen::setAppealPermission(bool allowed)
{
      m_canAppeal = allowed;
      setNetworkControls();
}

void GameScreen::setPausePermission(bool allowed)
{
      m_canPause = allowed;
      setNetworkControls();
}

void GameScreen::applyPlayers(const QVector<PlayerState> &players)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_networkPlayers = players;
      for (const PlayerState &state : players)
      {
            if (state.isPicker)
            {
                  m_pickerId = state.id;
            }
      }
      rebuildNetworkPlayerCards();
      setNetworkControls();
}

void GameScreen::applyBoard(const BoardState &board)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      applyNetworkBoard(board);
}

void GameScreen::applyPhase(const PhaseState &phase)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      const bool phaseChanged =
            phase.phaseSequence != m_networkPhaseSequence ||
            phase.phase != m_phase;
      m_networkPhase = phase;
      m_phase = phase.phase;
      m_networkPhaseSequence = phase.phaseSequence;
      m_networkQuestionSequence = phase.questionSequence;
      if (phaseChanged &&
          (phase.phase == SessionPhase::WaitingForReaction ||
           phase.phase == SessionPhase::Answering ||
           phase.phase == SessionPhase::ForAllAnswering))
      {
            finishMediaDisplay();
      }
      if (phaseChanged &&
          (phase.phase == SessionPhase::PickingQuestion ||
           phase.phase == SessionPhase::Lobby ||
           phase.phase == SessionPhase::Finished))
      {
            stopMediaPlayback();
            clearAnswerBubbles();
      }
      if (!phase.owner.isEmpty() &&
          (phase.phase == SessionPhase::Answering ||
           phase.phase == SessionPhase::SecretWager ||
           phase.phase == SessionPhase::ReadingQuestion))
      {
            m_answerOwnerId = phase.owner;
      }
      m_secretTargetSelection = phase.phase == SessionPhase::SecretTargetSelection;
      m_canPause = phase.phase == SessionPhase::PickingQuestion ||
                   phase.phase == SessionPhase::ReadingQuestion;
      if (phaseChanged && phase.phase != SessionPhase::AppealVoting)
      {
            m_appealVoteSubmitted = false;
            m_appealAppellant.clear();
            m_appealId = 0;
      }
      if (phase.phase == SessionPhase::AppealVoting)
      {
            if (phaseChanged)
            {
                  m_appealVoteSubmitted = true;
                  m_appealAppellant.clear();
                  m_appealId = 0;
            }
            ui->gameContentStack->setCurrentWidget(m_appealPage);
      }
      else if (phase.phase == SessionPhase::PickingQuestion)
      {
            m_pickerId = phase.owner;
            if (isFinalRound(m_networkBoard.round))
            {
                  QString pickerName = m_pickerId;
                  for (const PlayerState &state : m_networkPlayers)
                  {
                        if (state.id == m_pickerId)
                        {
                              pickerName = state.nickname;
                              break;
                        }
                  }
                  m_boardStatusLabel->setText(
                        tr("%1 eliminates a topic").arg(pickerName));
            }
            ui->gameContentStack->setCurrentWidget(ui->boardPage);
      }
      else if (phase.phase == SessionPhase::FinalWager)
      {
            m_boardStatusLabel->setText(tr("Waiting for final wagers"));
            applyNetworkBoard(m_networkBoard);
            ui->gameContentStack->setCurrentWidget(ui->boardPage);
      }
      else if (phase.phase != SessionPhase::Lobby &&
               phase.phase != SessionPhase::SecretTargetSelection &&
               phase.phase != SessionPhase::SecretWager &&
               phase.phase != SessionPhase::FinalWager)
      {
            ui->gameContentStack->setCurrentWidget(ui->questionPage);
      }
      if (phase.phase == SessionPhase::WaitingForReaction)
      {
            if (phaseChanged)
            {
                  m_networkReactionClaimed = false;
                  m_reactionElapsedTimer.restart();
                  startReactionFlash();
            }
      }
      else if (phaseChanged)
      {
            m_reactionElapsedTimer.invalidate();
            stopReactionFlash();
      }
      m_forAllAnswering = phase.phase == SessionPhase::ForAllAnswering;
      setNetworkPhaseTimer(phase);
      setNetworkControls();
      if (phase.phase == SessionPhase::Answering &&
          m_answerOwnerId == m_localPlayerId &&
          m_networkQuestion.has_value() && !m_networkAnswerSubmitted &&
          !m_networkAnswerInputOpened)
      {
            m_networkAnswerInputOpened = true;
            ui->AnswerBytton->setEnabled(false);
            openAnswerInput();
      }
      else if (phase.phase == SessionPhase::ForAllAnswering &&
               isFinalRound(m_boardRoundIndex) && m_canAnswer &&
               m_networkQuestion.has_value() && !m_networkAnswerSubmitted &&
               !m_networkAnswerInputOpened)
      {
            m_networkAnswerInputOpened = true;
            ui->AnswerBytton->setEnabled(false);
            openAnswerInput();
      }
}

void GameScreen::applyQuestion(const QuestionPresentation &question)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      applyNetworkQuestion(question);
}

void GameScreen::applyAnswerOwner(const PlayerId &playerId)
{
      m_answerOwnerId = playerId;
      setNetworkControls();
}

void GameScreen::applyAnswerResult(const AnswerResult &result)
{
      applyAuthoritativeAnswerResult(result);
      const PlayerGlow glow = result.correct ? PlayerGlow::Correct
                                             : PlayerGlow::Incorrect;
      m_playerGlows.insert(result.playerId, glow);
      for (PlayerState &state : m_networkPlayers)
      {
            if (state.id == result.playerId)
            {
                  state.balance = result.balance;
                  state.answeredIncorrectly = !result.correct;
                  state.mayAppeal = !result.correct;
                  break;
            }
      }
      rebuildNetworkPlayerCards();
      showNetworkAnswerBubble(result);
      setNetworkControls();
      QTimer::singleShot(
            PlayerResultGlowDuration, this,
            [this, playerId = result.playerId, glow]()
            {
                  if (m_playerGlows.value(playerId) == glow)
                  {
                        m_playerGlows.remove(playerId);
                        rebuildNetworkPlayerCards();
                  }
            });
}

void GameScreen::applyReveal(const AnswerReveal &reveal)
{
      if (m_mode == GameScreenMode::SinglePlayer || !m_networkQuestion.has_value())
      {
            return;
      }
      Question question = *m_networkQuestion;
      question.rightAnswers.clear();
      for (const QString &answer : reveal.rightAnswers)
      {
            question.rightAnswers.push_back(answer);
      }
      question.answerMediaType = reveal.answerMediaType;
      question.answerMediaPath = reveal.answerMediaPath;
      displayContent(reveal.rightAnswers.join(QLatin1Char('\n')),
                     reveal.answerMediaType, reveal.answerMediaPath,
                     reveal.mediaDurationMs);
      if (question.answerType == AnswerType::Select)
      {
            highlightSelectAnswers(question);
      }
      m_pickerId = reveal.nextPicker;
      m_answerOwnerId = reveal.answerOwner;
      m_pointInputEnabled = false;
      setNetworkControls();
}

void GameScreen::applyForAllResult(const ForAllResult &result)
{
      for (const AnswerResult &answer : result.results)
      {
            if (answer.playerId == m_localPlayerId)
            {
                  applyAnswerResult(answer);
            }
            else
            {
                  showNetworkAnswerBubble(answer);
            }
      }
}

void GameScreen::applyAppeal(const AppealState &appeal)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      stopMediaPlayback();
      m_appealId = appeal.appealId;
      m_appealAppellant = appeal.appellant;
      m_appealVoteSubmitted = appeal.votes.contains(m_localPlayerId);

      QString appellantName = appeal.appellant;
      for (const PlayerState &state : m_networkPlayers)
      {
            if (state.id == appeal.appellant)
            {
                  appellantName = state.nickname.isEmpty() ? tr("Unnamed")
                                                           : state.nickname;
                  break;
            }
      }
      m_appealSubmittedHeadingLabel->setText(
            tr("%1's answer").arg(appellantName));

      QString questionText;
      m_appealQuestionPixmap = {};
      if (m_networkQuestion.has_value())
      {
            questionText = m_networkQuestion->text;
            if (m_networkQuestion->mediaType == MediaType::Image &&
                !m_networkQuestion->mediaPath.isEmpty())
            {
                  m_appealQuestionPixmap.load(
                        QDir(m_gamepackPath)
                              .filePath(m_networkQuestion->mediaPath));
            }
            else if (m_networkQuestion->mediaType != MediaType::None &&
                     !m_networkQuestion->mediaPath.isEmpty())
            {
                  questionText += tr("\nMedia: %1")
                                        .arg(m_networkQuestion->mediaPath);
            }
      }
      if (questionText.isEmpty() && m_appealQuestionPixmap.isNull())
      {
            questionText = tr("(no question text)");
      }
      m_appealQuestionLabel->setText(questionText);
      m_appealQuestionMediaLabel->setVisible(
            !m_appealQuestionPixmap.isNull());

      const QString submitted = displayAnswerText(appeal.submitted);
      m_appealSubmittedLabel->setText(
            submitted.isEmpty() ? tr("(no answer)") : submitted);

      QStringList correctAnswers;
      for (const QString &answer : appeal.rightAnswers)
      {
            correctAnswers.push_back(displayAnswerText(answer));
      }
      m_appealCorrectAnswerPixmap = {};
      if (appeal.answerMediaType == MediaType::Image &&
          !appeal.answerMediaPath.isEmpty())
      {
            m_appealCorrectAnswerPixmap.load(
                  QDir(m_gamepackPath).filePath(appeal.answerMediaPath));
      }
      else if (appeal.answerMediaType != MediaType::None &&
               !appeal.answerMediaPath.isEmpty())
      {
            correctAnswers.push_back(
                  tr("Media: %1").arg(appeal.answerMediaPath));
      }
      m_appealCorrectAnswerLabel->setText(
            correctAnswers.isEmpty()
                  ? (m_appealCorrectAnswerPixmap.isNull()
                           ? tr("(no text answer)")
                           : tr("See answer media below."))
                  : correctAnswers.join(QLatin1Char('\n')));
      m_appealCorrectAnswerMediaLabel->setVisible(
            !m_appealCorrectAnswerPixmap.isNull());

      ui->appealButton->hide();
      ui->gameContentStack->setCurrentWidget(m_appealPage);
      QTimer::singleShot(0, this, &GameScreen::fitAppealPixmaps);
      setNetworkControls();
}

void GameScreen::applyAppealResult(const AppealResult &)
{
      m_appealVoteSubmitted = true;
      ui->appealButton->hide();
      setNetworkControls();
}

void GameScreen::applyPause(bool paused, SessionPhase phase,
                            unsigned int remainingMs)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            if (paused)
            {
                  pauseSinglePlayer();
            }
            else
            {
                  resumeSinglePlayer();
            }
            return;
      }
      m_networkPaused = paused;
      if (paused)
      {
            m_tickTimer->stop();
            m_progressAnimation->stop();
            pauseMediaPlayback();
            ui->gameContentStack->setCurrentWidget(ui->pausePage);
      }
      else
      {
            resumeMediaPlayback();
            PhaseState state = m_networkPhase;
            state.phase = phase;
            state.remainingMs = remainingMs;
            state.durationMs = remainingMs;
            applyPhase(state);
      }
      setNetworkControls();
}

void GameScreen::applySnapshot(const SessionSnapshot &snapshot)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_networkQuestionSequence = snapshot.phase.questionSequence;
      m_networkPhaseSequence = snapshot.phase.phaseSequence;
      m_pickerId = snapshot.currentPicker;
      m_answerOwnerId = snapshot.answerOwner;
      applyPlayers(snapshot.players);
      applyBoard(snapshot.board);
      if (snapshot.question.has_value())
      {
            applyQuestion(*snapshot.question);
      }
      applyPhase(snapshot.phase);
      if (snapshot.reveal.has_value())
      {
            applyReveal(*snapshot.reveal);
      }
      if (snapshot.appeal.has_value())
      {
            applyAppeal(*snapshot.appeal);
      }
      if (snapshot.paused)
      {
            applyPause(true, snapshot.phase.phase, snapshot.phase.remainingMs);
      }
}

void GameScreen::applySecretTargets(const QVector<PlayerState> &targets)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_secretTargetSelection = true;
      QSet<PlayerId> targetIds;
      for (const PlayerState &target : targets)
      {
            targetIds.insert(target.id);
      }
      for (PlayerState &state : m_networkPlayers)
      {
            state.isPicker = targetIds.contains(state.id);
      }
      rebuildNetworkPlayerCards();
      setNetworkControls();
}

void GameScreen::applyWagerPrompt(const SecretWagerParameters &parameters)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      auto *dialog = new QInputDialog(this);
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->setInputMode(QInputDialog::IntInput);
      dialog->setWindowTitle(m_phase == SessionPhase::FinalWager
                                   ? tr("Final wager")
                                   : tr("Secret wager"));
      const QString label = parameters.theme.isEmpty()
                                  ? tr("Choose a wager:")
                                  : tr("%1\nChoose a wager:").arg(parameters.theme);
      dialog->setLabelText(label);
      dialog->setIntRange(parameters.minimum, parameters.maximum);
      dialog->setIntStep(parameters.step > 0 ? parameters.step : 1);
      dialog->setIntValue(parameters.minimum);
      connect(dialog, &QInputDialog::intValueSelected, this,
              [this](int amount) { emit secretWagerSubmitted(amount); });
      dialog->open();
}

void GameScreen::applyNetworkQuestion(
      const QuestionPresentation &presentation)
{
      clearAnswerBubbles();
      m_networkQuestion = Question{};
      m_networkQuestion->price = presentation.price;
      m_networkQuestion->type = presentation.questionType;
      m_networkQuestion->answerType = presentation.answerType;
      m_networkQuestion->answerDuration = presentation.answerDurationMs / 1000U;
      m_networkQuestion->text = presentation.text;
      m_networkQuestion->mediaType = presentation.mediaType;
      m_networkQuestion->mediaPath = presentation.mediaPath;
      m_networkQuestion->mediaDuration =
            presentation.mediaDurationMs / 1000U;
      m_networkQuestion->answerOptions.assign(presentation.answerOptions.cbegin(),
                                              presentation.answerOptions.cend());
      m_networkQuestionSequence = presentation.questionSequence;
      m_currentThemeIndex = presentation.theme;
      m_currentQuestionIndex = presentation.question;
      m_answerOwnerId = presentation.answerOwner;
      m_secretTargetSelection = false;
      resetAnswerInputState();
      displayContent(presentation.text, presentation.mediaType,
                     presentation.mediaPath,
                     presentation.mediaDurationMs);
      if (presentation.answerType == AnswerType::Select &&
          presentation.answerOptions.size() >= 2)
      {
            populateAnswerOptions(*m_networkQuestion);
      }
      ui->gameContentStack->setCurrentWidget(ui->questionPage);
      setNetworkControls();
}

void GameScreen::applyNetworkBoard(const BoardState &board)
{
      m_networkBoard = board;
      if (board.round < 0 ||
          board.round >= static_cast<int>(m_game.rounds.size()))
      {
            return;
      }
      if (m_boardRoundIndex != board.round)
      {
            buildBoard(board.round);
      }
      QSet<QString> used;
      for (const BoardCell &cell : board.cells)
      {
            if (cell.used)
            {
                  used.insert(QStringLiteral("%1:%2:%3")
                                    .arg(cell.round)
                                    .arg(cell.theme)
                                    .arg(cell.question));
            }
      }
      const Round &round =
            m_game.rounds[static_cast<std::size_t>(board.round)];
      const bool finalRound = isFinalRound(board.round);
      for (int theme = 0; theme < static_cast<int>(round.themes.size()); ++theme)
      {
            const Theme &currentTheme =
                  round.themes[static_cast<std::size_t>(theme)];
            for (int question = 0;
                 question < static_cast<int>(currentTheme.questions.size());
                 ++question)
            {
                  auto *button = qobject_cast<QPushButton *>(
                        ui->tableWidget->cellWidget(theme, question));
                  if (button == nullptr)
                  {
                        continue;
                  }
                  const bool isUsed = used.contains(
                        QStringLiteral("%1:%2:%3")
                              .arg(board.round)
                              .arg(theme)
                              .arg(question));
                  button->setText(
                        isUsed
                              ? QString()
                              : (finalRound
                                       ? tr("Eliminate")
                                       : QString::number(
                                               currentTheme.questions
                                                     [static_cast<std::size_t>(
                                                           question)]
                                                           .price)));
                  button->setEnabled(!isUsed &&
                                     m_phase == SessionPhase::PickingQuestion &&
                                     m_pickerId == m_localPlayerId);
            }
      }
}

void GameScreen::applyReactionWinner(const PlayerId &playerId)
{
      for (auto iterator = m_playerGlows.begin();
           iterator != m_playerGlows.end();)
      {
            if (iterator.value() == PlayerGlow::Reaction)
            {
                  iterator = m_playerGlows.erase(iterator);
            }
            else
            {
                  ++iterator;
            }
      }
      m_playerGlows.insert(playerId, PlayerGlow::Reaction);
      rebuildNetworkPlayerCards();
}

QLabel *GameScreen::createAnswerBubble()
{
      auto *bubble = new QLabel(this);
      bubble->setAttribute(Qt::WA_TransparentForMouseEvents);
      bubble->setAlignment(Qt::AlignCenter);
      bubble->setWordWrap(true);
      bubble->setMaximumWidth(220);
      bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
      bubble->setStyleSheet(QStringLiteral(
            "QLabel { background-color: #ffffff; color: #111827; "
            "border: 2px solid #d1d5db; border-radius: 12px; "
            "padding: 8px; }"));
      bubble->hide();
      return bubble;
}

void GameScreen::showNetworkAnswerBubble(const AnswerResult &result)
{
      QLabel *bubble = m_networkAnswerLabels.value(result.playerId);
      const QString answer = displayAnswerText(result.submitted).trimmed();
      if (bubble == nullptr || answer.isEmpty())
      {
            return;
      }
      bubble->setText(answer);
      bubble->setVisible(true);
      positionAnswerBubbles();
}

void GameScreen::positionAnswerBubbles()
{
      const auto position = [this](QLabel *bubble, QLabel *avatar)
      {
            if (bubble == nullptr || avatar == nullptr || !bubble->isVisible())
            {
                  return;
            }
            const int bubbleWidth = std::clamp(avatar->width(), 120, 220);
            bubble->setFixedWidth(bubbleWidth);
            const int bubbleHeight = bubble->heightForWidth(bubbleWidth);
            bubble->resize(bubbleWidth,
                           bubbleHeight > 0 ? bubbleHeight
                                            : bubble->sizeHint().height());
            const QPoint avatarTop = avatar->mapTo(this, QPoint(0, 0));
            const int maximumX = std::max(0, width() - bubble->width());
            const int bubbleX = std::clamp(
                  avatarTop.x() + (avatar->width() - bubble->width()) / 2,
                  0, maximumX);
            const int bubbleY =
                  std::max(0, avatarTop.y() - bubble->height() - 8);
            bubble->move(bubbleX, bubbleY);
            bubble->raise();
      };
      for (const Player &player : m_players)
      {
            position(player.answerBubble, player.avatarLabel);
      }
      for (auto iterator = m_networkAnswerLabels.cbegin();
           iterator != m_networkAnswerLabels.cend(); ++iterator)
      {
            position(iterator.value(),
                     m_networkAvatarLabels.value(iterator.key()));
      }
}

void GameScreen::clearAnswerBubbles()
{
      const auto clear = [](QLabel *bubble)
      {
            if (bubble != nullptr)
            {
                  bubble->clear();
                  bubble->hide();
            }
      };
      for (const Player &player : m_players)
      {
            clear(player.answerBubble);
      }
      for (QLabel *bubble : std::as_const(m_networkAnswerLabels))
      {
            clear(bubble);
      }
}

void GameScreen::applyPlayerGlow(QLabel *avatar, PlayerGlow glow)
{
      if (avatar == nullptr)
      {
            return;
      }
      QColor glowColor(Qt::transparent);
      QString borderColor = QStringLiteral("transparent");
      switch (glow)
      {
      case PlayerGlow::Reaction:
            glowColor = QColor(QStringLiteral("#ffeb3b"));
            break;
      case PlayerGlow::Correct:
            glowColor = QColor(QStringLiteral("#00c853"));
            break;
      case PlayerGlow::Incorrect:
            glowColor = QColor(QStringLiteral("#d50000"));
            break;
      case PlayerGlow::None:
            break;
      }
      if (glow != PlayerGlow::None)
      {
            borderColor = glowColor.name();
      }
      auto *effect = qobject_cast<QGraphicsDropShadowEffect *>(
            avatar->graphicsEffect());
      if (effect == nullptr)
      {
            effect = new QGraphicsDropShadowEffect(avatar);
            effect->setBlurRadius(32.0);
            effect->setOffset(0.0);
            avatar->setGraphicsEffect(effect);
      }
      effect->setColor(glowColor);
      avatar->setStyleSheet(
            QStringLiteral("QLabel { border: 4px solid %1; }")
                  .arg(borderColor));
}

void GameScreen::setSinglePlayerGlow(PlayerGlow glow, bool clearAfterDelay)
{
      if (m_mode != GameScreenMode::SinglePlayer || m_players.empty())
      {
            return;
      }
      Player &player = m_players.front();
      player.glow = glow;
      applyPlayerGlow(player.avatarLabel, glow);
      if (!clearAfterDelay)
      {
            return;
      }
      QTimer::singleShot(
            PlayerResultGlowDuration, this,
            [this, glow]()
            {
                  if (m_mode == GameScreenMode::SinglePlayer &&
                      !m_players.empty() && m_players.front().glow == glow)
                  {
                        m_players.front().glow = PlayerGlow::None;
                        applyPlayerGlow(m_players.front().avatarLabel,
                                        PlayerGlow::None);
                  }
            });
}

void GameScreen::rebuildNetworkPlayerCards()
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      QVector<PlayerId> order;
      order.reserve(m_networkPlayers.size());
      for (const PlayerState &state : m_networkPlayers)
      {
            order.push_back(state.id);
      }
      if (order != m_networkCardOrder)
      {
            clearNetworkPlayerCards();
            m_networkCardOrder = order;
            for (const PlayerState &state : m_networkPlayers)
            {
                  auto *layout = new QVBoxLayout;
                  auto *avatar = new QLabel;
                  avatar->setScaledContents(true);
                  avatar->setProperty("secretTargetId", state.id);
                  avatar->installEventFilter(this);
                  auto *name = new QLabel;
                  auto *balance = new QLabel;
                  QLabel *answerBubble = createAnswerBubble();
                  layout->addWidget(avatar);
                  layout->addWidget(name);
                  layout->addWidget(balance);
                  ui->PlayersLayout->addLayout(layout);
                  m_networkAvatarLabels.insert(state.id, avatar);
                  m_networkNameLabels.insert(state.id, name);
                  m_networkBalanceLabels.insert(state.id, balance);
                  m_networkAnswerLabels.insert(state.id, answerBubble);
            }
      }

      const QPixmap fallback(QStringLiteral(":/Images/default.jpg"));
      for (const PlayerState &state : m_networkPlayers)
      {
            QLabel *avatar = m_networkAvatarLabels.value(state.id);
            QLabel *name = m_networkNameLabels.value(state.id);
            QLabel *balance = m_networkBalanceLabels.value(state.id);
            if (avatar == nullptr || name == nullptr || balance == nullptr)
            {
                  continue;
            }
            if (!m_networkCardProfiles.contains(state.id) ||
                m_networkCardProfiles.value(state.id) != state.profilePng)
            {
                  QPixmap picture = fallback;
                  if (!state.profilePng.isEmpty())
                  {
                        QPixmap remote;
                        if (remote.loadFromData(state.profilePng))
                        {
                              picture = remote;
                        }
                        else
                        {
                              qWarning() << "Unable to load remote profile picture"
                                         << state.id;
                        }
                  }
                  avatar->setPixmap(
                        picture.scaled(200, 200, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
                  m_networkCardProfiles.insert(state.id, state.profilePng);
            }
            applyPlayerGlow(
                  avatar, m_playerGlows.value(state.id, PlayerGlow::None));
            avatar->setCursor(m_secretTargetSelection && state.isPicker &&
                                      state.id != m_localPlayerId
                                    ? Qt::PointingHandCursor
                                    : Qt::ArrowCursor);
            name->setText(state.nickname.isEmpty() ? tr("Unnamed")
                                                   : state.nickname);
            balance->setText(tr("Balance: %1").arg(state.balance));
      }
      QTimer::singleShot(0, this, &GameScreen::positionAnswerBubbles);
}

void GameScreen::clearNetworkPlayerCards()
{
      deleteLayoutItems(ui->PlayersLayout);
      m_networkCardOrder.clear();
      m_networkAvatarLabels.clear();
      m_networkNameLabels.clear();
      m_networkBalanceLabels.clear();
      for (const Player &player : m_players)
      {
            delete player.answerBubble;
      }
      for (QLabel *bubble : std::as_const(m_networkAnswerLabels))
      {
            delete bubble;
      }
      m_networkAnswerLabels.clear();
      m_networkCardProfiles.clear();
      m_players.clear();
}

void GameScreen::setNetworkControls()
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      const PlayerState *local = nullptr;
      for (const PlayerState &state : m_networkPlayers)
      {
            if (state.id == m_localPlayerId)
            {
                  local = &state;
                  break;
            }
      }
      const bool eligible = local != nullptr && local->connected &&
                            !local->hasPassed && !local->answeredIncorrectly;
      const bool appealVoting = m_phase == SessionPhase::AppealVoting;
      m_canAppeal = local != nullptr && local->mayAppeal;
      ui->AnswerBytton->setText(appealVoting ? tr("Yes") : tr("Answer"));
      ui->passButton->setText(appealVoting ? tr("No") : tr("Pass"));
      if (appealVoting)
      {
            m_canAnswer = false;
            m_canPass = false;
            const bool canVote = local != nullptr && local->connected &&
                                 m_appealId != 0 &&
                                 m_appealAppellant != m_localPlayerId &&
                                 !m_appealVoteSubmitted && !m_networkPaused;
            ui->AnswerBytton->setEnabled(canVote);
            ui->passButton->setEnabled(canVote);
      }
      else
      {
            if (m_phase == SessionPhase::ReadingQuestion)
            {
                  m_canAnswer = false;
                  m_canPass = eligible;
            }
            else if (m_phase == SessionPhase::WaitingForReaction)
            {
                  m_canAnswer = eligible;
                  m_canPass = eligible;
            }
            else if (m_phase == SessionPhase::Answering)
            {
                  m_canAnswer = eligible && m_answerOwnerId == m_localPlayerId;
                  m_canPass = eligible && m_answerOwnerId != m_localPlayerId;
            }
            else if (m_phase == SessionPhase::ForAllAnswering)
            {
                  m_canAnswer = eligible &&
                                (local == nullptr || !local->hasAnsweredForAll);
                  m_canPass = m_canAnswer;
            }
            else
            {
                  m_canAnswer = false;
                  m_canPass = false;
            }
            ui->AnswerBytton->setEnabled(
                  m_canAnswer && !m_networkAnswerSubmitted && !m_networkPaused &&
                  !(m_phase == SessionPhase::WaitingForReaction &&
                    m_networkReactionClaimed));
            ui->passButton->setEnabled(m_canPass && !m_networkPaused);
      }
      ui->pushButton_3->setEnabled(m_canPause && !m_networkPaused);
      ui->pushButton_5->setEnabled(m_canPause && m_networkPaused);
      ui->appealButton->setVisible(m_canAppeal &&
                                   m_phase == SessionPhase::ShowingAnswer &&
                                   !m_networkPaused);
      if (m_networkPaused)
      {
            ui->AnswerBytton->setEnabled(false);
            ui->passButton->setEnabled(false);
      }
      if (m_phase == SessionPhase::PickingQuestion)
      {
            applyNetworkBoard(m_networkBoard);
      }
}

void GameScreen::connectHostSignals(MultiplayerHost *host)
{
      GameSession *session = host->session();
      connect(session, &GameSession::playersChanged, this,
              &GameScreen::applyPlayers);
      connect(session, &GameSession::boardChanged, this,
              &GameScreen::applyBoard);
      connect(session, &GameSession::phaseStarted, this,
              &GameScreen::applyPhase);
      connect(session, &GameSession::questionStarted, this,
              &GameScreen::applyQuestion);
      connect(session, &GameSession::reactionOpened, this,
              [this](const ReactionState &) { m_reactionElapsedTimer.restart(); });
      connect(session, &GameSession::reactionWinner, this,
              [this](const PlayerId &playerId, unsigned int)
              { applyReactionWinner(playerId); });
      connect(session, &GameSession::answerOwnerChanged, this,
              &GameScreen::applyAnswerOwner);
      connect(session, &GameSession::answerResult, this,
              [this](const AnswerResult &result)
              { applyAnswerResult(result); });
      connect(session, &GameSession::answerRevealed, this,
              &GameScreen::applyReveal);
      connect(session, &GameSession::forAllResult, this,
              &GameScreen::applyForAllResult);
      connect(session, &GameSession::appealOpened, this,
              &GameScreen::applyAppeal);
      connect(session, &GameSession::appealFinished, this,
              &GameScreen::applyAppealResult);
      connect(session, &GameSession::pickerChanged, this,
              &GameScreen::setPicker);
      connect(session, &GameSession::secretTargetsReady, this,
              [this](quint64, const QVector<PlayerState> &targets)
              { applySecretTargets(targets); });
      connect(session, &GameSession::secretWagerPrompt, this,
              [this](const PlayerId &target,
                     const SecretWagerParameters &parameters)
              {
                    if (target == m_localPlayerId)
                    {
                          applyWagerPrompt(parameters);
                    }
              });
      connect(session, &GameSession::pauseChanged, this,
              [this](bool paused, const PhaseState &state)
              { applyPause(paused, state.phase, state.remainingMs); });
}

void GameScreen::connectClientSignals(MultiplayerClient *client)
{
      connect(client, &MultiplayerClient::connected, this,
              [this](const PlayerId &id, bool) { setLocalPlayerId(id); });
      connect(client, &MultiplayerClient::lobbyChanged, this,
              &GameScreen::applyPlayers);
      connect(client, &MultiplayerClient::phaseReceived, this,
              &GameScreen::applyPhase);
      connect(client, &MultiplayerClient::boardReceived, this,
              &GameScreen::applyBoard);
      connect(client, &MultiplayerClient::questionReceived, this,
              &GameScreen::applyQuestion);
      connect(client, &MultiplayerClient::reactionWinnerReceived, this,
              [this](const PlayerId &playerId, unsigned int)
              { applyReactionWinner(playerId); });
      connect(client, &MultiplayerClient::answerOwnerReceived, this,
              [this](const PlayerId &id, unsigned int) { applyAnswerOwner(id); });
      connect(client, &MultiplayerClient::pickerReceived, this,
              &GameScreen::setPicker);
      connect(client, &MultiplayerClient::answerResultReceived, this,
              [this](const AnswerResult &result)
              { applyAnswerResult(result); });
      connect(client, &MultiplayerClient::forAllResultReceived, this,
              &GameScreen::applyForAllResult);
      connect(client, &MultiplayerClient::revealReceived, this,
              &GameScreen::applyReveal);
      connect(client, &MultiplayerClient::appealReceived, this,
              &GameScreen::applyAppeal);
      connect(client, &MultiplayerClient::appealResultReceived, this,
              &GameScreen::applyAppealResult);
      connect(client, &MultiplayerClient::pauseReceived, this,
              &GameScreen::applyPause);
      connect(client, &MultiplayerClient::secretTargetListReceived, this,
              &GameScreen::applySecretTargets);
      connect(client, &MultiplayerClient::secretWagerPromptReceived, this,
              &GameScreen::applyWagerPrompt);
      connect(client, &MultiplayerClient::snapshotApplied, this,
              &GameScreen::applySnapshot);
}

void GameScreen::emitNetworkAnswer(const AnswerSubmission &submission)
{
      emit answerSubmitted(submission);
}

quint64 GameScreen::nextLocalActionId() { return m_localActionId++; }

void GameScreen::setNetworkPhaseTimer(const PhaseState &phase)
{
      setProgressBarColor(phase.phase);
      if (phase.phase != SessionPhase::WaitingForReaction)
      {
            stopReactionFlash();
      }
      const unsigned int duration =
            phase.phase == SessionPhase::WaitingForReaction &&
                        m_answerWaitDuration > 0
                  ? m_answerWaitDuration
                  : (phase.durationMs > 0 ? phase.durationMs
                                          : phase.remainingMs);
      m_phaseDuration = duration;
      m_globalTimer->restart();
      m_progressAnimation->stop();
      const unsigned int remaining = std::min(phase.remainingMs, duration);
      const int startValue =
            duration == 0
                  ? ui->progressBar->minimum()
                  : ui->progressBar->minimum() +
                          static_cast<int>(
                                (ui->progressBar->maximum() -
                                 ui->progressBar->minimum()) *
                                (static_cast<double>(remaining) / duration));
      m_progressAnimation->setStartValue(startValue);
      m_progressAnimation->setEndValue(ui->progressBar->minimum());
      m_progressAnimation->setDuration(static_cast<int>(phase.remainingMs));
      if (phase.remainingMs > 0)
      {
            m_progressAnimation->start();
      }
      m_tickTimer->stop();
}

void GameScreen::deleteLayoutItems(QLayout *layout)
{
      if (layout == nullptr)
      {
            return;
      }
      while (QLayoutItem *item = layout->takeAt(0))
      {
            if (QWidget *widget = item->widget())
            {
                  delete widget;
            }
            if (QLayout *child = item->layout())
            {
                  deleteLayoutItems(child);
            }
            delete item;
      }
}
