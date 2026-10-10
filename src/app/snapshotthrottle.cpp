#include "snapshotthrottle.h"

SnapshotThrottle::SnapshotThrottle(int intervalSeconds, QObject *parent)
    : QObject(parent)
{
    setIntervalSeconds(intervalSeconds);
}

void SnapshotThrottle::setIntervalSeconds(int seconds)
{
    const int validated = seconds == 2 || seconds == 5 ? seconds : 1;
    if (validated == m_intervalSeconds && m_lastEmission)
        return;
    m_intervalSeconds = validated;
    m_lastEmission.reset(); // the next snapshot is presented immediately
}

void SnapshotThrottle::submit(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot)
{
    if (!m_lastEmission || snapshot.timeSeconds < *m_lastEmission
        || snapshot.timeSeconds - *m_lastEmission >= m_intervalSeconds) {
        m_lastEmission = snapshot.timeSeconds;
        emit snapshotReady(snapshot);
    }
}

