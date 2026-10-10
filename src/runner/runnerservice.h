#pragma once

// Runner operations shared by every frontend: the command line today, the
// Settings UI later. Frontends only parse input and present results.

#include "runnerarchive.h"
#include "runnercatalog.h"
#include "runnerinstaller.h"
#include "runnerremover.h"

#include <QList>
#include <QString>

namespace aqtruncat {

struct RunnerLocations {
    QString bundledRoot; // ":/runners" in the application
    QString userRoot;    // userRunnersDirectory() in the application
};

struct ListedRunner {
    RunnerEntry entry;
    RunnerLoadResult result;
};

struct ImportOptions {
    QString id;            // empty: derived from the archive
    bool replaceExisting = false;
    ArchiveLimits limits;
};

struct ImportResult {
    RunnerError error = RunnerError::None;
    QString message;
    std::optional<Runner> runner;  // the validated runner, as installed
    QString installedPath;
    QString replacedUserPath;      // a previous user runner that was replaced
    bool overridesBundled = false; // a bundled runner with the same id is now hidden
    QStringList skippedEntries;    // archive junk (__MACOSX, hidden files)
    qint64 extractMs = 0, validateMs = 0, installMs = 0;
    bool ok() const { return error == RunnerError::None; }
};

class RunnerService
{
public:
    explicit RunnerService(RunnerLocations locations);

    const RunnerLocations &locations() const { return m_locations; }

    QList<RunnerEntry> entries(QStringList *notes = nullptr) const;

    // Every discovered runner with its load result (valid or not).
    QList<ListedRunner> list() const;

    RunnerChoice choose(const QString &requestedId, const QString &defaultId) const;

    // ZIP archive → safe extraction (temporary directory) → validation →
    // installation as a user runner.
    ImportResult importArchive(const QString &archivePath, const ImportOptions &options = {}) const;

    RemoveResult remove(const QString &id) const;

private:
    RunnerLocations m_locations;
};

} // namespace aqtruncat
