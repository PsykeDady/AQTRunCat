#include "speedcurve.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aqtruncat {

int cpuToFps(double cpuPercent)
{
    const double cpu = std::isfinite(cpuPercent) ? std::clamp(cpuPercent, 0.0, 100.0) : 0.0;
    const double load = cpu / 100.0;
    return static_cast<int>(std::lround(MinFps + (MaxFps - MinFps) * load * load));
}

int cpuToSlowFps(double cpuPercent)
{
    const double cpu = std::isfinite(cpuPercent) ? std::clamp(cpuPercent, 0.0, 100.0) : 0.0;
    const double idle = 1.0 - cpu / 100.0;
    return static_cast<int>(std::lround(MinFps + (MaxFps - MinFps) * idle * idle));
}

int cpuToFps(double cpuPercent, SpeedMode mode)
{
    return mode == SpeedMode::SlowDown ? cpuToSlowFps(cpuPercent) : cpuToFps(cpuPercent);
}

int FpsStabilizer::update(int targetFps)
{
    if (targetFps == m_current) {
        m_pendingCount = 0;
        return m_current;
    }
    if (std::abs(targetFps - m_current) >= m_immediateStep) {
        m_current = targetFps;
        m_pendingCount = 0;
        return m_current;
    }
    if (targetFps != m_pending) {
        m_pending = targetFps;
        m_pendingCount = 0;
    }
    if (++m_pendingCount >= m_confirmSamples) {
        m_current = targetFps;
        m_pendingCount = 0;
    }
    return m_current;
}

} // namespace aqtruncat
