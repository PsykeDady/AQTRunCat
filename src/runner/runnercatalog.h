#pragma once

#include "runnererror.h"
#include "runnerloader.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace aqtruncat {

// A runner found on disk or in the bundled resources, not loaded yet.
struct RunnerEntry {
    QString id;
    QString path;
    RunnerSource source = RunnerSource::Bundled;
};

// $XDG_DATA_HOME/AQTRunCat/runners (falls back to ~/.local/share/AQTRunCat/runners).
QString userRunnersDirectory();

// Runner id for a directory name: the name itself, without the "-frames"
// suffix of Runner Gallery archives ("beagle-frames" → "beagle").
QString runnerIdForDirectoryName(const QString &name);

// Ids AQTRunCat creates itself (import): 1–100 characters, letters, digits,
// '.', '_' and '-', starting with a letter or digit. Always a safe single
// directory name. Directories copied by hand may use other names.
bool isValidRunnerId(const QString &id);

// Every runner directory directly under `root` (hidden entries skipped), in
// natural order, without resolving duplicate ids.
QList<RunnerEntry> runnerDirectories(const QString &root, RunnerSource source);

// Lists the runner directories under each root. Precedence is user > bundled:
// a user runner replaces a bundled one with the same id. Within one root, the
// first directory in natural order wins. Every shadowed directory is reported
// in *notes. The result is sorted by id.
QList<RunnerEntry> discoverRunners(const QString &bundledRoot, const QString &userRoot,
                                   QStringList *notes = nullptr);

struct RunnerChoice {
    std::optional<Runner> runner;
    QString error;                         // set when no runner could be chosen
    RunnerError errorKind = RunnerError::None;
    QStringList notes;                     // e.g. why the default was not used
};

// Picks the runner to show.
// - requestedId set: that runner or an error, never a fallback.
// - otherwise defaultId; if it is missing or invalid, the first valid runner
//   in id order (bundled before user), with a note saying so.
RunnerChoice chooseRunner(const QList<RunnerEntry> &entries, const QString &requestedId,
                          const QString &defaultId);

} // namespace aqtruncat
