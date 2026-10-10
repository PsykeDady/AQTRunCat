#pragma once

#include "core/speedcurve.h"

#include <QObject>

#include <optional>

// Keeps the frozen M1 stabiliser and changes only the CPU-to-FPS transform.
class RunnerSpeedController : public QObject
{
    Q_OBJECT

public:
    explicit RunnerSpeedController(aqtruncat::SpeedMode mode, QObject *parent = nullptr);

    aqtruncat::SpeedMode mode() const { return m_mode; }
    int currentFps() const { return m_stabilizer.current(); }

public slots:
    void setMode(aqtruncat::SpeedMode mode);
    void consume(double rawPercent, double smoothedPercent);

signals:
    void fpsChanged(int appliedFps, int targetFps, double rawPercent, double smoothedPercent);

private:
    void apply(double rawPercent, double smoothedPercent);

    aqtruncat::SpeedMode m_mode;
    aqtruncat::FpsStabilizer m_stabilizer;
    std::optional<double> m_lastRaw;
    std::optional<double> m_lastSmoothed;
};

