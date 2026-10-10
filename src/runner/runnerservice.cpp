#include "runnerservice.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <algorithm>

namespace aqtruncat {

namespace {

ImportResult importFailure(RunnerError kind, QString message)
{
    ImportResult result;
    result.error = kind;
    result.message = std::move(message);
    return result;
}

} // namespace

RunnerService::RunnerService(RunnerLocations locations)
    : m_locations(std::move(locations))
{
}

QList<RunnerEntry> RunnerService::entries(QStringList *notes) const
{
    return discoverRunners(m_locations.bundledRoot, m_locations.userRoot, notes);
}

QList<ListedRunner> RunnerService::list() const
{
    QList<ListedRunner> listed;
    for (const RunnerEntry &entry : entries())
        listed.append({entry, loadRunner(entry.path, entry.id, entry.source)});
    return listed;
}

RunnerChoice RunnerService::choose(const QString &requestedId, const QString &defaultId) const
{
    return chooseRunner(entries(), requestedId, defaultId);
}

ImportResult RunnerService::importArchive(const QString &archivePath, const ImportOptions &options) const
{
    QElapsedTimer timer;
    timer.start();

    // 1. Extraction into a private temporary directory, removed on return.
    QTemporaryDir workspace;
    if (!workspace.isValid())
        return importFailure(RunnerError::Filesystem,
                             QStringLiteral("cannot create a temporary directory: %1").arg(workspace.errorString()));
    ExtractionResult extraction = extractRunnerArchive(archivePath, workspace.path(), options.limits);
    if (!extraction.runner)
        return importFailure(extraction.errorKind, extraction.error);
    const qint64 extractMs = timer.restart();

    // 2. Validation of what was extracted.
    const QString id = options.id.isEmpty() ? extraction.runner->suggestedId : options.id;
    if (!isValidRunnerId(id))
        return importFailure(RunnerError::Invalid,
                             QStringLiteral("cannot use \"%1\" as runner id; choose one with --id "
                                            "(letters, digits, '.', '_' and '-')").arg(id));
    RunnerLoadResult loaded = loadRunner(extraction.runner->runnerRoot, id, RunnerSource::User);
    if (!loaded.isValid())
        return importFailure(RunnerError::Invalid, QStringLiteral("the archive is not a valid runner: %1").arg(loaded.error));
    const qint64 validateMs = timer.restart();

    // 3. Installation.
    const InstallResult installed = installRunner(extraction.runner->runnerRoot, id, m_locations.userRoot,
                                                  options.replaceExisting);
    if (!installed.ok())
        return importFailure(installed.error, installed.message);

    ImportResult result;
    result.runner = std::move(loaded.runner);
    result.runner->path = installed.installedPath;
    result.installedPath = installed.installedPath;
    result.replacedUserPath = installed.replacedUserPath;
    const QList<RunnerEntry> bundled = runnerDirectories(m_locations.bundledRoot, RunnerSource::Bundled);
    result.overridesBundled = std::any_of(bundled.cbegin(), bundled.cend(),
                                          [&id](const RunnerEntry &e) { return e.id == id; });
    result.skippedEntries = extraction.runner->skipped;
    result.extractMs = extractMs;
    result.validateMs = validateMs;
    result.installMs = timer.elapsed();
    return result;
}

RemoveResult RunnerService::remove(const QString &id) const
{
    return removeUserRunner(id, m_locations.bundledRoot, m_locations.userRoot);
}

} // namespace aqtruncat
