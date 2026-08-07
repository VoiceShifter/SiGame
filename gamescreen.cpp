#include "gamescreen.h"
#include "multiplayerclient.h"
#include "multiplayerhost.h"
#include "ui_gamescreen.h"

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QDialog>
#include <QDir>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
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

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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
        m_answerDuration(static_cast<unsigned int>(AnswerDuration) * 1000U),
        m_questionDuration(static_cast<unsigned int>(QuestionDuration) * 1000U),
        m_questionPickDuration(
              static_cast<unsigned int>(QuestionPickDuration) * 1000U),
        m_answerWaitDuration(
              static_cast<unsigned int>(AnswerWaitDuration) * 1000U)
{
      m_mode = mode;
      ui->setupUi(this);
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
                          if (m_phase == GamePhase::WaitingForReaction)
                          {
                                ui->AnswerBytton->setEnabled(false);
                                ui->passButton->setEnabled(false);
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
                          ui->passButton->setEnabled(false);
                          emit passRequested();
                          return;
                    }
                    if (m_players.empty() || m_currentThemeIndex < 0)
                    {
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
                    if (m_mode != GameScreenMode::SinglePlayer && m_canPause)
                    {
                          emit pauseRequested(true);
                    }
              });
      connect(ui->pushButton_5, &QPushButton::clicked, this,
              [this]()
              {
                    if (m_mode != GameScreenMode::SinglePlayer && m_networkPaused)
                    {
                          emit pauseRequested(false);
                    }
              });
      connect(ui->appealButton, &QPushButton::clicked, this,
              [this]() { emit appealRequested(); });
      connect(ui->appealYesButton, &QPushButton::clicked, this,
              [this]() { emit appealVoteSubmitted(true); });
      connect(ui->appealNoButton, &QPushButton::clicked, this,
              [this]() { emit appealVoteSubmitted(false); });

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
                        const int themeIndex    = static_cast<int>(row);
                        const int questionIndex = static_cast<int>(column);
                        connect(button, &QPushButton::clicked, this,
                                [this, themeIndex, questionIndex]()
                                {
                                      if (m_mode == GameScreenMode::SinglePlayer)
                                      {
                                            emit questionSelected(themeIndex,
                                                                  questionIndex);
                                      }
                                      else
                                      {
                                            emit questionPickRequested(
                                                  0, themeIndex, questionIndex);
                                      }
                                });
                        if (m_mode != GameScreenMode::SinglePlayer)
                        {
                              button->setEnabled(false);
                        }
                        ui->tableWidget->setCellWidget(themeIndex,
                                                       questionIndex, button);
                  }
            }
      }

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
            QLabel *playerName    = new QLabel(playerDisplayName);
            QLabel *balanceLabel  = new QLabel;
            playerLayout->addWidget(playerPfp);
            playerLayout->addWidget(playerName);
            playerLayout->addWidget(balanceLabel);
            playerLayout->setStretch(0, 0);
            ui->PlayersLayout->addLayout(playerLayout);
            m_players.push_back(
                  {playerDisplayName, 0, false, playerPfp, balanceLabel});
            updateBalanceLabel(m_players.back());
      }

      ui->questionMediaLabel->installEventFilter(this);
      ui->answerOptionsContainer->installEventFilter(this);
      ui->questionMediaLabel->hide();
      ui->answerOptionsTable->hide();
      ui->answerOptionsContainer->hide();
      ui->passButton->setEnabled(false);
      ui->appealButton->hide();
      ui->appealYesButton->hide();
      ui->appealNoButton->hide();
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            returnToBoard();
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
                        applyAnswerResult(isCorrect, submittedAnswer);
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
            color = QStringLiteral("#9c27b0");
            break;
      case GamePhase::ReadingQuestion:
            color = QStringLiteral("#2196f3");
            break;
      case GamePhase::WaitingForReaction:
            color = QStringLiteral("#ffeb3b");
            break;
      case GamePhase::Answering:
      case GamePhase::ShowingAnswer:
      case GamePhase::AppealVoting:
            color = QStringLiteral("#f44336");
            break;
      case GamePhase::Lobby:
      case GamePhase::SecretTargetSelection:
      case GamePhase::SecretWager:
      case GamePhase::ForAllAnswering:
      case GamePhase::Finished:
            color = QStringLiteral("#9c27b0");
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
            startReactionFlash();
            ui->AnswerBytton->setEnabled(true);
            startPhaseTimer(GamePhase::WaitingForReaction,
                            m_answerWaitDuration);
            break;
      case GamePhase::WaitingForReaction:
      case GamePhase::Answering:
            showAnswer();
            break;
      case GamePhase::ShowingAnswer:
            returnToBoard();
            break;
      case GamePhase::ForAllAnswering:
      case GamePhase::Lobby:
      case GamePhase::SecretTargetSelection:
      case GamePhase::SecretWager:
      case GamePhase::AppealVoting:
      case GamePhase::Finished:
            break;
      }
}

