#pragma once

#include "core/speedcurve.h"

#include <QString>

namespace aqtruncat {

class AppSettings;

struct EffectiveRunPreferences {
    QString runnerId;
    SpeedMode speedMode = SpeedMode::Normal;
    int fixedFps = 0;
    bool runnerFromCommandLine = false;
    bool fpsFromCommandLine = false;
};

// One precedence point for the desktop invocation:
// explicit CLI > persisted AppSettings > built-in defaults.
EffectiveRunPreferences resolveRunPreferences(const QString &cliRunnerId, int cliFps,
                                              const AppSettings &settings);

} // namespace aqtruncat

