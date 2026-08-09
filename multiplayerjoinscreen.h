#ifndef MULTIPLAYERJOINSCREEN_H
#define MULTIPLAYERJOINSCREEN_H

#include "multiplayerclient.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QListWidget;

class MultiplayerJoinScreen : public QWidget
{
      Q_OBJECT

    public:
      explicit MultiplayerJoinScreen(QWidget *parent = nullptr);
      ~MultiplayerJoinScreen() override = default;

      MultiplayerClient *client() const { return m_client; }
      QString gamepackPath() const { return m_packPath; }

    signals:
      void gameStarted(MultiplayerClient *client, QString gamepackPath);
      void cancelled();

    private slots:
      void choosePack();
      void chooseProfile();
      void connectOrDisconnect();
      void updateRoster(const QVector<PlayerState> &players);
      void updateStatus(const QString &status);
      void showError(QString code, QString message);

    private:
      bool preparePack();
      void usePack(const QString &path);
      void useProfile(const QString &path);

      QLineEdit *m_addressEdit{};
      QSpinBox *m_portSpin{};
      QLineEdit *m_packEdit{};
      QLineEdit *m_nicknameEdit{};
      QLabel *m_profilePreview{};
      QLabel *m_hashLabel{};
      QLabel *m_hostConfigLabel{};
      QLabel *m_statusLabel{};
      QListWidget *m_rosterList{};
      QPushButton *m_connectButton{};
      QString m_packPath;
      QString m_profilePath;
      PlayerIdentity m_identity;
      MultiplayerClient *m_client{};
};

#endif // MULTIPLAYERJOINSCREEN_H
