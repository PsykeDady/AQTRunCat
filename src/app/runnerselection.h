#pragma once

#include "runner/runnerservice.h"

struct StartupRunnerSelection {
    aqtruncat::RunnerChoice choice;
    bool repairPersistedRunner = false;
};

// Explicit CLI selection is strict. A persisted selection may fall back;
// definitively missing ids are repaired, while present-but-invalid runners
// retain their setting because the failure may be temporary.
StartupRunnerSelection selectStartupRunner(const aqtruncat::RunnerService &runners,
                                           const QString &requestedId,
                                           const QString &defaultId,
                                           bool explicitCommandLine);

