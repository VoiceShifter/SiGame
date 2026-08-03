#include "gamescreen.h"
#include "ui_gamescreen.h"

#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QInputDialog>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <utility>
#include <vector>

GameScreen::GameScreen(signed int PlayerCount, const QString &GamepackPath,
                       const QString &ProfilePicturePath,
                       const QString &Nickname, int AnswerDuration,
                       int QuestionDuration,
                       int QuestionPickDuration, int AnswerWaitDuration,
                       QWidget *parent)
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
      ui->setupUi(this);
      ui->progressBar->setRange(0, 1000);
      m_progressAnimation->setTargetObject(ui->progressBar);
      m_progressAnimation->setPropertyName("value");
      m_progressAnimation->setEasingCurve(QEasingCurve::Linear);

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
                    if (m_phase != GamePhase::WaitingForReaction)
                    {
                          return;
                    }

                    ui->AnswerBytton->setEnabled(false);
                    ui->passButton->setEnabled(false);
                    stopReactionFlash();
                    const Question &question =
                          m_game.rounds.front()
                                .themes[static_cast<std::size_t>(
                                      m_currentThemeIndex)]
                                .questions[static_cast<std::size_t>(
                                      m_currentQuestionIndex)];
                    m_answerResultApplied = false;
                    const unsigned int duration =
                          question.answerDuration > 0
                                ? static_cast<unsigned int>(
                                        question.answerDuration) *
                                        1000U
                                : m_answerDuration;
                    startPhaseTimer(GamePhase::Answering, duration);
                    openAnswerDialog();
              });
      connect(ui->passButton, &QPushButton::clicked, this,
              [this]()
              {
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

      m_questionFrameStyleSheet = ui->questionFrame->styleSheet();
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
                                ? QStringLiteral(
                                        "QFrame#questionFrame { border: 5px "
                                        "solid yellow; }")
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
                                      emit questionSelected(themeIndex,
                                                            questionIndex);
                                });
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
                  {playerDisplayName, 0, false, balanceLabel});
            updateBalanceLabel(m_players.back());
      }

      ui->questionMediaLabel->hide();
      ui->passButton->setEnabled(false);
      returnToBoard();
}

GameScreen::~GameScreen()
{
      delete m_globalTimer;
      delete ui;
}

void GameScreen::resizeEvent(QResizeEvent *event)
{
      QWidget::resizeEvent(event);
      fitDisplayedPixmap();
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
            color = QStringLiteral("#f44336");
            break;
      }
      ui->progressBar->setStyleSheet(
            QStringLiteral("QProgressBar::chunk { background-color: %1; }")
                  .arg(color));
}

void GameScreen::updateTimerProgress()
{
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

      button->setText(QString());
      button->setEnabled(false);
      m_currentThemeIndex    = themeIndex;
      m_currentQuestionIndex = questionIndex;
      const Question &question =
            theme.questions[static_cast<std::size_t>(questionIndex)];
      displayContent(question.text, question.mediaType, question.mediaPath);
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
      if (m_answerDialog != nullptr && m_answerDialog->isVisible())
      {
            m_answerDialog->reject();
      }
      stopReactionFlash();
      const Question &question =
            m_game.rounds.front()
                  .themes[static_cast<std::size_t>(m_currentThemeIndex)]
                  .questions[static_cast<std::size_t>(m_currentQuestionIndex)];
      QStringList answers;
      for (const QString &answer : question.rightAnswers)
      {
            answers.push_back(answer);
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
      if (m_answerDialog != nullptr && m_answerDialog->isVisible())
      {
            m_answerDialog->reject();
      }
      ui->questionTextLabel->clear();
      ui->questionMediaLabel->clear();
      ui->questionMediaLabel->hide();
      m_displayedPixmap = {};
      m_currentThemeIndex = -1;
      m_currentQuestionIndex = -1;
      ui->gameContentStack->setCurrentWidget(ui->boardPage);
      startPhaseTimer(GamePhase::PickingQuestion, m_questionPickDuration);
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
      ui->questionMediaLabel->setPixmap(
            m_displayedPixmap.scaled(maximumImageSize, Qt::KeepAspectRatio,
                                    Qt::SmoothTransformation));
}

void GameScreen::startReactionFlash()
{
      stopReactionFlash();
      m_flashStep = 0;
      ui->questionFrame->setStyleSheet(QStringLiteral(
            "QFrame#questionFrame { border: 5px solid yellow; }"));
      m_flashTimer->start(120);
}

void GameScreen::stopReactionFlash()
{
      m_flashTimer->stop();
      m_flashStep = 0;
      ui->questionFrame->setStyleSheet(m_questionFrameStyleSheet);
}

void GameScreen::openAnswerDialog()
{
      if (m_answerDialog != nullptr)
      {
            m_answerDialog->reject();
      }

      m_answerDialog = new QInputDialog(this);
      m_answerDialog->setAttribute(Qt::WA_DeleteOnClose);
      m_answerDialog->setInputMode(QInputDialog::TextInput);
      m_answerDialog->setWindowTitle(tr("Answer question"));
      m_answerDialog->setLabelText(tr("Enter your answer:"));
      connect(m_answerDialog, &QInputDialog::textValueSelected, this,
              &GameScreen::handleSubmittedAnswer);
      connect(m_answerDialog, &QDialog::rejected, this,
              &GameScreen::handleAnswerDeclined);
      m_answerDialog->open();
}

void GameScreen::handleSubmittedAnswer(const QString &answer)
{
      if (m_phase != GamePhase::Answering || m_players.empty())
      {
            return;
      }

      const Question &question =
            m_game.rounds.front()
                  .themes[static_cast<std::size_t>(m_currentThemeIndex)]
                  .questions[static_cast<std::size_t>(m_currentQuestionIndex)];
      const QString normalizedAnswer = answer.trimmed();
      const bool isCorrect = std::any_of(
            question.rightAnswers.cbegin(), question.rightAnswers.cend(),
            [&normalizedAnswer](const QString &rightAnswer)
            {
                  return normalizedAnswer.compare(rightAnswer.trimmed(),
                                                  Qt::CaseInsensitive) == 0;
            });

      Player &player = m_players.front();
      if (isCorrect)
      {
            player.balance += question.price;
            m_answerResultApplied = true;
            updateBalanceLabel(player);
            m_answerDialog = nullptr;
            showAnswer();
            return;
      }

      applyIncorrectAnswerPenalty();
      emit incorrectAnswerSubmitted(0, answer);
}

void GameScreen::handleAnswerDeclined()
{
      if (m_phase == GamePhase::Answering && !m_answerResultApplied)
      {
            applyIncorrectAnswerPenalty();
      }
}

void GameScreen::applyIncorrectAnswerPenalty()
{
      const Question &question =
            m_game.rounds.front()
                  .themes[static_cast<std::size_t>(m_currentThemeIndex)]
                  .questions[static_cast<std::size_t>(m_currentQuestionIndex)];
      Player &player = m_players.front();
      player.balance -= question.price;
      m_answerResultApplied = true;
      updateBalanceLabel(player);
}

void GameScreen::updateBalanceLabel(Player &player)
{
      player.balanceLabel->setText(tr("Balance: %1").arg(player.balance));
}
