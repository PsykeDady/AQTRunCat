#include "cpumonitor.h"

CpuMonitor::CpuMonitor(QObject *parent, double smoothing)
    : QObject(parent)
    , m_ema(smoothing)
{
}

void CpuMonitor::consume(const aqtruncat::metrics::CpuMetrics &cpu)
{
    if (!cpu.status.ok() || !cpu.usagePercent)
        return;
    const double raw = *cpu.usagePercent;
    emit sampled(raw, m_ema.add(raw));
}
