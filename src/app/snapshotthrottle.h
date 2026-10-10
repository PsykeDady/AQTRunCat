#pragma once

#include "metrics/systemmetrics.h"

#include <QObject>

#include <optional>

// Presentation cadence only: the system sampler and runner continue at 1 Hz.
class SnapshotThrottle : public QObject
{
    Q_OBJECT

public:
    explicit SnapshotThrottle(int intervalSeconds = 1, QObject *parent = nullptr);

    int intervalSeconds() const { return m_intervalSeconds; }

public slots:
    void setIntervalSeconds(int seconds);
    void submit(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);

signals:
    void snapshotReady(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);

private:
    int m_intervalSeconds = 1;
    std::optional<double> m_lastEmission;
};

