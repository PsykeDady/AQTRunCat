#include "metricsampler.h"

MetricSampler::MetricSampler(QObject *parent, aqtruncat::metrics::MetricsSources sources,
                             std::chrono::milliseconds interval)
    : QObject(parent)
    , m_sampler(std::move(sources))
{
    m_timer.setInterval(interval);
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MetricSampler::sampleNow);
}

void MetricSampler::start()
{
    if (m_timer.isActive())
        return;
    sampleNow(); // establish rate/CPU baselines immediately
    m_timer.start();
}

void MetricSampler::stop()
{
    m_timer.stop();
}

void MetricSampler::setNetworkSelection(aqtruncat::metrics::NetworkSelection selection)
{
    m_sampler.setNetworkSelection(std::move(selection));
}

void MetricSampler::sampleNow()
{
    emit snapshotReady(m_sampler.sample(aqtruncat::metrics::SystemMetricsSampler::now()));
}
