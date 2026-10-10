#pragma once

#include "runnererror.h"

#include <QString>

namespace aqtruncat {

struct RemoveResult {
    RunnerError error = RunnerError::None;
    QString message;
    QString removedPath;
    bool bundledVisibleAgain = false; // a bundled runner with the same id is used again
    bool ok() const { return error == RunnerError::None; }
};

// Removes the user runner `id` from userRoot. Bundled runners are never
// removed: if `id` exists only as a bundled runner the result is a Conflict.
// If both exist, only the user override is removed and the bundled runner
// becomes visible again through the normal precedence. An id provided by
// several user directories is ambiguous and nothing is removed.
// The directory is first renamed to a hidden name (invisible to discovery),
// then deleted, so an interrupted removal never leaves a half runner visible.
RemoveResult removeUserRunner(const QString &id, const QString &bundledRoot, const QString &userRoot);

} // namespace aqtruncat
