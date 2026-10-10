#include "runnerremover.h"

#include "runnercatalog.h"

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>

namespace aqtruncat {

namespace {

bool containsId(const QList<RunnerEntry> &entries, const QString &id)
{
    return std::any_of(entries.cbegin(), entries.cend(), [&id](const RunnerEntry &e) { return e.id == id; });
}

} // namespace

RemoveResult removeUserRunner(const QString &id, const QString &bundledRoot, const QString &userRoot)
{
    RemoveResult result;
    QList<RunnerEntry> matches;
    for (const RunnerEntry &entry : runnerDirectories(userRoot, RunnerSource::User))
        if (entry.id == id)
            matches.append(entry);
    const bool bundled = containsId(runnerDirectories(bundledRoot, RunnerSource::Bundled), id);

    if (matches.isEmpty()) {
        result.error = bundled ? RunnerError::Conflict : RunnerError::NotFound;
        result.message = bundled
            ? QStringLiteral("\"%1\" is a bundled runner and cannot be removed").arg(id)
            : QStringLiteral("no user runner \"%1\"").arg(id);
        return result;
    }
    if (matches.size() > 1) {
        result.error = RunnerError::Conflict;
        result.message = QStringLiteral("runner \"%1\" is provided by several directories (%2, %3); nothing removed")
                             .arg(id, matches[0].path, matches[1].path);
        return result;
    }

    const QString path = matches.first().path;
    // Hide first (discovery skips dot-directories), then delete.
    QTemporaryDir trash(QDir(userRoot).filePath(QStringLiteral(".aqtruncat-removing-XXXXXX")));
    if (!trash.isValid() || !QDir().rename(path, QDir(trash.path()).filePath(QStringLiteral("runner")))) {
        result.error = RunnerError::Filesystem;
        result.message = QStringLiteral("cannot remove %1").arg(path);
        return result;
    }
    if (!trash.remove()) {
        // The runner is already invisible; only disk space is left behind.
        result.message = QStringLiteral("removed, but could not delete %1").arg(trash.path());
    }
    result.removedPath = path;
    result.bundledVisibleAgain = bundled;
    return result;
}

} // namespace aqtruncat
