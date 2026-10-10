#pragma once

#include "core/frameplayback.h"

#include <QList>
#include <QObject>
#include <QTimer>

namespace aqtruncat {

// Advances through a runner's playback sequence at a given frame rate and
// announces which frame to show. Knows neither the CPU nor the tray.
class RunnerAnimator : public QObject
{
    Q_OBJECT

public:
    explicit RunnerAnimator(QObject *parent = nullptr);

    // New runner: playback restarts at its first entry; the frame rate and
    // the running timer are kept. Emits frameChanged() for that first frame.
    void setSequence(const QList<int> &sequence);

    // Changes only the timer interval; the current position is kept, so the
    // cycle continues from the frame on screen.
    void setFps(int fps);

    int fps() const { return m_fps; }
    int currentFrame() const { return m_playback.currentFrame(); }
    bool isRunning() const { return m_timer.isActive(); }
    void setPaused(bool paused);

signals:
    void frameChanged(int frameIndex);

private:
    void tick();

    QTimer m_timer;
    FramePlayback m_playback;
    int m_fps = 0;
    bool m_paused = false;
};

} // namespace aqtruncat
