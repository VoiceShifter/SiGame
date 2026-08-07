#include "pingworker.h"

#include <algorithm>

PingWorker::PingWorker(QObject *parent) : QObject(parent) {}

void PingWorker::start()
{
      if (m_timer == nullptr)
      {
            m_timer = new QTimer(this);
            m_timer->setInterval(500);
            connect(m_timer, &QTimer::timeout, this,
                    &PingWorker::requestPings);
      }
      m_clock.start();
      m_timer->start();
}

void PingWorker::stop()
{
      if (m_timer != nullptr)
      {
            m_timer->stop();
      }
      for (SampleState &state : m_samples)
      {
            state.pending.clear();
      }
}

void PingWorker::setPlayers(const QVector<PlayerId> &players)
{
      m_players = players;
      for (const PlayerId &player : m_players)
      {
            if (!m_samples.contains(player))
            {
                  m_samples.insert(player, {});
            }
      }
}

void PingWorker::pingCompleted(const PlayerId &playerId, quint64 pingId)
{
      auto iterator = m_samples.find(playerId);
      if (iterator == m_samples.end())
      {
            return;
      }
      auto pending = iterator->pending.find(pingId);
      if (pending == iterator->pending.end())
      {
            return;
      }
      const double rtt = std::max<qint64>(0, m_clock.elapsed() - pending.value());
      iterator->pending.erase(pending);
      iterator->samples.push_back(rtt);
      while (iterator->samples.size() > 8)
      {
            iterator->samples.removeFirst();
      }
      double total = 0.0;
      for (const double sample : iterator->samples)
      {
            total += sample;
      }
      emit averageUpdated(playerId, total / iterator->samples.size());
}

void PingWorker::requestPings()
{
      if (!m_clock.isValid())
      {
            m_clock.start();
      }
      for (const PlayerId &player : m_players)
      {
            const quint64 pingId = m_nextPingId++;
            SampleState &state = m_samples[player];
            state.pending.insert(pingId, m_clock.elapsed());
            emit pingRequested(player, pingId);
      }
}
