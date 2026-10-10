#include "runnerselection.h"

#include <algorithm>

StartupRunnerSelection selectStartupRunner(const aqtruncat::RunnerService &runners,
                                           const QString &requestedId,
                                           const QString &defaultId,
                                           bool explicitCommandLine)
{
    StartupRunnerSelection result;
    result.choice = runners.choose(requestedId, defaultId);
    if (result.choice.runner || explicitCommandLine)
        return result;

    const QList<aqtruncat::RunnerEntry> entries = runners.entries();
    const bool stillExists = std::any_of(entries.cbegin(), entries.cend(), [&](const aqtruncat::RunnerEntry &entry) {
        return entry.id == requestedId;
    });
    const QString error = result.choice.error;
    result.choice = runners.choose(QString(), defaultId);
    result.choice.notes.prepend(QStringLiteral("saved runner \"%1\" cannot be used: %2")
                                    .arg(requestedId, error));
    result.repairPersistedRunner = result.choice.runner.has_value() && !stillExists;
    return result;
}

