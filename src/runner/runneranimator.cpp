#include "runneranimator.h"

#include <vector>

namespace aqtruncat {

RunnerAnimator::RunnerAnimator(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &RunnerAnimator::tick);
}

void RunnerAnimator::setSequence(const QList<int> &sequence)
{
    m_playback.setSequence(std::vector<int>(sequence.cbegin(), sequence.cend()));
    if (!m_playback.isEmpty())
        emit frameChanged(m_playback.currentFrame());
}

void RunnerAnimator::setFps(int fps)
{
    if (fps <= 0 || fps == m_fps)
        return;
    m_fps = fps;
    if (!m_paused)
        m_timer.start(qRound(1000.0 / fps));
}

void RunnerAnimator::setPaused(bool paused)
{
    if (paused == m_paused)
        return;
    m_paused = paused;
    if (paused) {
        m_timer.stop();
    } else if (m_fps > 0) {
        m_timer.start(qRound(1000.0 / m_fps));
    }
}

void RunnerAnimator::tick()
{
    if (!m_playback.isEmpty())
        emit frameChanged(m_playback.advance());
}

} // namespace aqtruncat
