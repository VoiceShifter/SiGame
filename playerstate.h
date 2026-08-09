#ifndef PLAYERSTATE_H
#define PLAYERSTATE_H

#include "gamecontent.h"

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

using PlayerId = QString;
using PlayerToken = QString;

enum class SessionPhase
{
      Lobby,
      PickingQuestion,
      SecretTargetSelection,
      SecretWager,
      FinalWager,
      ReadingQuestion,
      WaitingForReaction,
      Answering,
      ForAllAnswering,
      ShowingAnswer,
      AppealVoting,
      Finished
};

struct PlayerState
{
      PlayerId id;
      PlayerToken token;
      QString nickname;
      QByteArray profilePng;
      int balance{};
      int correctAnswers{};
      int wrongAnswers{};
      bool connected{};
      bool ready{};
      bool isPicker{};
      bool hasPassed{};
      bool answeredIncorrectly{};
      bool hasAnsweredForAll{};
      bool mayAppeal{};
};

struct PlayerQuestionState
{
      quint64 questionSequence{};
      bool passed{};
      bool answeredIncorrectly{};
      bool submitted{};
      bool appealable{};
      int effectiveAmount{};
};

struct BoardCell
{
      int round{};
      int theme{};
      int question{};
      bool used{};
};

struct BoardState
{
      quint64 boardSequence{};
      int round{};
      QVector<BoardCell> cells;
};

struct PhaseState
{
      SessionPhase phase{SessionPhase::Lobby};
      quint64 phaseSequence{};
      quint64 questionSequence{};
      unsigned int durationMs{};
      unsigned int remainingMs{};
      PlayerId owner;
      bool paused{};
};

struct ReactionState
{
      quint64 questionSequence{};
      quint64 phaseSequence{};
      unsigned int durationMs{};
      unsigned int remainingMs{};
};

struct QuestionPresentation
{
      quint64 questionSequence{};
      int round{};
      int theme{};
      int question{};
      QString themeName;
      int price{};
      QuestionType questionType{QuestionType::Default};
      AnswerType answerType{AnswerType::Text};
      QString text;
      MediaType mediaType{MediaType::None};
      QString mediaPath;
      unsigned int mediaDurationMs{};
      unsigned int answerDurationMs{};
      PlayerId answerOwner;
      QVector<AnswerOption> answerOptions;
};

struct AnswerSubmission
{
      AnswerType answerType{AnswerType::Text};
      QString answer;
      QString optionId;
      QPointF point;
      bool hasPoint{};
      QString mode;
};

struct AnswerResult
{
      quint64 questionSequence{};
      PlayerId playerId;
      bool correct{};
      int amount{};
      int balance{};
      AnswerType answerKind{AnswerType::Text};
      QString submitted;
      unsigned int remainingReactionMs{};
      bool retryAllowed{};
};

struct AnswerReveal
{
      quint64 questionSequence{};
      QVector<QString> rightAnswers;
      MediaType answerMediaType{MediaType::None};
      QString answerMediaPath;
      unsigned int mediaDurationMs{};
      PlayerId answerOwner;
      PlayerId nextPicker;
};

struct ForAllResult
{
      quint64 questionSequence{};
      QVector<AnswerResult> results;
      PlayerId nextPicker;
};

struct SecretWagerParameters
{
      int minimum{};
      int maximum{};
      int step{};
      QString theme;
};

struct AppealState
{
      quint64 appealId{};
      quint64 questionSequence{};
      PlayerId appellant;
      QString submitted;
      QVector<QString> rightAnswers;
      MediaType answerMediaType{MediaType::None};
      QString answerMediaPath;
      QVector<PlayerId> voters;
      QHash<PlayerId, bool> votes;
      unsigned int durationMs{};
};

struct AppealResult
{
      quint64 appealId{};
      quint64 questionSequence{};
      bool accepted{};
      PlayerId appellant;
      int correction{};
      int balance{};
      PlayerId nextPicker;
};

struct SessionSnapshot
{
      quint64 snapshotSequence{};
      QString sessionId;
      PhaseState phase;
      BoardState board;
      PlayerId currentPicker;
      PlayerId answerOwner;
      QVector<PlayerState> players;
      bool paused{};
      std::optional<QuestionPresentation> question;
      std::optional<AnswerReveal> reveal;
      std::optional<AppealState> appeal;
};

Q_DECLARE_METATYPE(SessionPhase)
Q_DECLARE_METATYPE(AnswerOption)
Q_DECLARE_METATYPE(QVector<AnswerOption>)
Q_DECLARE_METATYPE(PlayerState)
Q_DECLARE_METATYPE(QVector<PlayerState>)
Q_DECLARE_METATYPE(BoardState)
Q_DECLARE_METATYPE(PhaseState)
Q_DECLARE_METATYPE(ReactionState)
Q_DECLARE_METATYPE(QuestionPresentation)
Q_DECLARE_METATYPE(AnswerSubmission)
Q_DECLARE_METATYPE(AnswerResult)
Q_DECLARE_METATYPE(AnswerReveal)
Q_DECLARE_METATYPE(ForAllResult)
Q_DECLARE_METATYPE(SecretWagerParameters)
Q_DECLARE_METATYPE(AppealState)
Q_DECLARE_METATYPE(AppealResult)
Q_DECLARE_METATYPE(SessionSnapshot)

#endif // PLAYERSTATE_H
