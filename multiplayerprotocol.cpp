#include "multiplayerprotocol.h"

#include "playerstate.h"

#include <QRegularExpression>
#include <QUrl>

#include <cmath>

namespace
{
bool validFieldName(const QString &name)
{
      static const QRegularExpression expression(
            QStringLiteral("^[A-Za-z0-9_]+$"));
      return expression.match(name).hasMatch();
}

bool validPercentEncoding(const QString &value)
{
      for (int index = 0; index < value.size(); ++index)
      {
            if (value[index] != QLatin1Char('%'))
            {
                  continue;
            }
            if (index + 2 >= value.size() ||
                !value[index + 1].isDigit() &&
                      (value[index + 1].toUpper() < QLatin1Char('A') ||
                       value[index + 1].toUpper() > QLatin1Char('F')) ||
                !value[index + 2].isDigit() &&
                      (value[index + 2].toUpper() < QLatin1Char('A') ||
                       value[index + 2].toUpper() > QLatin1Char('F')))
            {
                  return false;
            }
            index += 2;
      }
      return true;
}

QString fieldError(const QString &name)
{
      return QStringLiteral("Missing or invalid field: %1").arg(name);
}
} // namespace

namespace MultiplayerProtocol
{
bool parseFrame(const QByteArray &line, Frame *frame, QString *errorMessage)
{
      if (frame == nullptr)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Frame output is null");
            }
            return false;
      }
      *frame = {};
      if (line.size() > MaxControlLineBytes)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Control line is too large");
            }
            return false;
      }

      QByteArray data = line;
      while (!data.isEmpty() && (data.endsWith('\n') || data.endsWith('\r')))
      {
            data.chop(1);
      }
      if (data.isEmpty())
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Empty command");
            }
            return false;
      }

      const QString text = QString::fromUtf8(data);
      const QStringList tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (tokens.isEmpty() || tokens.front().isEmpty() ||
          !validFieldName(tokens.front()))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Invalid command");
            }
            return false;
      }
      frame->command = tokens.front();
      for (int index = 1; index < tokens.size(); ++index)
      {
            const QString &token = tokens[index];
            const int separator = token.indexOf(QLatin1Char('='));
            if (separator <= 0)
            {
                  if (errorMessage != nullptr)
                  {
                        *errorMessage = QStringLiteral("Invalid field");
                  }
                  return false;
            }
            const QString name = token.left(separator);
            const QString value = token.mid(separator + 1);
            if (!validFieldName(name) || frame->fields.contains(name) ||
                !validPercentEncoding(value))
            {
                  if (errorMessage != nullptr)
                  {
                        *errorMessage = QStringLiteral("Invalid field: %1")
                                            .arg(name);
                  }
                  return false;
            }
            QString decoded;
            if (!decodeValue(value, &decoded, errorMessage))
            {
                  return false;
            }
            frame->fields.insert(name, decoded);
      }
      return true;
}

QByteArray encodeFrame(const QString &command,
                       const QMap<QString, QString> &fields)
{
      QByteArray result = command.toUtf8();
      for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator)
      {
            result += ' ';
            result += iterator.key().toUtf8();
            result += '=';
            result += encodeValue(iterator.value()).toUtf8();
      }
      result += '\n';
      return result;
}

QString encodeValue(const QString &value)
{
      return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

bool decodeValue(const QString &value, QString *decoded, QString *errorMessage)
{
      if (decoded == nullptr || !validPercentEncoding(value))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = QStringLiteral("Invalid percent encoding");
            }
            return false;
      }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      *decoded = QUrl::fromPercentEncoding(value.toLatin1());
#else
      *decoded = QString::fromUtf8(QUrl::fromPercentEncoding(value.toLatin1()));
#endif
      return true;
}

bool parseUnsigned(const QMap<QString, QString> &fields, const QString &name,
                   quint64 *value, QString *errorMessage)
{
      if (value == nullptr || !fields.contains(name))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = fieldError(name);
            }
            return false;
      }
      bool ok = false;
      const quint64 parsed = fields.value(name).toULongLong(&ok, 10);
      if (!ok)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = fieldError(name);
            }
            return false;
      }
      *value = parsed;
      return true;
}

bool parseSigned(const QMap<QString, QString> &fields, const QString &name,
                 qint64 *value, QString *errorMessage)
{
      if (value == nullptr || !fields.contains(name))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = fieldError(name);
            }
            return false;
      }
      bool ok = false;
      const qint64 parsed = fields.value(name).toLongLong(&ok, 10);
      if (!ok)
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = fieldError(name);
            }
            return false;
      }
      *value = parsed;
      return true;
}

bool parseBool(const QMap<QString, QString> &fields, const QString &name,
              bool *value, QString *errorMessage)
{
      if (value == nullptr || !fields.contains(name))
      {
            if (errorMessage != nullptr)
            {
                  *errorMessage = fieldError(name);
            }
            return false;
      }
      const QString text = fields.value(name);
      if (text == QStringLiteral("1") || text.compare(QStringLiteral("true"),
                                                       Qt::CaseInsensitive) == 0)
      {
            *value = true;
            return true;
      }
      if (text == QStringLiteral("0") || text.compare(QStringLiteral("false"),
                                                       Qt::CaseInsensitive) == 0)
      {
            *value = false;
            return true;
      }
      if (errorMessage != nullptr)
      {
            *errorMessage = fieldError(name);
      }
      return false;
}

QString phaseName(int phase)
{
      return phaseName(static_cast<SessionPhase>(phase));
}

