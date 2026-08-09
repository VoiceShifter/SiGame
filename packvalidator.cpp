#include "packvalidator.h"

#include "gamecontent.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSet>
#include <QTextStream>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

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

bool isSafeRelativePath(const QString &path)
{
      const QString cleanPath = QDir::cleanPath(path);
      return !path.trimmed().isEmpty() && !QDir::isAbsolutePath(path) &&
             cleanPath != QStringLiteral("..") &&
             !cleanPath.startsWith(QStringLiteral("../"));
}

bool isValidBoolean(const QString &value)
{
      const QString normalized = value.trimmed().toLower();
      return normalized == QStringLiteral("true") ||
             normalized == QStringLiteral("false") ||
             normalized == QStringLiteral("1") ||
             normalized == QStringLiteral("0") ||
             normalized == QStringLiteral("yes") ||
             normalized == QStringLiteral("no");
}

bool isValidDuration(const QString &value)
{
      const QStringList parts = value.trimmed().split(QLatin1Char(':'));
      bool valid = false;
      if (parts.size() == 1)
      {
            parts[0].toULongLong(&valid);
            return valid;
      }
      if (parts.size() != 3)
      {
            return false;
      }
      parts[0].toULongLong(&valid);
      if (!valid)
      {
            return false;
      }
      const qulonglong minutes = parts[1].toULongLong(&valid);
      if (!valid || minutes >= 60)
      {
            return false;
      }
      const qulonglong seconds = parts[2].toULongLong(&valid);
      return valid && seconds < 60;
}

bool isPointAnswer(const QString &value)
{
      const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
      if (parts.size() != 2 && parts.size() != 3)
      {
            return false;
      }
      bool xValid = false;
      bool yValid = false;
      const double x = parts[0].trimmed().toDouble(&xValid);
      const double y = parts[1].trimmed().toDouble(&yValid);
      if (!xValid || !yValid || !std::isfinite(x) || !std::isfinite(y) ||
          x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0)
      {
            return false;
      }
      if (parts.size() == 3)
      {
            bool ratioValid = false;
            const double ratio = parts[2].trimmed().toDouble(&ratioValid);
            if (!ratioValid || !std::isfinite(ratio))
            {
                  return false;
            }
      }
      return true;
}

QString questionLocation(int roundIndex, int themeIndex, int questionIndex)
{
      return QObject::tr("Round %1, theme %2, question %3")
            .arg(roundIndex + 1)
            .arg(themeIndex + 1)
            .arg(questionIndex + 1);
}

QString resolvedMediaPath(const Game &game, const QString &reference)
{
      for (const QString &mediaFile : game.mediaFiles)
      {
            if (mediaFile == reference ||
                mediaFile.endsWith(QStringLiteral("/") + reference))
            {
                  return mediaFile;
            }
      }
      return reference;
}

void addError(PackValidationResult &result, const QString &message)
{
      result.errors.push_back(message);
      result.logLines.push_back(QObject::tr("ERROR: %1").arg(message));
}

void validateFile(const QDir &pack, const QString &relativePath,
                  const QString &context, PackValidationResult &result)
{
      if (!isSafeRelativePath(relativePath))
      {
            addError(result,
                     QObject::tr("%1 uses an invalid pack-relative path: %2")
                           .arg(context, relativePath));
            return;
      }
      const QFileInfo file(pack.filePath(QDir::cleanPath(relativePath)));
      if (!file.isFile() || file.isSymLink())
      {
            addError(result, QObject::tr("%1 is missing file: %2")
                                   .arg(context, relativePath));
      }
}

