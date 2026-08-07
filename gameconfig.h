#ifndef GAMECONFIG_H
#define GAMECONFIG_H

#include <QMetaType>
#include <QString>

struct GameConfig
{
      QString gamepackPath;
      QString packHash;
      int maxPlayers{1};
      unsigned int answerDurationMs{};
      unsigned int questionDurationMs{};
      unsigned int questionPickDurationMs{};
      unsigned int answerWaitDurationMs{};
      unsigned int answerRevealDurationMs{5000U};
      unsigned int appealDurationMs{15000U};
};

Q_DECLARE_METATYPE(GameConfig)

inline GameConfig gameConfigFromSeconds(const QString &gamepackPath,
                                         const QString &packHash,
                                         int maxPlayers,
                                         int answerDuration,
                                         int questionDuration,
                                         int questionPickDuration,
                                         int answerWaitDuration)
{
      GameConfig config;
      config.gamepackPath = gamepackPath;
      config.packHash = packHash;
      config.maxPlayers = maxPlayers;
      config.answerDurationMs =
            static_cast<unsigned int>(answerDuration) * 1000U;
      config.questionDurationMs =
            static_cast<unsigned int>(questionDuration) * 1000U;
      config.questionPickDurationMs =
            static_cast<unsigned int>(questionPickDuration) * 1000U;
      config.answerWaitDurationMs =
            static_cast<unsigned int>(answerWaitDuration) * 1000U;
      return config;
}

#endif // GAMECONFIG_H