QString phaseName(SessionPhase phase)
{
      switch (phase)
      {
      case SessionPhase::Lobby:
            return QStringLiteral("Lobby");
      case SessionPhase::PickingQuestion:
            return QStringLiteral("PickingQuestion");
      case SessionPhase::SecretTargetSelection:
            return QStringLiteral("SecretTargetSelection");
      case SessionPhase::SecretWager:
            return QStringLiteral("SecretWager");
      case SessionPhase::ReadingQuestion:
            return QStringLiteral("ReadingQuestion");
      case SessionPhase::WaitingForReaction:
            return QStringLiteral("WaitingForReaction");
      case SessionPhase::Answering:
            return QStringLiteral("Answering");
      case SessionPhase::ForAllAnswering:
            return QStringLiteral("ForAllAnswering");
      case SessionPhase::ShowingAnswer:
            return QStringLiteral("ShowingAnswer");
      case SessionPhase::AppealVoting:
            return QStringLiteral("AppealVoting");
      case SessionPhase::Finished:
            return QStringLiteral("Finished");
      }
      return QStringLiteral("Unknown");
}

bool phaseFromName(const QString &name, SessionPhase *phase)
{
      if (phase == nullptr)
      {
            return false;
      }
      const QList<SessionPhase> phases = {
            SessionPhase::Lobby,
            SessionPhase::PickingQuestion,
            SessionPhase::SecretTargetSelection,
            SessionPhase::SecretWager,
            SessionPhase::ReadingQuestion,
            SessionPhase::WaitingForReaction,
            SessionPhase::Answering,
            SessionPhase::ForAllAnswering,
            SessionPhase::ShowingAnswer,
            SessionPhase::AppealVoting,
            SessionPhase::Finished};
      for (const SessionPhase candidate : phases)
      {
            if (phaseName(candidate).compare(name, Qt::CaseInsensitive) == 0)
            {
                  *phase = candidate;
                  return true;
            }
      }
      return false;
}

QString answerTypeName(int type)
{
      switch (static_cast<AnswerType>(type))
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

QString mediaTypeName(int type)
{
      switch (static_cast<MediaType>(type))
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
      return QStringLiteral("None");
}

QString questionTypeName(int type)
{
      switch (static_cast<QuestionType>(type))
      {
      case QuestionType::Default:
            return QStringLiteral("Default");
      case QuestionType::ForAll:
            return QStringLiteral("ForAll");
      case QuestionType::SecretPublicPrice:
            return QStringLiteral("SecretPublicPrice");
      case QuestionType::Unknown:
            return QStringLiteral("Unknown");
      }
      return QStringLiteral("Unknown");
}

bool isKnownCommand(const QString &command)
{
      static const QStringList commands = {
            QStringLiteral("HELLO"),
            QStringLiteral("WELCOME"),
            QStringLiteral("PACK_OK"),
            QStringLiteral("READY"),
            QStringLiteral("ERROR"),
            QStringLiteral("PROFILE_BEGIN"),
            QStringLiteral("PROFILE_CHUNK"),
            QStringLiteral("PROFILE_END"),
            QStringLiteral("LOBBY_STATE"),
            QStringLiteral("ROSTER_BEGIN"),
            QStringLiteral("ROSTER_PLAYER"),
            QStringLiteral("ROSTER_END"),
            QStringLiteral("GAME_STARTED"),
            QStringLiteral("ROUND_STARTED"),
            QStringLiteral("SELECT_QUESTION"),
            QStringLiteral("BOARD_STATE"),
            QStringLiteral("PICKER_CHANGED"),
            QStringLiteral("PHASE_START"),
            QStringLiteral("PHASE_SYNC"),
            QStringLiteral("PHASE_RESUMED"),
            QStringLiteral("QUESTION_START"),
            QStringLiteral("REACTION_OPEN"),
            QStringLiteral("REACTION_WINNER"),
            QStringLiteral("ANSWER_OWNER"),
            QStringLiteral("ANSWER_DRAFT"),
            QStringLiteral("ANSWER_SUBMIT"),
            QStringLiteral("PASS"),
            QStringLiteral("ANSWER_RESULT"),
            QStringLiteral("REACTION_RESUMED"),
            QStringLiteral("FORALL_PROGRESS"),
            QStringLiteral("FORALL_RESULT"),
            QStringLiteral("SECRET_TARGETS"),
            QStringLiteral("SECRET_TARGET"),
            QStringLiteral("SECRET_WAGER_PROMPT"),
            QStringLiteral("SECRET_WAGER"),
            QStringLiteral("SECRET_READY"),
            QStringLiteral("ANSWER_REVEAL"),
            QStringLiteral("APPEAL_REQUEST"),
            QStringLiteral("APPEAL_OPEN"),
            QStringLiteral("APPEAL_VOTE"),
            QStringLiteral("APPEAL_RESULT"),
            QStringLiteral("PAUSE_REQUEST"),
            QStringLiteral("PAUSE_STATE"),
            QStringLiteral("PING"),
            QStringLiteral("PONG"),
            QStringLiteral("SNAPSHOT_BEGIN"),
            QStringLiteral("SNAPSHOT_CONFIG"),
            QStringLiteral("SNAPSHOT_PLAYER"),
            QStringLiteral("SNAPSHOT_CELL"),
            QStringLiteral("SNAPSHOT_END"),
            QStringLiteral("PLAYER_CONNECTION"),
            QStringLiteral("GAME_FINISHED"),
            QStringLiteral("GAME_ABORTED")};
      return commands.contains(command);
}
} // namespace MultiplayerProtocol