void validateXmlItems(const QString &contentPath, const QDir &pack,
                      const Game &game, PackValidationResult &result)
{
      QFile file(contentPath);
      if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
      {
            return;
      }

      QXmlStreamReader xml(&file);
      int roundIndex = -1;
      int themeIndex = -1;
      int questionIndex = -1;
      bool insideQuestion = false;

      while (!xml.atEnd())
      {
            xml.readNext();
            if (xml.isStartElement())
            {
                  const QString name = xml.name().toString();
                  const QXmlStreamAttributes attributes = xml.attributes();
                  if (name == QStringLiteral("round"))
                  {
                        ++roundIndex;
                        themeIndex = -1;
                  }
                  else if (name == QStringLiteral("theme"))
                  {
                        ++themeIndex;
                        questionIndex = -1;
                  }
                  else if (name == QStringLiteral("question"))
                  {
                        ++questionIndex;
                        insideQuestion = true;
                        bool validPrice = false;
                        attributeValue(attributes, QStringLiteral("price"))
                              .toInt(&validPrice);
                        if (!validPrice)
                        {
                              addError(
                                    result,
                                    QObject::tr("%1 has an invalid price.")
                                          .arg(questionLocation(
                                                roundIndex, themeIndex,
                                                questionIndex)));
                        }
                  }
                  else if (insideQuestion && name == QStringLiteral("item"))
                  {
                        const QString location = questionLocation(
                              roundIndex, themeIndex, questionIndex);
                        const QString type = attributeValue(
                              attributes, QStringLiteral("type"));
                        if (!type.isEmpty() && type != QStringLiteral("image") &&
                            type != QStringLiteral("audio") &&
                            type != QStringLiteral("video"))
                        {
                              addError(result,
                                       QObject::tr("%1 has unsupported media "
                                                   "type '%2'.")
                                             .arg(location, type));
                        }
                        if (hasAttribute(attributes, QStringLiteral("isRef")) &&
                            !isValidBoolean(attributeValue(
                                  attributes, QStringLiteral("isRef"))))
                        {
                              addError(result,
                                       QObject::tr("%1 has an invalid isRef "
                                                   "value.")
                                             .arg(location));
                        }
                        if (hasAttribute(attributes,
                                         QStringLiteral("waitForFinish")) &&
                            !isValidBoolean(attributeValue(
                                  attributes,
                                  QStringLiteral("waitForFinish"))))
                        {
                              addError(result,
                                       QObject::tr("%1 has an invalid "
                                                   "waitForFinish value.")
                                             .arg(location));
                        }
                        if (hasAttribute(attributes,
                                         QStringLiteral("duration")) &&
                            !isValidDuration(attributeValue(
                                  attributes, QStringLiteral("duration"))))
                        {
                              addError(result,
                                       QObject::tr("%1 has an invalid media "
                                                   "duration.")
                                             .arg(location));
                        }

                        const QString itemText =
                              xml.readElementText(
                                       QXmlStreamReader::IncludeChildElements)
                                    .trimmed();
                        if (type == QStringLiteral("image") ||
                            type == QStringLiteral("audio") ||
                            type == QStringLiteral("video"))
                        {
                              if (itemText.isEmpty())
                              {
                                    addError(result,
                                             QObject::tr("%1 has an empty media "
                                                         "path.")
                                                   .arg(location));
                              }
                              else
                              {
                                    const bool isReference =
                                          attributeValue(
                                                attributes,
                                                QStringLiteral("isRef"))
                                                .trimmed()
                                                .compare(QStringLiteral("true"),
                                                         Qt::CaseInsensitive) ==
                                                0 ||
                                          attributeValue(
                                                attributes,
                                                QStringLiteral("isRef")) ==
                                                QStringLiteral("1") ||
                                          attributeValue(
                                                attributes,
                                                QStringLiteral("isRef"))
                                                .trimmed()
                                                .compare(QStringLiteral("yes"),
                                                         Qt::CaseInsensitive) ==
                                                0;
                                    const QString mediaPath =
                                          isReference
                                                ? resolvedMediaPath(game,
                                                                    itemText)
                                                : itemText;
                                    if (isReference &&
                                        std::find(game.mediaFiles.begin(),
                                                  game.mediaFiles.end(),
                                                  mediaPath) ==
                                              game.mediaFiles.end())
                                    {
                                          addError(
                                                result,
                                                QObject::tr("%1 references media "
                                                            "not declared in "
                                                            "content.xml: %2")
                                                      .arg(location, itemText));
                                    }
                                    validateFile(pack, mediaPath, location,
                                                 result);
                              }
                        }
                  }
            }
            else if (xml.isEndElement() &&
                     xml.name() == QStringLiteral("question"))
            {
                  insideQuestion = false;
            }
      }
}
} // namespace

