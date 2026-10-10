#include "runnerspeedcontroller.h"

RunnerSpeedController::RunnerSpeedController(aqtruncat::SpeedMode mode, QObject *parent)
    : QObject(parent)
    , m_mode(mode)
    , m_stabilizer(aqtruncat::cpuToFps(0.0, mode))
{
}

void RunnerSpeedController::setMode(aqtruncat::SpeedMode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    apply(m_lastRaw.value_or(0.0), m_lastSmoothed.value_or(0.0));
}

void RunnerSpeedController::consume(double rawPercent, double smoothedPercent)
{
    m_lastRaw = rawPercent;
    m_lastSmoothed = smoothedPercent;
    apply(rawPercent, smoothedPercent);
}

void RunnerSpeedController::apply(double rawPercent, double smoothedPercent)
{
    const int target = aqtruncat::cpuToFps(smoothedPercent, m_mode);
    const int applied = m_stabilizer.update(target);
    emit fpsChanged(applied, target, rawPercent, smoothedPercent);
}

