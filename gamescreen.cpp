#include "gamescreen.h"
#include "ui_gamescreen.h"

#include <QColor>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFontMetrics>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScreen>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
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
                    if (m_phase != GamePhase::Answering ||
                        m_answerResultApplied ||
                        currentQuestion().answerType != AnswerType::Select)
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
                    openAnswerInput();
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

      ui->questionMediaLabel->installEventFilter(this);
      ui->answerOptionsContainer->installEventFilter(this);
      ui->questionMediaLabel->hide();
      ui->answerOptionsTable->hide();
      ui->answerOptionsContainer->hide();
      ui->passButton->setEnabled(false);
      returnToBoard();
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
      if (watched == ui->questionMediaLabel &&
          event->type() == QEvent::MouseButtonPress &&
          m_phase == GamePhase::Answering && m_pointInputEnabled &&
          !m_displayedPixmapRect.isEmpty())
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

                  const double dx =
                        (x - m_correctPoint->x()) *
                        m_correctPointAspectRatio;
                  const double dy = y - m_correctPoint->y();
                  const double allowedDeviation =
                        std::max(0.02, currentQuestion().answerDeviation);
                  const bool isCorrect =
                        std::hypot(dx, dy) <= allowedDeviation;
                  const QString submittedAnswer =
                        QStringLiteral("%1,%2")
                              .arg(x, 0, 'f', 6)
                              .arg(y, 0, 'f', 6);
                  applyAnswerResult(isCorrect, submittedAnswer);
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
            applyAnswerResult(false, QString());
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
      if (!m_correctPoint.has_value() || m_displayedPixmap.isNull())
      {
            qWarning() << "Point input cannot be enabled";
            return;
      }
      m_pointInputEnabled = true;
      ui->questionMediaLabel->setCursor(Qt::CrossCursor);
}

void GameScreen::handleSubmittedAnswer(const QString &answer)
{
      if (m_phase != GamePhase::Answering || m_players.empty())
      {
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
      emit incorrectAnswerSubmitted(0, submittedAnswer);
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
      clearAnswerOptions();
      ui->questionTextLabel->setMinimumHeight(0);
      ui->questionTextLabel->setMaximumHeight(QWIDGETSIZE_MAX);
      m_pointInputEnabled = false;
      m_correctPoint.reset();
      m_submittedPoint.reset();
      m_correctPointAspectRatio = 1.0;
      m_displayedPixmapRect = {};
      ui->questionMediaLabel->setCursor(Qt::ArrowCursor);
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