PackValidationResult PackValidator::validate(const QString &packPath)
{
      PackValidationResult result;
      const QFileInfo rootInfo(packPath);
      result.logLines.push_back(
            QObject::tr("Validating pack: %1").arg(packPath));
      if (!rootInfo.isDir())
      {
            addError(result, QObject::tr("The selected folder does not exist."));
            return result;
      }

      const QDir pack(rootInfo.absoluteFilePath());
      const QStringList requiredDirectories = {QStringLiteral("Audio"),
                                               QStringLiteral("Images"),
                                               QStringLiteral("Video")};
      for (const QString &directory : requiredDirectories)
      {
            if (!QFileInfo(pack.filePath(directory)).isDir())
            {
                  addError(result,
                           QObject::tr("Required directory is missing: %1")
                                 .arg(directory));
            }
      }
      const QStringList requiredFiles = {QStringLiteral("content.xml"),
                                         QStringLiteral("quality.marker")};
      for (const QString &requiredFile : requiredFiles)
      {
            if (!QFileInfo(pack.filePath(requiredFile)).isFile())
            {
                  addError(result, QObject::tr("Required file is missing: %1")
                                         .arg(requiredFile));
            }
      }

      const QString contentPath =
            pack.filePath(QStringLiteral("content.xml"));
      if (!QFileInfo(contentPath).isFile())
      {
            return result;
      }

      Game game;
      QString parseError;
      if (!parseGameContent(contentPath, &game, &parseError))
      {
            addError(result, parseError);
            return result;
      }
      result.logLines.push_back(QObject::tr("OK: content.xml parsed correctly."));
      result.logLines.push_back(QObject::tr("Package: %1").arg(game.name));

      if (game.name.trimmed().isEmpty())
      {
            addError(result, QObject::tr("The package name is empty."));
      }
      if (game.rounds.empty())
      {
            addError(result, QObject::tr("The pack has no rounds."));
      }

      QSet<QString> declaredMedia;
      for (const QString &mediaFile : game.mediaFiles)
      {
            if (declaredMedia.contains(mediaFile))
            {
                  addError(result,
                           QObject::tr("Media file is declared more than once: %1")
                                 .arg(mediaFile));
            }
            declaredMedia.insert(mediaFile);
            validateFile(pack, mediaFile, QObject::tr("Media index"), result);
      }

      for (std::size_t roundIndex = 0; roundIndex < game.rounds.size();
           ++roundIndex)
      {
            const Round &round = game.rounds[roundIndex];
            ++result.roundCount;
            if (round.name.trimmed().isEmpty())
            {
                  addError(result,
                           QObject::tr("Round %1 has an empty name.")
                                 .arg(roundIndex + 1));
            }
            if (round.themes.empty())
            {
                  addError(result, QObject::tr("Round %1 has no themes.")
                                         .arg(roundIndex + 1));
            }
            for (std::size_t themeIndex = 0;
                 themeIndex < round.themes.size(); ++themeIndex)
            {
                  const Theme &theme = round.themes[themeIndex];
                  ++result.themeCount;
                  if (theme.name.trimmed().isEmpty())
                  {
                        addError(result,
                                 QObject::tr("Round %1, theme %2 has an empty "
                                             "name.")
                                       .arg(roundIndex + 1)
                                       .arg(themeIndex + 1));
                  }
                  if (theme.questions.empty())
                  {
                        addError(result,
                                 QObject::tr("Round %1, theme %2 has no "
                                             "questions.")
                                       .arg(roundIndex + 1)
                                       .arg(themeIndex + 1));
                  }
                  for (std::size_t questionIndex = 0;
                       questionIndex < theme.questions.size(); ++questionIndex)
                  {
                        const Question &question =
                              theme.questions[questionIndex];
                        ++result.questionCount;
                        const QString location = questionLocation(
                              static_cast<int>(roundIndex),
                              static_cast<int>(themeIndex),
                              static_cast<int>(questionIndex));
                        result.logLines.push_back(
                              QObject::tr("Checking %1 (price %2)")
                                    .arg(location)
                                    .arg(question.price));

                        if (question.type == QuestionType::Unknown)
                        {
                              addError(result,
                                       QObject::tr("%1 has an unsupported "
                                                   "question type.")
                                             .arg(location));
                        }
                        if (question.answerType == AnswerType::Unknown)
                        {
                              addError(result,
                                       QObject::tr("%1 has an unsupported "
                                                   "answer type.")
                                             .arg(location));
                        }
                        if (question.text.trimmed().isEmpty() &&
                            question.mediaType == MediaType::None)
                        {
                              addError(result,
                                       QObject::tr("%1 has no question text or "
                                                   "media.")
                                             .arg(location));
                        }
                        if (question.rightAnswers.empty())
                        {
                              addError(result,
                                       QObject::tr("%1 has no right answer.")
                                             .arg(location));
                        }

                        if (question.answerType == AnswerType::Select)
                        {
                              if (question.answerOptions.empty())
                              {
                                    addError(result,
                                             QObject::tr("%1 is a select "
                                                         "question without "
                                                         "answer options.")
                                                   .arg(location));
                              }
                              QSet<QString> optionIds;
                              for (const AnswerOption &option :
                                   question.answerOptions)
                              {
                                    if (option.id.trimmed().isEmpty() ||
                                        option.text.trimmed().isEmpty())
                                    {
                                          addError(
                                                result,
                                                QObject::tr("%1 has an answer "
                                                            "option with an "
                                                            "empty id or text.")
                                                      .arg(location));
                                    }
                                    if (optionIds.contains(option.id))
                                    {
                                          addError(
                                                result,
                                                QObject::tr("%1 has duplicate "
                                                            "answer option id "
                                                            "'%2'.")
                                                      .arg(location, option.id));
                                    }
                                    optionIds.insert(option.id);
                              }
                              for (const QString &answer :
                                   question.rightAnswers)
                              {
                                    if (!optionIds.contains(answer))
                                    {
                                          addError(
                                                result,
                                                QObject::tr("%1 has right answer "
                                                            "'%2' which is not "
                                                            "an option id.")
                                                      .arg(location, answer));
                                    }
                              }
                        }
                        else if (question.answerType == AnswerType::Point)
                        {
                              bool hasPointAnswer = false;
                              for (const QString &answer :
                                   question.rightAnswers)
                              {
                                    hasPointAnswer =
                                          hasPointAnswer || isPointAnswer(answer);
                              }
                              if (!hasPointAnswer)
                              {
                                    addError(result,
                                             QObject::tr("%1 has no valid point "
                                                         "answer (x,y[,aspect]).")
                                                   .arg(location));
                              }
                              if (question.mediaType != MediaType::Image)
                              {
                                    addError(result,
                                             QObject::tr("%1 is a point question "
                                                         "without a question "
                                                         "image.")
                                                   .arg(location));
                              }
                              if (!std::isfinite(question.answerDeviation) ||
                                  question.answerDeviation <= 0.0)
                              {
                                    addError(result,
                                             QObject::tr("%1 has an invalid "
                                                         "answer deviation.")
                                                   .arg(location));
                              }
                        }

                        if (question.type ==
                            QuestionType::SecretPublicPrice)
                        {
                              if (!question.secretParameters.has_value())
                              {
                                    addError(result,
                                             QObject::tr("%1 has no secret "
                                                         "question parameters.")
                                                   .arg(location));
                              }
                              else
                              {
                                    const SecretQuestionParameters &secret =
                                          *question.secretParameters;
                                    if (secret.selectionMode.trimmed().isEmpty())
                                    {
                                          addError(result,
                                                   QObject::tr("%1 has an empty "
                                                               "secret selection "
                                                               "mode.")
                                                         .arg(location));
                                    }
                                    const NumberSet &price = secret.price;
                                    const bool zeroOnly =
                                          price.minimum == 0 &&
                                          price.maximum == 0 && price.step == 0;
                                    if (!zeroOnly &&
                                        (price.minimum > price.maximum ||
                                         price.step <= 0))
                                    {
                                          addError(result,
                                                   QObject::tr("%1 has an invalid "
                                                               "secret price "
                                                               "range.")
                                                         .arg(location));
                                    }
                              }
                        }
                  }
            }
      }

      validateXmlItems(contentPath, pack, game, result);
      result.logLines.push_back(
            QObject::tr("Checked %1 rounds, %2 themes, and %3 questions.")
                  .arg(result.roundCount)
                  .arg(result.themeCount)
                  .arg(result.questionCount));
      result.logLines.push_back(
            result.isValid()
                  ? QObject::tr("VALIDATION PASSED: no errors found.")
                  : QObject::tr("VALIDATION FAILED: %1 error(s) found.")
                          .arg(result.errors.size()));
      return result;
}

bool PackValidator::writeErrors(const PackValidationResult &result,
                                QString *filePath, QString *errorMessage)
{
      const QString path =
            QDir(QCoreApplication::applicationDirPath())
                  .filePath(QStringLiteral("errors.txt"));
      if (filePath != nullptr)
      {
            *filePath = path;
      }

      QFile file(path);
      if (!file.open(QIODevice::WriteOnly | QIODevice::Text |
                     QIODevice::Truncate))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QObject::tr("Cannot write %1: %2")
                                        .arg(path, file.errorString());
            }
            return false;
      }

      QTextStream stream(&file);
      for (const QString &error : result.errors)
      {
            stream << error << '\n';
      }
      return true;
}
