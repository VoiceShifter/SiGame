#ifndef GAMECONTENT_H
#define GAMECONTENT_H

#include <QString>

#include <array>
#include <cstddef>
#include <vector>

const std::size_t QuestionFlagCount = 32;

enum class QuestionFlag : std::size_t
{
      WaitForFinish = 0,
      MediaIsReference,
      MediaPlacementBackground,
      ForAll,
      SecretPublicPrice,
      Count
};

static_assert(static_cast<std::size_t>(QuestionFlag::Count) <=
                    QuestionFlagCount,
              "QuestionFlagCount is too small for known question flags");

enum class MediaType
{
      None,
      Image,
      Audio,
      Video
};

enum class AnswerType
{
      Text,
      Select,
      Point,
      Unknown
};

struct AnswerOption
{
      QString id;
      QString text;
};

struct Question
{
      signed int price{};
      std::size_t answerDuration{};
      AnswerType answerType{AnswerType::Text};
      std::vector<AnswerOption> answerOptions;
      QString text;
      MediaType mediaType{MediaType::None};
      QString mediaPath{};
      std::size_t mediaDuration{0};
      MediaType answerMediaType{MediaType::None};
      QString answerMediaPath{};
      std::size_t answerMediaDuration{};
      std::array<bool, QuestionFlagCount> flags{};
      std::vector<QString> rightAnswers;
      std::vector<QString> wrongAnswers;
};

struct Theme
{
      QString name;
      std::vector<Question> questions;
};

struct Round
{
      QString name;
      QString type;
      std::vector<Theme> themes;
};

struct Game
{
      QString name;
      std::vector<QString> tags;
      std::vector<QString> mediaFiles;
      std::vector<Round> rounds;
};

inline void setQuestionFlag(Question &question, QuestionFlag flag,
                            bool value = true)
{
      question.flags[static_cast<std::size_t>(flag)] = value;
}

inline bool questionHasFlag(const Question &question, QuestionFlag flag)
{
      return question.flags[static_cast<std::size_t>(flag)];
}

bool parseGameContent(const QString &contentXmlPath, Game *game,
                      QString *errorMessage = nullptr);
void printGameContent(const Game &game);

#endif // GAMECONTENT_H
