#include "gamecontent.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QXmlStreamReader>

#include <utility>

namespace
{

QString attributeValue(const QXmlStreamAttributes &attributes,
                       const QString &name)
{
      for (const auto &attribute : attributes)
      {
            if (attribute.name().toString() == name)
            {
                  return attribute.value().toString();
            }
      }
      return {};
}

bool hasAttribute(const QXmlStreamAttributes &attributes, const QString &name)
{
      for (const auto &attribute : attributes)
      {
            if (attribute.name().toString() == name)
            {
                  return true;
            }
      }

      return false;
}

bool parseBoolean(const QString &value)
{
      const QString normalized = value.trimmed().toLower();
      return normalized == QStringLiteral("true") ||
             normalized == QStringLiteral("1") ||
             normalized == QStringLiteral("yes");
}

MediaType mediaTypeFromString(const QString &value)
{
      if (value == QStringLiteral("image"))
      {
            return MediaType::Image;
      }
      if (value == QStringLiteral("audio"))
      {
            return MediaType::Audio;
      }
      if (value == QStringLiteral("video"))
      {
            return MediaType::Video;
      }
      return MediaType::None;
}

AnswerType answerTypeFromString(const QString &value)
{
      if (value.isEmpty())
      {
            return AnswerType::Text;
      }
      if (value == QStringLiteral("select"))
      {
            return AnswerType::Select;
      }
      if (value == QStringLiteral("point"))
      {
            return AnswerType::Point;
      }
      return AnswerType::Unknown;
}

void appendText(QString &target, const QString &text)
{
      if (text.isEmpty())
      {
            return;
      }
      if (!target.isEmpty())
      {
            target += QLatin1Char('\n');
      }
      target += text;
}

void setQuestionTypeFlag(Question &question, QString Value)
{
      if (Value == QStringLiteral("forAll"))
      {
            setQuestionFlag(question, QuestionFlag::ForAll);
      }
      else if (Value == QStringLiteral("secretPublicPrice"))
      {
            setQuestionFlag(question, QuestionFlag::SecretPublicPrice);
      }
}

void parseItemAttributes(Question &question,
                         const QXmlStreamAttributes &attributes)
{
      if (hasAttribute(attributes, QStringLiteral("waitForFinish")))
      {
            setQuestionFlag(
                  question, QuestionFlag::WaitForFinish,
                  parseBoolean(attributeValue(
                        attributes, QStringLiteral("waitForFinish"))));
      }

      if (parseBoolean(attributeValue(attributes, QStringLiteral("isRef"))))
      {
            setQuestionFlag(question, QuestionFlag::MediaIsReference);
      }

      if (attributeValue(attributes, QStringLiteral("placement")) ==
          QStringLiteral("background"))
      {
            setQuestionFlag(question, QuestionFlag::MediaPlacementBackground);
      }
}

QString resolveMediaReference(const Game &game, const QString &media)
{
      for (const auto &indexedMedia : game.mediaFiles)
      {
            if (indexedMedia == media ||
                indexedMedia.endsWith(QStringLiteral("/") + media))
            {
                  return indexedMedia;
            }
      }
      return media;
}

void storeMedia(Question &question, const QString &paramName,
                MediaType mediaType, const QString &media, std::size_t duration)
{
      if (mediaType == MediaType::None || media.isEmpty())
      {
            return;
      }

      if (paramName == QStringLiteral("question") &&
          question.mediaPath.isEmpty())
      {
            question.mediaPath     = media;
            question.mediaType     = mediaType;
            question.mediaDuration = duration;
      }
      else if (paramName == QStringLiteral("answer") &&
               question.answerMediaPath.isEmpty())
      {
            question.answerMediaPath     = media;
            question.answerMediaType     = mediaType;
            question.answerMediaDuration = duration;
      }
}

QString currentParamName(const std::vector<QString> &paramStack)
{
      if (paramStack.empty())
      {
            return {};
      }
      return paramStack.back();
}

QString mediaTypeName(MediaType mediaType)
{
      switch (mediaType)
      {
      case MediaType::None:
            return QStringLiteral("None");
      case MediaType::Image:
            return QStringLiteral("Image");
      case MediaType::Audio:
            return QStringLiteral("Audio");
      case MediaType::Video:
            return QStringLiteral("Video");
      }
      return QStringLiteral("Unknown");
}

QString answerTypeName(AnswerType answerType)
{
      switch (answerType)
      {
      case AnswerType::Text:
            return QStringLiteral("Text");
      case AnswerType::Select:
            return QStringLiteral("Select");
      case AnswerType::Point:
            return QStringLiteral("Point");
      case AnswerType::Unknown:
            return QStringLiteral("Unknown");
      }
      return QStringLiteral("Unknown");
}

QString flagName(std::size_t index)
{
      switch (static_cast<QuestionFlag>(index))
      {
      case QuestionFlag::WaitForFinish:
            return QStringLiteral("WaitForFinish");
      case QuestionFlag::MediaIsReference:
            return QStringLiteral("MediaIsReference");
      case QuestionFlag::MediaPlacementBackground:
            return QStringLiteral("MediaPlacementBackground");
      case QuestionFlag::ForAll:
            return QStringLiteral("ForAll");
      case QuestionFlag::SecretPublicPrice:
            return QStringLiteral("SecretPublicPrice");
      case QuestionFlag::Count:
            break;
      }
      return QStringLiteral("ReservedFlag%1").arg(index);
}

} // namespace