void GameScreen::showQuestion(int themeIndex, int questionIndex)
{
      if (m_phase != GamePhase::PickingQuestion || m_game.rounds.empty() ||
          themeIndex < 0 || questionIndex < 0)
      {
            return;
      }

      const Round &round = m_game.rounds.front();
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

      const Question &question =
            theme.questions[static_cast<std::size_t>(questionIndex)];
      button->setText(QString());
      button->setEnabled(false);
      m_currentThemeIndex    = themeIndex;
      m_currentQuestionIndex = questionIndex;

      switch (question.type)
      {
      case QuestionType::ForAll:
            emit forAllQuestionSelected(0, themeIndex, questionIndex);
            break;
      case QuestionType::SecretPublicPrice:
            if (question.secretParameters.has_value())
            {
                  const SecretQuestionParameters &parameters =
                        *question.secretParameters;
                  emit secretPublicPriceQuestionSelected(
                        0, themeIndex, questionIndex,
                        parameters.selectionMode, parameters.price.minimum,
                        parameters.price.maximum, parameters.price.step,
                        parameters.theme);
            }
            else
            {
                  qWarning() << "Secret public price question has no metadata";
                  emit secretPublicPriceQuestionSelected(
                        0, themeIndex, questionIndex, QString(), 0, 0, 0,
                        QString());
            }
            break;
      case QuestionType::Default:
            break;
      case QuestionType::Unknown:
            qWarning() << "Unknown question type";
            break;
      }

      resetAnswerInputState();
      displayContent(question.text, question.mediaType, question.mediaPath);
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
      startPhaseTimer(GamePhase::ReadingQuestion, m_questionDuration);
}

void GameScreen::showAnswer()
{
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

      displayContent(answers.join(QLatin1Char('\n')),
                     question.answerMediaType, question.answerMediaPath);
      startPhaseTimer(GamePhase::ShowingAnswer, AnswerRevealDuration);
}

void GameScreen::returnToBoard()
{
      stopReactionFlash();
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
      startPhaseTimer(GamePhase::PickingQuestion, m_questionPickDuration);
}

const Question &GameScreen::currentQuestion() const
{
      if (m_mode != GameScreenMode::SinglePlayer &&
          m_networkQuestion.has_value())
      {
            return *m_networkQuestion;
      }
      return m_game.rounds.front()
            .themes[static_cast<std::size_t>(m_currentThemeIndex)]
            .questions[static_cast<std::size_t>(m_currentQuestionIndex)];
}

void GameScreen::pickRandomQuestion()
{
      std::vector<std::pair<int, int>> availableQuestions;
      if (!m_game.rounds.empty())
      {
            const Round &round = m_game.rounds.front();
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
            m_tickTimer->stop();
            ui->progressBar->setValue(0);
            ui->gameContentStack->setCurrentWidget(ui->boardPage);
            return;
      }

      const int selectedIndex = QRandomGenerator::global()->bounded(
            static_cast<int>(availableQuestions.size()));
      const auto [themeIndex, questionIndex] =
            availableQuestions[static_cast<std::size_t>(selectedIndex)];
      emit questionSelected(themeIndex, questionIndex);
}

void GameScreen::displayContent(const QString &text, MediaType mediaType,
                                const QString &mediaPath)
{
      ui->questionTextLabel->setText(text);
      ui->questionMediaLabel->clear();
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
            }
      }

      if (m_displayedPixmap.isNull())
      {
            ui->questionMediaLabel->hide();
            ui->questionTextLabel->setSizePolicy(QSizePolicy::Expanding,
                                                 QSizePolicy::Expanding);
            ui->questionTextLabel->setAlignment(Qt::AlignCenter);
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
      m_answerDialog->setLabelText(tr("Enter your answer:"));
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
      if (m_phase == GamePhase::Answering && !m_answerResultApplied)
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
      setSinglePlayerGlow(isCorrect ? PlayerGlow::Correct
                                    : PlayerGlow::Incorrect,
                          true);
      Player &player = m_players.front();
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
      ui->appealYesButton->hide();
      ui->appealNoButton->hide();
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
      m_networkPhase = phase;
      m_phase = phase.phase;
      m_networkPhaseSequence = phase.phaseSequence;
      m_networkQuestionSequence = phase.questionSequence;
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
      if (phase.phase == SessionPhase::PickingQuestion)
      {
            m_pickerId = phase.owner;
            ui->gameContentStack->setCurrentWidget(ui->boardPage);
      }
      else if (phase.phase != SessionPhase::Lobby &&
               phase.phase != SessionPhase::SecretTargetSelection &&
               phase.phase != SessionPhase::SecretWager)
      {
            ui->gameContentStack->setCurrentWidget(ui->questionPage);
      }
      if (phase.phase == SessionPhase::WaitingForReaction)
      {
            m_reactionElapsedTimer.restart();
            startReactionFlash();
      }
      else
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
                     reveal.answerMediaType, reveal.answerMediaPath);
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
                  break;
            }
      }
}

