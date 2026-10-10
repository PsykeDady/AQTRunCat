#include "runpreferences.h"

#include "appsettings.h"

namespace aqtruncat {

EffectiveRunPreferences resolveRunPreferences(const QString &cliRunnerId, int cliFps,
                                              const AppSettings &settings)
{
    EffectiveRunPreferences result;
    result.runnerFromCommandLine = !cliRunnerId.isEmpty();
    result.runnerId = result.runnerFromCommandLine ? cliRunnerId : settings.runnerId();
    result.fpsFromCommandLine = cliFps > 0;
    result.fixedFps = result.fpsFromCommandLine ? cliFps : 0;
    result.speedMode = settings.speedMode();
    return result;
}

} // namespace aqtruncat