void printGameContent(const Game &game)
{
      const QString logPath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("log.log"));
      QFile logFile(logPath);
      if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text |
                        QIODevice::Truncate))
      {
            return;
      }

      QTextStream out(&logFile);
      out << "========== Parsed Game ==========" << '\n';
      out << "Log file: " << logPath << '\n';
      out << "Game.name: " << game.name << '\n';

      out << "Tags count: " << game.tags.size() << '\n';
      for (std::size_t tagIndex = 0; tagIndex < game.tags.size(); ++tagIndex)
      {
            out << QStringLiteral("  Tag[%1]: %2")
                         .arg(tagIndex)
                         .arg(game.tags[tagIndex])
                << '\n';
      }

      out << "Media files count: " << game.mediaFiles.size() << '\n';
      for (std::size_t mediaIndex = 0; mediaIndex < game.mediaFiles.size();
           ++mediaIndex)
      {
            out << QStringLiteral("  MediaFile[%1]: %2")
                         .arg(mediaIndex)
                         .arg(game.mediaFiles[mediaIndex])
                << '\n';
      }
      out << "Rounds count: " << game.rounds.size() << '\n';
      for (std::size_t roundIndex = 0; roundIndex < game.rounds.size();
           ++roundIndex)
      {
            const Round &round = game.rounds[roundIndex];
            out << QStringLiteral("  Round[%1].name: %2")
                         .arg(roundIndex)
                         .arg(round.name)
                << '\n';
            out << QStringLiteral("  Round[%1].type: %2")
                         .arg(roundIndex)
                         .arg(round.type)
                << '\n';
            out << QStringLiteral("  Round[%1].themes count: %2")
                         .arg(roundIndex)
                         .arg(round.themes.size())
                << '\n';

            for (std::size_t themeIndex = 0; themeIndex < round.themes.size();
                 ++themeIndex)
            {
                  const Theme &theme = round.themes[themeIndex];
                  out << QStringLiteral("    Theme[%1].name: %2")
                               .arg(themeIndex)
                               .arg(theme.name)
                      << '\n';
                  out << QStringLiteral("    Theme[%1].questions count: %2")
                               .arg(themeIndex)
                               .arg(theme.questions.size())
                      << '\n';

                  for (std::size_t questionIndex = 0;
                       questionIndex < theme.questions.size(); ++questionIndex)
                  {
                        const Question &question =
                              theme.questions[questionIndex];
                        out << QStringLiteral("      Question[%1].price: %2")
                                     .arg(questionIndex)
                                     .arg(question.price)
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerDuration: %2")
                                     .arg(questionIndex)
                                     .arg(question.answerDuration)
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerType: %2")
                                     .arg(questionIndex)
                                     .arg(answerTypeName(question.answerType))
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerOptions count: "
                                     "%2")
                                     .arg(questionIndex)
                                     .arg(question.answerOptions.size())
                            << '\n';
                        for (std::size_t optionIndex = 0;
                             optionIndex < question.answerOptions.size();
                             ++optionIndex)
                        {
                              const AnswerOption &option =
                                    question.answerOptions[optionIndex];
                              out << QStringLiteral(
                                           "        answerOptions[%1] %2: %3")
                                           .arg(optionIndex)
                                           .arg(option.id)
                                           .arg(option.text)
                                  << '\n';
                        }
                        out << QStringLiteral("      Question[%1].text: %2")
                                     .arg(questionIndex)
                                     .arg(question.text)
                            << '\n';
                        out << QStringLiteral("      Question[%1].media: %2")
                                     .arg(questionIndex)
                                     .arg(question.mediaPath)
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].mediaType: %2")
                                     .arg(questionIndex)
                                     .arg(mediaTypeName(question.mediaType))
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].mediaDuration: %2")
                                     .arg(questionIndex)
                                     .arg(question.mediaDuration)
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerMedia: %2")
                                     .arg(questionIndex)
                                     .arg(question.answerMediaPath)
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerMediaType: %2")
                                     .arg(questionIndex)
                                     .arg(mediaTypeName(
                                           question.answerMediaType))
                            << '\n';
                        out << QStringLiteral(
                                     "      Question[%1].answerMediaDuration: "
                                     "%2")
                                     .arg(questionIndex)
                                     .arg(question.answerMediaDuration)
                            << '\n';

                        out << QStringLiteral("      Question[%1].flags:")
                                     .arg(questionIndex)
                            << '\n';
                        for (std::size_t flagIndex = 0;
                             flagIndex < question.flags.size(); ++flagIndex)
                        {
                              out << QStringLiteral("        flags[%1] %2 = %3")
                                           .arg(flagIndex)
                                           .arg(flagName(flagIndex))
                                           .arg(question.flags[flagIndex]
                                                      ? QStringLiteral("true")
                                                      : QStringLiteral("false"))
                                  << '\n';
                        }

                        out << QStringLiteral("      Question[%1].rightAnswers "
                                              "count: %2")
                                     .arg(questionIndex)
                                     .arg(question.rightAnswers.size())
                            << '\n';
                        for (std::size_t answerIndex = 0;
                             answerIndex < question.rightAnswers.size();
                             ++answerIndex)
                        {
                              out << QStringLiteral(
                                           "        rightAnswers[%1]: %2")
                                           .arg(answerIndex)
                                           .arg(question.rightAnswers
                                                      [answerIndex])
                                  << '\n';
                        }

                        out << QStringLiteral("      Question[%1].wrongAnswers "
                                              "count: %2")
                                     .arg(questionIndex)
                                     .arg(question.wrongAnswers.size())
                            << '\n';
                        for (std::size_t answerIndex = 0;
                             answerIndex < question.wrongAnswers.size();
                             ++answerIndex)
                        {
                              out << QStringLiteral(
                                           "        wrongAnswers[%1]: %2")
                                           .arg(answerIndex)
                                           .arg(question.wrongAnswers
                                                      [answerIndex])
                                  << '\n';
                        }
                  }
            }
      }
      out << "======== End Parsed Game ========" << '\n';
}

