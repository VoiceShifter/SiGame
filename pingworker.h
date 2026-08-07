#ifndef PINGWORKER_H
#define PINGWORKER_H

#include "playerstate.h"

#include <QElapsedTimer>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <QVector>

class PingWorker : public QObject
{
      Q_OBJECT

    public:
      explicit PingWorker(QObject *parent = nullptr);

    public slots:
      void start();
      void stop();
      void setPlayers(const QVector<PlayerId> &players);
      void pingCompleted(const PlayerId &playerId, quint64 pingId);

    signals:
      void pingRequested(const PlayerId &playerId, quint64 pingId);
      void averageUpdated(const PlayerId &playerId, double rttMs);

    private slots:
      void requestPings();

    private:
      struct SampleState
      {
            QVector<double> samples;
            QHash<quint64, qint64> pending;
      };

      QTimer *m_timer{};
      QElapsedTimer m_clock;
      QVector<PlayerId> m_players;
      QHash<PlayerId, SampleState> m_samples;
      quint64 m_nextPingId{1};
};

#endif // PINGWORKER_H
