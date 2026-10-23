#pragma once

namespace aqtruncat {

inline constexpr int MinFps = 10;
inline constexpr int MaxFps = 40;

enum class SpeedMode {
    Normal,
    SlowDown,
};

// AQTRunCat speed curve (approved after the M1.0 tray spike):
//   fps = round(10 + 30 * (cpu / 100)^2), cpu clamped to [0, 100].
// Quadratic so that the expensive high frame rates are reserved for real
// load. Non-finite input is treated as 0 % (slowest runner).
int cpuToFps(double cpuPercent);

// Mirrored M5 curve: fast while idle, slow under load.
//   fps = round(10 + 30 * (1 - cpu / 100)^2)
int cpuToSlowFps(double cpuPercent);

int cpuToFps(double cpuPercent, SpeedMode mode);

// Stops the runner from flickering between nearby frame rates
// (18 → 19 → 18 …, or 26 → 28 → 26 … on the steeper part of the curve) when
// the smoothed CPU hovers around a step. A target that differs by at least
// `immediateStep` FPS is applied at once (real load changes); a smaller change
// is applied only after the same target has been requested `confirmSamples`
// times in a row. Defaults chosen by replaying a recorded runtime test, see
// docs/cpu-and-runner-speed.md.
class FpsStabilizer
{
public:
    explicit FpsStabilizer(int initialFps = MinFps, int immediateStep = 3, int confirmSamples = 3)
        : m_current(initialFps), m_immediateStep(immediateStep), m_confirmSamples(confirmSamples) {}

    // Returns the frame rate the runner should use now.
    int update(int targetFps);
    int current() const { return m_current; }

private:
    int m_current;
    int m_immediateStep;
    int m_confirmSamples;
    int m_pending = 0;
    int m_pendingCount = 0;
};

} // namespace aqtruncat