bool parseGameContent(const QString &contentXmlPath, Game *game,
                      QString *errorMessage)
{
      if (game == nullptr)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Game output pointer is null");
            }
            return false;
      }

      QFile file(contentXmlPath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Cannot open content.xml: %1")
                                        .arg(file.errorString());
            }
            return false;
      }

      Game parsedGame;
      QXmlStreamReader xml(&file);

      Round *currentRound       = nullptr;
      Theme *currentTheme       = nullptr;
      Question *currentQuestion = nullptr;
      std::vector<QString> paramStack;
      std::vector<QString> paramTextStack;
      bool insideRightAnswers = false;
      bool insideWrongAnswers = false;

      while (!xml.atEnd())
      {
            xml.readNext();

            if (xml.isStartElement())
            {
                  const QString elementName             = xml.name().toString();
                  const QXmlStreamAttributes attributes = xml.attributes();

                  if (elementName == QStringLiteral("package"))
                  {
                        parsedGame.name = attributeValue(
                              attributes, QStringLiteral("name"));
                  }
                  else if (elementName == QStringLiteral("tag"))
                  {
                        parsedGame.tags.push_back(
                              xml.readElementText().trimmed());
                  }
                  else if (elementName == QStringLiteral("file"))
                  {
                        parsedGame.mediaFiles.push_back(attributeValue(
                              attributes, QStringLiteral("name")));
                  }
                  else if (elementName == QStringLiteral("round"))
                  {
                        parsedGame.rounds.push_back({});
                        currentRound       = &parsedGame.rounds.back();
                        currentRound->name = attributeValue(
                              attributes, QStringLiteral("name"));
                        currentRound->type = attributeValue(
                              attributes, QStringLiteral("type"));
                  }
                  else if (elementName == QStringLiteral("theme") &&
                           currentRound != nullptr)
                  {
                        currentRound->themes.push_back({});
                        currentTheme       = &currentRound->themes.back();
                        currentTheme->name = attributeValue(
                              attributes, QStringLiteral("name"));
                  }
                  else if (elementName == QStringLiteral("question") &&
                           currentTheme != nullptr)
                  {
                        currentTheme->questions.push_back({});
                        currentQuestion = &currentTheme->questions.back();
                        currentQuestion->price =
                              attributeValue(attributes,
                                             QStringLiteral("price"))
                                    .toInt();

                        setQuestionTypeFlag(
                              *currentQuestion,
                              attributeValue(attributes,
                                             QStringLiteral("type")));
                  }
                  else if (elementName == QStringLiteral("param"))
                  {
                        const QString paramName = attributeValue(
                              attributes, QStringLiteral("name"));
                        if (currentQuestion != nullptr &&
                            !paramStack.empty() &&
                            paramStack.back() ==
                                  QStringLiteral("answerOptions"))
                        {
                              currentQuestion->answerOptions.push_back(
                                    {paramName, {}});
                        }
                        paramStack.push_back(paramName);
                        paramTextStack.push_back({});
                  }
                  else if (elementName == QStringLiteral("right"))
                  {
                        insideRightAnswers = true;
                  }
                  else if (elementName == QStringLiteral("wrong"))
                  {
                        insideWrongAnswers = true;
                  }
                  else if (elementName == QStringLiteral("answer") &&
                           currentQuestion != nullptr)
                  {
                        const QString answer = xml.readElementText().trimmed();
                        if (insideRightAnswers)
                        {
                              currentQuestion->rightAnswers.push_back(answer);
                        }
                        else if (insideWrongAnswers)
                        {
                              currentQuestion->wrongAnswers.push_back(answer);
                        }
                  }
                  else if (elementName == QStringLiteral("item") &&
                           currentQuestion != nullptr)
                  {
                        parseItemAttributes(*currentQuestion, attributes);

                        const QString paramName = currentParamName(paramStack);
                        const MediaType mediaType =
                              mediaTypeFromString(attributeValue(
                                    attributes, QStringLiteral("type")));
                        const bool isReference = parseBoolean(attributeValue(
                              attributes, QStringLiteral("isRef")));
                        const std::size_t duration =
                              attributeValue(attributes,
                                             QStringLiteral("duration"))
                                    .toInt();
                        const QString itemText =
                              xml.readElementText(
                                       QXmlStreamReader::IncludeChildElements)
                                    .trimmed();
                        const QString media =
                              isReference ? resolveMediaReference(parsedGame,
                                                                  itemText)
                                          : itemText;

                        storeMedia(*currentQuestion, paramName, mediaType,
                                   media, duration);
                        if (paramName == QStringLiteral("question") &&
                            mediaType == MediaType::None)
                        {
                              appendText(currentQuestion->text, itemText);
                        }
                        else if (mediaType == MediaType::None &&
                                 paramStack.size() >= 2 &&
                                 paramStack[paramStack.size() - 2] ==
                                       QStringLiteral("answerOptions") &&
                                 !currentQuestion->answerOptions.empty())
                        {
                              appendText(
                                    currentQuestion->answerOptions.back().text,
                                    itemText);
                        }
                  }
            }
            else if (xml.isCharacters() && !paramTextStack.empty())
            {
                  paramTextStack.back() += xml.text().toString();
            }
            else if (xml.isEndElement())
            {
                  const QString elementName = xml.name().toString();

                  if (elementName == QStringLiteral("param") &&
                      !paramStack.empty())
                  {
                        if (currentQuestion != nullptr &&
                            paramStack.size() == 1)
                        {
                              if (paramStack.back() ==
                                  QStringLiteral("answerDuration"))
                              {
                                    bool isDurationValid{false};
                                    const int answerDuration =
                                          paramTextStack.back().trimmed().toInt(
                                                &isDurationValid);
                                    if (isDurationValid && answerDuration >= 0)
                                    {
                                          currentQuestion->answerDuration =
                                                static_cast<std::size_t>(
                                                      answerDuration);
                                    }
                              }
                              else if (paramStack.back() ==
                                       QStringLiteral("answerType"))
                              {
                                    currentQuestion->answerType =
                                          answerTypeFromString(
                                                paramTextStack.back().trimmed());
                              }
                        }
                        paramStack.pop_back();
                        paramTextStack.pop_back();
                  }
                  else if (elementName == QStringLiteral("right"))
                  {
                        insideRightAnswers = false;
                  }
                  else if (elementName == QStringLiteral("wrong"))
                  {
                        insideWrongAnswers = false;
                  }
                  else if (elementName == QStringLiteral("question"))
                  {
                        currentQuestion = nullptr;
                  }
                  else if (elementName == QStringLiteral("theme"))
                  {
                        currentTheme = nullptr;
                  }
                  else if (elementName == QStringLiteral("round"))
                  {
                        currentRound = nullptr;
                  }
            }
      }

      if (xml.hasError())
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage =
                        QStringLiteral(
                              "XML parse error at line %1, column %2: %3")
                              .arg(xml.lineNumber())
                              .arg(xml.columnNumber())
                              .arg(xml.errorString());
            }
            return false;
      }

      *game = std::move(parsedGame);
      return true;
}
