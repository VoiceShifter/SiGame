#include "gamesession.h"

#include <QtTest>

class SecretQuestionFlowTest : public QObject
{
      Q_OBJECT

    private:
      static Game gameWithQuestionType(QuestionType type)
      {
            Question question;
            question.price = 700;
            question.type = type;
            question.secretParameters = SecretQuestionParameters{
                  QStringLiteral("exceptCurrent"), {100, 500, 100},
                  QStringLiteral("Secret theme")};
            question.text = QStringLiteral("Question");
            question.rightAnswers.push_back(QStringLiteral("Answer"));

            Theme theme;
            theme.name = QStringLiteral("Board theme");
            theme.questions.push_back(question);
            Round round;
            round.name = QStringLiteral("Round");
            round.themes.push_back(theme);
            Game game;
            game.name = QStringLiteral("Test");
            game.rounds.push_back(round);
            return game;
      }

      static GameConfig config()
      {
            GameConfig config;
            config.maxPlayers = 2;
            config.answerDurationMs = 10000;
            config.questionDurationMs = 10000;
            config.questionPickDurationMs = 10000;
            return config;
      }

      static void addPlayers(GameSession &session)
      {
            PlayerState first;
            first.id = QStringLiteral("p1");
            first.connected = true;
            first.ready = true;
            QVERIFY(session.addPlayer(first));

            PlayerState second;
            second.id = QStringLiteral("p2");
            second.connected = true;
            second.ready = true;
            QVERIFY(session.addPlayer(second));
      }

      static PlayerId otherPlayer(const PlayerId &picker)
      {
            return picker == QStringLiteral("p1") ? QStringLiteral("p2")
                                                   : QStringLiteral("p1");
      }

      static void recordEvents(GameSession &session, QStringList &events,
                               QuestionType &presentedType)
      {
            connect(&session, &GameSession::phaseStarted, &session,
                    [&events](const PhaseState &phase)
                    {
                          if (phase.phase == SessionPhase::SecretTargetSelection)
                          {
                                events.push_back(QStringLiteral("target-phase"));
                          }
                          else if (phase.phase == SessionPhase::SecretWager)
                          {
                                events.push_back(QStringLiteral("wager-phase"));
                          }
                          else if (phase.phase == SessionPhase::ReadingQuestion)
                          {
                                events.push_back(QStringLiteral("reading-phase"));
                          }
                    });
            connect(&session, &GameSession::secretInformationReady, &session,
                    [&events](const SecretWagerParameters &)
                    { events.push_back(QStringLiteral("info")); });
            connect(&session, &GameSession::secretTargetsReady, &session,
                    [&events](quint64, const QVector<PlayerState> &)
                    { events.push_back(QStringLiteral("targets")); });
            connect(&session, &GameSession::secretWagerPrompt, &session,
                    [&events](const PlayerId &,
                              const SecretWagerParameters &)
                    { events.push_back(QStringLiteral("wager-prompt")); });
            connect(&session, &GameSession::secretReady, &session,
                    [&events](quint64, const PlayerId &)
                    { events.push_back(QStringLiteral("ready")); });
            connect(&session, &GameSession::questionStarted, &session,
                    [&events, &presentedType](
                          const QuestionPresentation &presentation)
                    {
                          presentedType = presentation.questionType;
                          events.push_back(QStringLiteral("question"));
                    });
      }

    private slots:
      void ordersPrivateAndPublicSecretInformation()
      {
            GameSession secretSession(
                  gameWithQuestionType(QuestionType::Secret), config());
            GameSession publicSession(
                  gameWithQuestionType(QuestionType::SecretPublicPrice),
                  config());
            addPlayers(secretSession);
            addPlayers(publicSession);

            QStringList secretEvents;
            QStringList publicEvents;
            QuestionType secretPresentation = QuestionType::Unknown;
            QuestionType publicPresentation = QuestionType::Unknown;
            recordEvents(secretSession, secretEvents, secretPresentation);
            recordEvents(publicSession, publicEvents, publicPresentation);

            secretSession.startGame();
            publicSession.startGame();
            QTRY_VERIFY_WITH_TIMEOUT(
                  secretSession.phase() == SessionPhase::PickingQuestion,
                  3500);
            QTRY_VERIFY_WITH_TIMEOUT(
                  publicSession.phase() == SessionPhase::PickingQuestion,
                  3500);
            secretEvents.clear();
            publicEvents.clear();

            const PlayerId secretPicker = secretSession.currentPicker();
            const PlayerId publicPicker = publicSession.currentPicker();
            secretSession.selectQuestion(
                  secretPicker, 0, 0, 0,
                  secretSession.nextActionId(secretPicker));
            publicSession.selectQuestion(
                  publicPicker, 0, 0, 0,
                  publicSession.nextActionId(publicPicker));

            QCOMPARE(secretEvents,
                     QStringList({QStringLiteral("target-phase"),
                                  QStringLiteral("targets")}));
            QCOMPARE(publicEvents,
                     QStringList({QStringLiteral("info"),
                                  QStringLiteral("target-phase"),
                                  QStringLiteral("targets")}));

            secretEvents.clear();
            publicEvents.clear();
            const PlayerId secretTarget = otherPlayer(secretPicker);
            const PlayerId publicTarget = otherPlayer(publicPicker);
            secretSession.selectSecretTarget(
                  secretPicker, secretTarget,
                  secretSession.questionSequence(),
                  secretSession.nextActionId(secretPicker));
            publicSession.selectSecretTarget(
                  publicPicker, publicTarget,
                  publicSession.questionSequence(),
                  publicSession.nextActionId(publicPicker));

            QCOMPARE(secretEvents,
                     QStringList({QStringLiteral("info"),
                                  QStringLiteral("wager-phase"),
                                  QStringLiteral("wager-prompt")}));
            QCOMPARE(publicEvents,
                     QStringList({QStringLiteral("wager-phase"),
                                  QStringLiteral("wager-prompt")}));

            secretEvents.clear();
            publicEvents.clear();
            secretSession.submitSecretWager(
                  secretTarget, 300, secretSession.questionSequence(),
                  secretSession.nextActionId(secretTarget));
            publicSession.submitSecretWager(
                  publicTarget, 300, publicSession.questionSequence(),
                  publicSession.nextActionId(publicTarget));

            const QStringList questionOrder{
                  QStringLiteral("ready"), QStringLiteral("question"),
                  QStringLiteral("reading-phase")};
            QCOMPARE(secretEvents, questionOrder);
            QCOMPARE(publicEvents, questionOrder);
            QCOMPARE(secretPresentation, QuestionType::Secret);
            QCOMPARE(publicPresentation, QuestionType::SecretPublicPrice);
      }
};

QTEST_GUILESS_MAIN(SecretQuestionFlowTest)

#include "test_secretquestionflow.moc"
