#ifndef SINGLEPLAYERSCREEN_H
#define SINGLEPLAYERSCREEN_H

#include <QString>
#include <QWidget>

namespace Ui
{
class SinglePlayerScreen;
}

class SinglePlayerScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit SinglePlayerScreen(QWidget *parent = nullptr);
      ~SinglePlayerScreen();

    private slots:
      void pickPack();
      void pickProfilePicture();
      void createGame();
    signals:
      void SingleGameStarted(int Players, const QString &GamepackPath,
                             const QString &ProfilePicturePath,
                             const QString &Nickname, int answerDuration,
                             int questionDuration,
                             int questionPickDuration,
                             int answerWaitDuration);

    private:
      void usePack(const QString &path, bool showInvalidWarning,
                   bool updateCache = true);
      void useProfilePicture(const QString &path, bool showInvalidWarning,
                             bool updateCache = true);
      void saveCache() const;

      Ui::SinglePlayerScreen *ui;
      QString GamepackPath;
      QString ProfilePicturePath;
};

#endif // SINGLEPLAYERSCREEN_H
