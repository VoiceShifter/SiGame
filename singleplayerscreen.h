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
      void createGame();
    signals:
      void SingleGameStarted(int Players, const QString &GamepackPath);

    private:
      Ui::SinglePlayerScreen *ui;
      QString GamepackPath;
      QString ProfilePicturePath;
};

#endif // SINGLEPLAYERSCREEN_H
