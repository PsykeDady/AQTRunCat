#include "runnerinstaller.h"

#include "runnercatalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace aqtruncat {

namespace {

InstallResult failure(RunnerError kind, QString message)
{
    InstallResult result;
    result.error = kind;
    result.message = std::move(message);
    return result;
}

// Copies regular files and directories, skipping hidden entries and symlinks.
bool copyTree(const QString &from, const QString &to, QString *error)
{
    const QDir source(from);
    QDirIterator it(from, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = source.relativeFilePath(info.filePath());
        if (relative.split(u'/').first().startsWith(u'.') || info.fileName().startsWith(u'.'))
            continue;
        const QString target = QDir(to).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(target)) {
                *error = QStringLiteral("cannot create %1").arg(target);
                return false;
            }
        } else {
            QDir().mkpath(QFileInfo(target).path());
            QFile file(info.filePath());
            if (!file.copy(target)) {
                *error = QStringLiteral("cannot copy %1: %2").arg(info.filePath(), file.errorString());
                return false;
            }
        }
    }
    return true;
}

} // namespace

InstallResult installRunner(const QString &sourceDir, const QString &id, const QString &userRoot,
                            bool replaceExisting)
{
    if (!isValidRunnerId(id))
        return failure(RunnerError::Invalid,
                       QStringLiteral("\"%1\" is not a valid runner id (letters, digits, '.', '_' and '-', "
                                      "starting with a letter or digit, at most 100 characters)").arg(id));
    if (!QDir().mkpath(userRoot))
        return failure(RunnerError::Filesystem, QStringLiteral("cannot create %1").arg(userRoot));

    // Existing user runners with this id ("beagle/" or "beagle-frames/").
    QList<RunnerEntry> existing;
    for (const RunnerEntry &entry : runnerDirectories(userRoot, RunnerSource::User))
        if (entry.id == id)
            existing.append(entry);
    if (existing.size() > 1)
        return failure(RunnerError::Conflict,
                       QStringLiteral("several user directories provide runner \"%1\" (%2, %3); remove the extra ones first")
                           .arg(id, existing[0].path, existing[1].path));
    if (!existing.isEmpty() && !replaceExisting)
        return failure(RunnerError::Conflict,
                       QStringLiteral("runner \"%1\" is already installed at %2").arg(id, existing.first().path));

    const QString destination = QDir(userRoot).filePath(id);
    if (QFileInfo::exists(destination) && (existing.isEmpty() || existing.first().path != destination))
        return failure(RunnerError::Conflict, QStringLiteral("%1 already exists and is not a runner").arg(destination));

    // Stage next to the destination: hidden (ignored by discovery) and on the
    // same filesystem, so the final rename is atomic.
    QTemporaryDir staging(QDir(userRoot).filePath(QStringLiteral(".aqtruncat-install-XXXXXX")));
    if (!staging.isValid())
        return failure(RunnerError::Filesystem, QStringLiteral("cannot create a staging directory in %1: %2")
                                                    .arg(userRoot, staging.errorString()));
    QString error;
    if (!copyTree(sourceDir, staging.path(), &error))
        return failure(RunnerError::Filesystem, error);
    if (const RunnerLoadResult check = loadRunner(staging.path(), id, RunnerSource::User); !check.isValid())
        return failure(RunnerError::Invalid, QStringLiteral("installed copy is not valid: %1").arg(check.error));

    InstallResult result;
    result.installedPath = destination;

    if (existing.isEmpty()) {
        if (!QDir().rename(staging.path(), destination))
            return failure(RunnerError::Filesystem, QStringLiteral("cannot move the runner to %1").arg(destination));
        staging.setAutoRemove(false);
        return result;
    }

    // Replacement: move the old runner aside, put the new one in place,
    // restore the old one if that fails, and only then delete it.
    const QString oldPath = existing.first().path;
    QTemporaryDir backupHolder(QDir(userRoot).filePath(QStringLiteral(".aqtruncat-replaced-XXXXXX")));
    if (!backupHolder.isValid())
        return failure(RunnerError::Filesystem, QStringLiteral("cannot create a backup directory in %1").arg(userRoot));
    const QString backup = QDir(backupHolder.path()).filePath(QStringLiteral("old"));
    if (!QDir().rename(oldPath, backup))
        return failure(RunnerError::Filesystem, QStringLiteral("cannot move the old runner %1 aside").arg(oldPath));
    if (!QDir().rename(staging.path(), destination)) {
        const bool restored = QDir().rename(backup, oldPath);
        if (!restored)
            backupHolder.setAutoRemove(false); // never delete the only copy of the old runner
        return failure(RunnerError::Filesystem,
                       QStringLiteral("cannot move the new runner to %1; old runner %2")
                           .arg(destination, restored ? QStringLiteral("restored") : QStringLiteral("left in ") + backup));
    }
    staging.setAutoRemove(false);
    result.replacedUserPath = oldPath; // backupHolder removes the old copy on return
    return result;
}

} // namespace aqtruncat
