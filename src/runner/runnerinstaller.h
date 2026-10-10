#pragma once

#include "runnererror.h"

#include <QString>

namespace aqtruncat {

struct InstallResult {
    RunnerError error = RunnerError::None;
    QString message;
    QString installedPath;      // <userRoot>/<id>
    QString replacedUserPath;   // previous user runner, when replaced
    bool ok() const { return error == RunnerError::None; }
};

// Installs the already validated runner in `sourceDir` as user runner `id`
// (<userRoot>/<id>). Knows nothing about archives or the command line.
//
// The runner is copied into a hidden staging directory inside userRoot (same
// filesystem), checked again with the loader, then renamed into place, so a
// failure never leaves a partial runner visible. An existing user runner with
// the same id is an error unless `replaceExisting` is set; in that case the old
// directory is moved aside first and restored if the final rename fails.
// A bundled runner with the same id is not touched: the user runner simply
// takes precedence (user > bundled).
InstallResult installRunner(const QString &sourceDir, const QString &id, const QString &userRoot,
                            bool replaceExisting);

} // namespace aqtruncat
