#pragma once

#include "core/smoothing.h"
#include "metrics/systemmetrics.h"

#include <QObject>

// Runner-side consumer of CPU values produced by the shared desktop metric
// sampler. It deliberately owns only the M1 EMA: source reads and timing
// belong to MetricSampler, while the Dashboard receives the raw snapshot.
class CpuMonitor : public QObject
{
    Q_OBJECT

public:
    static constexpr double DefaultSmoothing = aqtruncat::CpuSmoothingAlpha;

    explicit CpuMonitor(QObject *parent = nullptr, double smoothing = DefaultSmoothing);

public slots:
    // Unavailable/Pending/Error CPU sections leave the current runner speed
    // unchanged, matching the old CpuMonitor failure behaviour.
    void consume(const aqtruncat::metrics::CpuMetrics &cpu);

signals:
    void sampled(double rawPercent, double smoothedPercent);

private:
    aqtruncat::Ema m_ema;
};