void GameScreen::applyAppeal(const AppealState &appeal)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_appealId = appeal.appealId;
      m_appealAppellant = appeal.appellant;
      ui->appealButton->hide();
      ui->appealYesButton->setVisible(appeal.appellant != m_localPlayerId);
      ui->appealNoButton->setVisible(appeal.appellant != m_localPlayerId);
      setNetworkControls();
}

void GameScreen::applyAppealResult(const AppealResult &)
{
      ui->appealButton->hide();
      ui->appealYesButton->hide();
      ui->appealNoButton->hide();
}

void GameScreen::applyPause(bool paused, SessionPhase phase,
                            unsigned int remainingMs)
{
      if (m_mode == GameScreenMode::SinglePlayer)
      {
            return;
      }
      m_networkPaused = paused;
      if (paused)
      {
            m_tickTimer->stop();
            m_progressAnimation->stop();
            ui->gameContentStack->setCurrentWidget(ui->pausePage);
      }
      else
      {
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
      dialog->setWindowTitle(tr("Secret wager"));
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
      m_networkQuestion = Question{};
      m_networkQuestion->price = presentation.price;
      m_networkQuestion->type = presentation.questionType;
      m_networkQuestion->answerType = presentation.answerType;
      m_networkQuestion->answerDuration = presentation.answerDurationMs / 1000U;
      m_networkQuestion->text = presentation.text;
      m_networkQuestion->mediaType = presentation.mediaType;
      m_networkQuestion->mediaPath = presentation.mediaPath;
      m_networkQuestion->answerOptions.assign(presentation.answerOptions.cbegin(),
                                              presentation.answerOptions.cend());
      m_networkQuestionSequence = presentation.questionSequence;
      m_currentThemeIndex = presentation.theme;
      m_currentQuestionIndex = presentation.question;
      m_answerOwnerId = presentation.answerOwner;
      m_secretTargetSelection = false;
      resetAnswerInputState();
      displayContent(presentation.text, presentation.mediaType,
                     presentation.mediaPath);
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
      if (m_game.rounds.empty())
      {
            return;
      }
      const Round &round = m_game.rounds.front();
      for (int theme = 0; theme < static_cast<int>(round.themes.size()); ++theme)
      {
            const Theme &currentTheme = round.themes[static_cast<std::size_t>(theme)];
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
                        QStringLiteral("0:%1:%2").arg(theme).arg(question));
                  button->setText(isUsed
                                        ? QString()
                                        : QString::number(
                                              currentTheme.questions[static_cast<std::size_t>(
                                                    question)]
                                                    .price));
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
      clearNetworkPlayerCards();
      const QPixmap fallback(QStringLiteral("Images/default.jpg"));
      for (const PlayerState &state : m_networkPlayers)
      {
            auto *layout = new QVBoxLayout;
            auto *avatar = new QLabel;
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
            avatar->setScaledContents(true);
            applyPlayerGlow(
                  avatar, m_playerGlows.value(state.id, PlayerGlow::None));
            avatar->setProperty("secretTargetId", state.id);
            avatar->installEventFilter(this);
            avatar->setCursor(m_secretTargetSelection && state.isPicker &&
                                      state.id != m_localPlayerId
                                    ? Qt::PointingHandCursor
                                    : Qt::ArrowCursor);
            auto *name = new QLabel(state.nickname.isEmpty()
                                          ? tr("Unnamed")
                                          : state.nickname);
            auto *balance = new QLabel(tr("Balance: %1").arg(state.balance));
            layout->addWidget(avatar);
            layout->addWidget(name);
            layout->addWidget(balance);
            ui->PlayersLayout->addLayout(layout);
      }
}

void GameScreen::clearNetworkPlayerCards()
{
      deleteLayoutItems(ui->PlayersLayout);
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
      m_canAppeal = local != nullptr && local->mayAppeal;
      if (m_phase == SessionPhase::WaitingForReaction)
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
            m_canAnswer = eligible && (local == nullptr || !local->hasAnsweredForAll);
            m_canPass = m_canAnswer;
      }
      else
      {
            m_canAnswer = false;
            m_canPass = false;
      }
      ui->AnswerBytton->setEnabled(m_canAnswer && !m_networkAnswerSubmitted &&
                                   !m_networkPaused);
      ui->passButton->setEnabled(m_canPass && !m_networkPaused);
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
      if (phase.phase != SessionPhase::WaitingForReaction)
      {
            stopReactionFlash();
      }
      m_phaseDuration = phase.remainingMs;
      m_globalTimer->restart();
      m_progressAnimation->stop();
      m_progressAnimation->setStartValue(ui->progressBar->maximum());
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
