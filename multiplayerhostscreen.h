#ifndef MULTIPLAYERHOSTSCREEN_H
#define MULTIPLAYERHOSTSCREEN_H

#include "multiplayerhost.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QListWidget;

class MultiplayerHostScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit MultiplayerHostScreen(QWidget *parent = nullptr);
      ~MultiplayerHostScreen() override = default;

      MultiplayerHost *host() const { return m_host; }
      QString gamepackPath() const { return m_packPath; }

    signals:
      void gameStarted(MultiplayerHost *host, QString gamepackPath);
      void cancelled();

    private slots:
      void choosePack();
      void chooseProfile();
      void hostOrStart();
      void updateRoster(const QVector<PlayerState> &players);
      void showListening(quint16 port, const QStringList &addresses);
      void showError(const QString &error);

    private:
      bool createHost();

      QLineEdit *m_packEdit{};
      QLineEdit *m_nicknameEdit{};
      QSpinBox *m_maxPlayersSpin{};
      QSpinBox *m_answerDurationSpin{};
      QSpinBox *m_questionDurationSpin{};
      QSpinBox *m_pickDurationSpin{};
      QSpinBox *m_waitDurationSpin{};
      QLabel *m_profilePreview{};
      QLabel *m_hashLabel{};
      QLabel *m_statusLabel{};
      QListWidget *m_rosterList{};
      QPushButton *m_hostButton{};
      QString m_packPath;
      QString m_profilePath;
      PlayerIdentity m_identity;
      MultiplayerHost *m_host{};
      bool m_gameStartedEmitted{};
};

#endif // MULTIPLAYERHOSTSCREEN_H
