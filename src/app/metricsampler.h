#pragma once

#include "metrics/systemmetrics.h"

#include <QObject>
#include <QTimer>

#include <chrono>

// Application-owned runtime sampler. It is independent of Dashboard
// visibility and is the sole metrics source in the desktop process.
class MetricSampler : public QObject
{
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds DefaultInterval{1000};

    explicit MetricSampler(QObject *parent = nullptr,
                           aqtruncat::metrics::MetricsSources sources = {},
                           std::chrono::milliseconds interval = DefaultInterval);

    void start();
    void stop();
    bool isActive() const { return m_timer.isActive(); }
    void setNetworkSelection(aqtruncat::metrics::NetworkSelection selection);
    void setGpuPreference(const QString &device);
    void setFpsSource(const QString &source);
    aqtruncat::metrics::FpsMetrics sampleFps() { return m_sampler.sampleFps(); }

public slots:
    void sampleNow();

signals:
    void snapshotReady(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);

private:
    aqtruncat::metrics::SystemMetricsSampler m_sampler;
    QTimer m_timer;
};
