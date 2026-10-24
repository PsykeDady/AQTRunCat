#include "custommetricsservice.h"

#include "appsettings.h"
#include "custommetricpaths.h"
#include "processgroup.h"

#include "custommetrics/metricgroup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QLoggingCategory>
#include <QProcess>
#include <QSet>

#include <algorithm>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include <csignal>
#include <unistd.h>

using namespace aqtruncat::custommetrics;

// v2F1.3: one line when a source starts failing (or fails differently) and
// one when it recovers; never per run, never the command's output.
Q_STATIC_LOGGING_CATEGORY(lcCustomMetrics, "aqtruncat.custommetrics", QtInfoMsg)

namespace {
// The first non-empty stderr line, bounded: enough to say why, without
// carrying a script's whole diagnostic output into every frontend.
constexpr qsizetype MaxDiagnosticCharacters = 200;

QString diagnosticLine(const QByteArray &standardError)
{
    for (const QString &line : QString::fromUtf8(standardError).split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed.size() > MaxDiagnosticCharacters
                ? trimmed.left(MaxDiagnosticCharacters - 1) + QChar(0x2026) : trimmed;
    }
    return {};
}

constexpr int DebounceMilliseconds = 100;
constexpr int RetryMilliseconds = 200;
constexpr int RetryCount = 3;
constexpr int RecoveryMilliseconds = 5000;
// After SIGKILL the scheduler re-checks a not-yet-reaped process at this
// pace instead of spinning on an already expired deadline.
constexpr int KillGraceMilliseconds = 1000;
constexpr int StopWaitMilliseconds = 250;

// Every command runs as the leader of its own process group, so a timeout,
// an output overflow or shutdown also reaches helpers spawned by a script.
void signalProcessGroup(const QProcess &process, int signal)
{
    aqtruncat::process::signalGroup(process, signal);
}
}

struct CustomMetricsService::CommandJob {
    CommandDefinition definition;
    std::unique_ptr<QProcess> process;
    QByteArray standardOutput;
    QByteArray standardError;
    qint64 nextDueMilliseconds = 0;
    qint64 deadlineMilliseconds = 0;
    // v2F1.6: when the current run started and the last one ended (-1:
    // never), so a reload can move the deadline and the next run of a
    // changed interval or timeout without restarting the job.
    qint64 startedMilliseconds = -1;
    qint64 finishedMilliseconds = -1;
    // The current run's id: callbacks of an earlier process (this job's
    // before a reload replaced its definition) never touch this one.
    quint64 run = 0;
    QString forcedError;
    // The last result of this job, for logging transitions only.
    State lastOutcome = State::Pending;
    QString lastMessage;
};

CustomMetricsService::CustomMetricsService(aqtruncat::AppSettings *settings,
                                           QString discoveryDirectory, QObject *parent)
    : CustomMetricsModel(parent)
    , m_settings(settings)
    , m_discoveryDirectory(discoveryDirectory.isEmpty() ? customMetricsDirectory()
                                                        : absolutePath(discoveryDirectory))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(DebounceMilliseconds);
    m_retry.setSingleShot(true);
    m_retry.setInterval(RetryMilliseconds);
    m_recovery.setInterval(RecoveryMilliseconds);
    m_commandWake.setSingleShot(true);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReload(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        m_retryAttempt = 0;
        reload(false);
    });
    connect(&m_retry, &QTimer::timeout, this, [this] { reload(++m_retryAttempt >= RetryCount); });
    connect(&m_recovery, &QTimer::timeout, this, [this] { reload(true); });
    connect(&m_commandWake, &QTimer::timeout, this, &CustomMetricsService::processCommandWake);
    connect(m_settings, &aqtruncat::AppSettings::customMetricFilesChanged,
            this, [this] { scheduleReload(); });
}

CustomMetricsService::~CustomMetricsService()
{
    stop();
}

QString CustomMetricsService::absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString CustomMetricsService::identityFor(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? absolutePath(path) : canonical;
}

bool CustomMetricsService::registerFile(const QString &path, QString *error)
{
    if (path.trimmed().isEmpty()) {
        if (error)
            *error = tr("The file path is empty.");
        return false;
    }
    const QString absolute = absolutePath(path);
    const QFileInfo info(absolute);
    if (info.exists()) {
        if (!info.isFile()) {
            if (error)
                *error = tr("The selected path is not a regular file.");
            return false;
        }
        const ConfigObject config = readConfigObject(absolute);
        const bool group = config.object && isGroupObject(*config.object);
        const CommandParseResult command = group ? CommandParseResult{} : parseCommandFile(absolute);
        const ParseResult parsed = group || command.recognized ? ParseResult{} : parseFile(absolute);
        if (group && info.fileName() != QLatin1String("metric.json")) {
            if (error)
                *error = tr("A group must be a package's metric.json (its children are the package's subdirectories).");
            return false;
        }
        if (!group && ((command.recognized && !command.definition)
                       || (!command.recognized && !parsed.snapshot))) {
            if (error)
                *error = command.recognized ? command.error : parsed.error;
            return false;
        }
    }
    QStringList files = m_settings->customMetricFiles();
    const QString identity = identityFor(absolute);
    for (const QString &file : std::as_const(files)) {
        if (identityFor(file) == identity)
            return true;
    }
    files.append(absolute);
    m_settings->setCustomMetricFiles(files);
    if (m_active)
        reload(true);
    return true;
}

void CustomMetricsService::unregisterFile(const QString &path)
{
    QStringList files = m_settings->customMetricFiles();
    const QString identity = identityFor(path);
    files.erase(std::remove_if(files.begin(), files.end(), [&](const QString &file) {
        return identityFor(file) == identity;
    }), files.end());
    m_settings->setCustomMetricFiles(files);
    if (m_active)
        reload(true);
}

bool CustomMetricsService::setEnabled(const QString &path, bool enabled, QString *error)
{
    const QString identity = identityFor(path);
    const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(), [&](const Source &entry) {
        return entry.id == identity || entry.path == absolutePath(path);
    });
    if (source == m_sources.cend()) {
        if (error)
            *error = tr("%1 is not a known Custom Metrics source.").arg(path);
        return false;
    }
    if (!source->integration.isEmpty()) {
        if (error)
            *error = tr("%1 belongs to an integration: turn it on or off in Integrations.").arg(path);
        return false;
    }
    if (!writeEnabled(source->path, enabled, error))
        return false;
    // Applied now, not on the file watcher's next notification.
    if (m_active)
        reload(true);
    return true;
}

void CustomMetricsService::start()
{
    if (m_active)
        return;
    m_active = true;
    m_clock.start();
    QDir().mkpath(m_discoveryDirectory);
    reload(true);
}

void CustomMetricsService::stop()
{
    m_active = false;
    m_debounce.stop();
    m_retry.stop();
    m_recovery.stop();
    m_commandWake.stop();
    stopCommands();
    const QStringList files = m_watcher.files();
    const QStringList directories = m_watcher.directories();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    if (!directories.isEmpty())
        m_watcher.removePaths(directories);
}

void CustomMetricsService::rescan()
{
    if (m_active)
        reload(true);
}

void CustomMetricsService::reloadConfigurations()
{
    const ReloadReport report = reloadNow();
    emit reloadFinished(report);
}

ReloadReport CustomMetricsService::reloadNow()
{
    if (!m_active) {
        ReloadReport report;
        report.error = tr("Custom Metrics are not running.");
        return report;
    }
    // Whatever the watcher had pending is part of this reload.
    m_debounce.stop();
    m_retry.stop();
    m_retryAttempt = 0;
    const ReloadReport report = reload(true);
    if (report.ok)
        qCInfo(lcCustomMetrics).noquote() << reloadSummary(report);
    return report;
}

void CustomMetricsService::scheduleReload()
{
    if (m_active)
        m_debounce.start();
}

namespace {
QByteArray compactJson(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString reportName(const Source &source)
{
    return source.configuredId.isEmpty() ? source.path : source.configuredId;
}
}

ReloadReport CustomMetricsService::reload(bool finalAttempt)
{
    ReloadReport report;
    if (!m_active) {
        report.error = tr("Custom Metrics are not running.");
        return report;
    }
    if (m_reloading) {
        // Never inside another reload (it is synchronous; this would only be
        // a re-entrant call): the request is folded into a reload right after.
        m_debounce.start();
        report.error = tr("A reload is already running; it is repeated when it ends.");
        return report;
    }
    m_reloading = true;
    m_reloadFailure.clear();

    // v2F1.6: a directory that exists but cannot be listed would make every
    // source below it look removed. Such a reload is not applied (a missing
    // discovery directory is just empty: nothing is configured there).
    const QFileInfo root(m_discoveryDirectory);
    if (root.exists() && (!root.isDir() || !root.isReadable() || !root.isExecutable()))
        m_reloadFailure = tr("Cannot read the directory %1.").arg(m_discoveryDirectory);

    struct Candidate { QString path; QString explicitPath; bool explicitFile; bool discovered; };
    QList<Candidate> candidates;
    QHash<QString, int> byIdentity;

    for (const QString &registered : m_settings->customMetricFiles()) {
        const QString path = absolutePath(registered);
        const QString id = identityFor(path);
        const auto existing = byIdentity.constFind(id);
        if (existing != byIdentity.cend()) {
            candidates[*existing].explicitFile = true;
            candidates[*existing].explicitPath = registered;
            continue;
        }
        byIdentity.insert(id, candidates.size());
        candidates.append({path, registered, true, false});
    }

    QDir discovery(m_discoveryDirectory);
    const QFileInfoList discovered = discovery.entryInfoList({QStringLiteral("*.json")},
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : discovered) {
        const QString path = info.absoluteFilePath();
        const QString id = identityFor(path);
        const auto existing = byIdentity.constFind(id);
        if (existing != byIdentity.cend()) {
            candidates[*existing].discovered = true;
            continue;
        }
        byIdentity.insert(id, candidates.size());
        candidates.append({path, {}, false, true});
    }

    const QFileInfoList packages = discovery.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                            QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &directory : packages) {
        if ((!directory.isReadable() || !directory.isExecutable()) && m_reloadFailure.isEmpty())
            m_reloadFailure = tr("Cannot read the directory %1.").arg(directory.absoluteFilePath());
        const QString path = directory.absoluteFilePath() + QStringLiteral("/metric.json");
        if (!QFileInfo(path).isFile())
            continue;
        const QString id = identityFor(path);
        const auto existing = byIdentity.constFind(id);
        if (existing != byIdentity.cend()) {
            candidates[*existing].discovered = true;
            continue;
        }
        byIdentity.insert(id, candidates.size());
        candidates.append({path, {}, false, true});
    }

    // The candidate configuration is built aside; m_sources and the jobs
    // only change once it is complete and applicable.
    QList<Source> next;
    QList<CommandDefinition> commandDefinitions;
    bool retryNeeded = false;
    for (const Candidate &candidate : std::as_const(candidates)) {
        Source source;
        source.id = identityFor(candidate.path);
        source.path = candidate.path;
        source.explicitPath = candidate.explicitPath;
        source.explicitlyRegistered = candidate.explicitFile;
        source.discovered = candidate.discovered;

        const Source *previous = previousFor(source);
        if (previous)
            source.snapshot = previous->snapshot;

        const QFileInfo info(candidate.path);
        if (!info.exists() || !info.isFile()) {
            source.message = tr("File is not available.");
            source.configuration = source.message.toUtf8();
            if (!finalAttempt && source.explicitlyRegistered && source.snapshot) {
                source.state = State::Pending;
                retryNeeded = true;
            } else {
                source.state = State::Unavailable;
            }
            next.append(std::move(source));
            continue;
        }
        const ConfigObject config = readConfigObject(candidate.path);
        source.configuration = config.object ? compactJson(*config.object) : config.error.toUtf8();
        if (config.object && isGroupObject(*config.object)) {
            // v2F1.5: a group and, recursively, its children.
            appendGroup(std::move(source), *config.object, next, commandDefinitions);
            continue;
        }
        const CommandParseResult command = parseCommandFile(candidate.path);
        if (command.recognized) {
            applyCommand(source, command, previous, commandDefinitions);
        } else {
            const ParseResult parsed = parseFile(candidate.path);
            if (parsed.snapshot && !parsed.enabled) {
                disable(source, parsed.snapshot->title, parsed.snapshot->symbol, parsed.snapshot->icon);
                source.configuredEnabled = false;
            } else if (parsed.snapshot) {
                source.snapshot = parsed.snapshot;
                source.state = State::Available;
            } else {
                source.message = parsed.error;
                if (!finalAttempt) {
                    source.state = State::Pending;
                    retryNeeded = true;
                } else {
                    source.state = State::Error;
                }
            }
        }
        next.append(std::move(source));
    }

    if (!m_reloadFailure.isEmpty()) {
        // Not applied: the active sources, jobs and watches stay as they are.
        report.error = m_reloadFailure;
        if (report.error != m_loggedFailure)
            qCWarning(lcCustomMetrics).noquote()
                << QStringLiteral("Custom Metrics configuration not reloaded: %1").arg(report.error);
        m_loggedFailure = report.error;
        m_reloading = false;
        if (!finalAttempt)
            m_retry.start();
        return report;
    }
    m_loggedFailure.clear();

    // What changed, by stable source id.
    report.ok = true;
    QHash<QString, const Source *> before;
    for (const Source &old : std::as_const(m_sources))
        before.insert(old.id, &old);
    QSet<QString> scheduled;
    for (const CommandDefinition &definition : std::as_const(commandDefinitions))
        scheduled.insert(identityFor(definition.configurationPath));
    for (const Source &source : std::as_const(next)) {
        const ReloadEntry entry{source.id, reportName(source), {}};
        const Source *old = before.take(source.id);
        if (!old)
            report.added.append(entry);
        else if (old->configuration != source.configuration || old->type != source.type
                 || old->parentId != source.parentId)
            report.changed.append(entry);
        else
            ++report.unchanged;
        // Configuration errors only; a scheduled command's last run may
        // have failed too, which is not the configuration's fault.
        if (source.state == State::Error && !scheduled.contains(source.id))
            report.errors.append({source.id, reportName(source), source.message});
    }
    for (const Source &old : std::as_const(m_sources)) {
        if (before.contains(old.id))
            report.removed.append({old.id, reportName(old), {}});
    }

    QHash<QString, QStringList> warningsBefore;
    for (const Source &old : std::as_const(m_sources))
        warningsBefore.insert(old.id, old.warnings);
    const QHash<QString, QString> iconWarningsBefore = std::exchange(m_iconWarnings, {});
    m_sources = std::move(next);
    syncCommandJobs(commandDefinitions);
    // v2F1.6: what does not stop a source: logged when it appears, listed
    // in the report (every reload, like the errors) until it is fixed.
    IconThemeLookup themes;
    for (Source &source : m_sources) {
        if (source.snapshot && source.snapshot->icon) {
            const QString warning = iconWarning(*source.snapshot->icon, themes);
            if (!warning.isEmpty())
                m_iconWarnings.insert(source.id, warning);
            if (!warning.isEmpty() && iconWarningsBefore.value(source.id) != warning)
                qCWarning(lcCustomMetrics).noquote()
                    << QStringLiteral("custom metric \"%1\": %2").arg(reportName(source), warning);
        }
        updateWarnings(source, warningsBefore.value(source.id));
        for (const QString &warning : std::as_const(source.warnings))
            report.warnings.append({source.id, reportName(source), warning});
    }
    rebuildWatches();
    publish();
    m_reloading = false;

    if (retryNeeded && !finalAttempt)
        m_retry.start();
    else
        m_retry.stop();
    if (hasDegradedExplicitSource() || std::any_of(m_sources.cbegin(), m_sources.cend(), [](const Source &s) {
            return s.type == SourceType::Snapshot && s.enabled && s.state == State::Error;
        }))
        m_recovery.start();
    else
        m_recovery.stop();
    return report;
}

void CustomMetricsService::rebuildWatches()
{
    const QStringList oldFiles = m_watcher.files();
    const QStringList oldDirectories = m_watcher.directories();
    if (!oldFiles.isEmpty())
        m_watcher.removePaths(oldFiles);
    if (!oldDirectories.isEmpty())
        m_watcher.removePaths(oldDirectories);

    QSet<QString> files;
    QSet<QString> directories{m_discoveryDirectory};
    // Package directories are watched even before they contain metric.json,
    // so `mkdir pkg && cp metric.json pkg/` is noticed like a standalone file.
    const QFileInfoList packages = QDir(m_discoveryDirectory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &package : packages)
        directories.insert(package.absoluteFilePath());
    for (const Source &source : std::as_const(m_sources)) {
        const QFileInfo info(source.path);
        // An unreadable file cannot be watched (inotify refuses it and Qt warns
        // on every attempt); its directory stays watched and the degraded-only
        // recovery timer notices when it becomes readable again.
        if (info.exists() && info.isFile() && info.isReadable())
            files.insert(source.path);
        QString parent = info.absolutePath();
        while (!parent.isEmpty() && !QFileInfo(parent).isDir()) {
            const QString next = QFileInfo(parent).absolutePath();
            if (next == parent)
                break;
            parent = next;
        }
        if (!parent.isEmpty())
            directories.insert(parent);
        if (!source.id.isEmpty())
            directories.insert(QFileInfo(source.id).absolutePath());
    }
    QStringList fileList(files.cbegin(), files.cend());
    QStringList directoryList(directories.cbegin(), directories.cend());
    fileList.removeIf([](const QString &path) { return !QFileInfo(path).isFile(); });
    directoryList.removeIf([](const QString &path) { return !QFileInfo(path).isDir(); });
    if (!fileList.isEmpty())
        m_watcher.addPaths(fileList);
    if (!directoryList.isEmpty())
        m_watcher.addPaths(directoryList);
}

bool CustomMetricsService::hasDegradedExplicitSource() const
{
    return std::any_of(m_sources.cbegin(), m_sources.cend(), [](const Source &source) {
        return source.type == SourceType::Snapshot && source.explicitlyRegistered && source.enabled
            && source.state != State::Available;
    });
}

namespace {
// v2F1.6: how a command's definition changed. Presentation: what the
// frontends show (title, icon, showBar, id, provenance); Schedule: also the
// interval or the timeout; Execution: anything that changes what runs or
// how its output is read.
enum class DefinitionChange { None, Presentation, Schedule, Execution };

DefinitionChange definitionChange(const CommandDefinition &before, const CommandDefinition &after)
{
    if (before == after)
        return DefinitionChange::None;
    CommandDefinition normalized = after;
    normalized.id = before.id;
    normalized.title = before.title;
    normalized.integration = before.integration;
    normalized.icon = before.icon;
    normalized.legacySymbol = before.legacySymbol;
    normalized.configuredExecutable = before.configuredExecutable; // the same resolved executable
    normalized.showBar = before.showBar;
    if (normalized == before)
        return DefinitionChange::Presentation;
    normalized.intervalSeconds = before.intervalSeconds;
    normalized.timeoutSeconds = before.timeoutSeconds;
    return normalized == before ? DefinitionChange::Schedule : DefinitionChange::Execution;
}

// The last result keeps its values and takes the new presentation, as the
// next run would give it (snapshotFromCommandOutput: the configuration owns
// title, icon and showBar; a scalar's item is named after the title).
void applyPresentation(Snapshot &snapshot, const CommandDefinition &definition)
{
    snapshot.title = definition.title;
    snapshot.symbol = definition.legacySymbol;
    snapshot.icon = definition.icon;
    snapshot.showBar = definition.showBar;
    if (definition.output == CommandOutput::Scalar) {
        for (Item &item : snapshot.metrics)
            item.title = definition.title;
    }
}
}

void CustomMetricsService::syncCommandJobs(const QList<CommandDefinition> &definitions)
{
    std::set<QString> wanted;
    for (const CommandDefinition &definition : definitions) {
        const QString id = identityFor(definition.configurationPath);
        wanted.insert(id);
        const auto existing = m_commandJobs.find(id);
        const DefinitionChange change = existing != m_commandJobs.end()
            ? definitionChange(existing->second->definition, definition) : DefinitionChange::Execution;
        if (change == DefinitionChange::None)
            continue;
        if (change != DefinitionChange::Execution) {
            // The job goes on: no restart, its last values and its running
            // process are kept; only what was changed is applied.
            CommandJob &job = *existing->second;
            job.definition = definition;
            if (change == DefinitionChange::Schedule) {
                if (job.process && job.forcedError.isEmpty())
                    job.deadlineMilliseconds = job.startedMilliseconds + qint64(definition.timeoutSeconds) * 1000;
                else if (!job.process && job.finishedMilliseconds >= 0)
                    job.nextDueMilliseconds = job.finishedMilliseconds + qint64(definition.intervalSeconds) * 1000;
            }
            if (Source *source = sourceFor(id); source && source->snapshot)
                applyPresentation(*source->snapshot, definition);
            continue;
        }
        if (existing != m_commandJobs.end()) {
            stopCommand(*existing->second);
            m_commandJobs.erase(existing);
        }
        auto job = std::make_unique<CommandJob>();
        job->definition = definition;
        job->nextDueMilliseconds = 0; // new or changed definitions run immediately
        m_commandJobs.emplace(id, std::move(job));

        if (Source *source = sourceFor(id)) {
            Snapshot skeleton;
            skeleton.title = definition.title;
            skeleton.symbol = definition.legacySymbol;
            skeleton.icon = definition.icon;
            skeleton.showBar = definition.showBar;
            source->snapshot = std::move(skeleton);
            source->state = State::Pending;
            source->message = tr("Waiting for the first command result.");
        }
    }

    for (auto it = m_commandJobs.begin(); it != m_commandJobs.end();) {
        if (wanted.contains(it->first)) {
            ++it;
            continue;
        }
        stopCommand(*it->second);
        it = m_commandJobs.erase(it);
    }
    scheduleCommandWake();
}

void CustomMetricsService::scheduleCommandWake()
{
    m_commandWake.stop();
    if (!m_active || m_commandJobs.empty())
        return;

    const qint64 now = m_clock.elapsed();
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (const auto &[id, job] : m_commandJobs) {
        Q_UNUSED(id);
        earliest = std::min(earliest, job->process ? job->deadlineMilliseconds
                                                   : job->nextDueMilliseconds);
    }
    const qint64 delay = std::clamp(earliest - now, qint64(0), qint64(std::numeric_limits<int>::max()));
    m_commandWake.start(int(delay));
}

void CustomMetricsService::processCommandWake()
{
    if (!m_active)
        return;
    const qint64 now = m_clock.elapsed();
    QStringList due;
    for (auto &[id, job] : m_commandJobs) {
        if (job->process) {
            if (job->deadlineMilliseconds <= now) {
                if (job->forcedError.isEmpty())
                    job->forcedError = tr("Command timed out after %1 seconds.")
                                           .arg(job->definition.timeoutSeconds);
                signalProcessGroup(*job->process, SIGKILL);
                job->deadlineMilliseconds = now + KillGraceMilliseconds;
            }
        } else if (job->nextDueMilliseconds <= now) {
            due.append(id);
        }
    }
    for (const QString &id : std::as_const(due))
        startCommand(id);
    scheduleCommandWake();
}

void CustomMetricsService::startCommand(const QString &id)
{
    const auto found = m_commandJobs.find(id);
    if (found == m_commandJobs.end() || found->second->process)
        return;
    CommandJob &job = *found->second;
    // v2F1.3: a program that cannot be run is an execution error, not the
    // intentional Unavailable of exit status 69.
    const QFileInfo executable(job.definition.executable);
    if (!executable.exists() || !executable.isFile()) {
        failCommand(id, State::Error,
                    tr("Executable not found: %1").arg(job.definition.executable));
        return;
    }
    if (!executable.isExecutable()) {
        failCommand(id, State::Error,
                    tr("Permission denied: %1 is not executable.").arg(job.definition.executable));
        return;
    }

    job.standardOutput.clear();
    job.standardError.clear();
    job.forcedError.clear();
    job.run = ++m_nextRun;
    const quint64 run = job.run;
    job.process = std::make_unique<QProcess>();
    QProcess *process = job.process.get();
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setWorkingDirectory(job.definition.workingDirectory);
    aqtruncat::process::runInOwnGroup(*process);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, run] { readCommandOutput(id, run); });
    connect(process, &QProcess::readyReadStandardError, this, [this, id, run] { readCommandOutput(id, run); });
    connect(process, &QProcess::finished, this,
            [this, id, run](int exitCode, QProcess::ExitStatus status) { finishCommand(id, run, exitCode, status); });
    connect(process, &QProcess::errorOccurred, this, [this, id, run](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        CommandJob *job = runningJob(id, run);
        if (!job)
            return;
        job->forcedError = tr("Could not start executable: %1").arg(job->process->errorString());
        // FailedToStart may be emitted synchronously from QProcess::start().
        // Defer teardown so the emitting QProcess is never destroyed inside
        // its own signal delivery; by then a reload may have replaced the
        // job, whose new run this must not finish (run id).
        QTimer::singleShot(0, this, [this, id, run] { finishCommand(id, run, -1, QProcess::CrashExit); });
    });

    if (Source *source = sourceFor(id)) {
        source->state = State::Pending;
        source->message = tr("Command is running.");
    }
    publish();
    job.startedMilliseconds = m_clock.elapsed();
    process->start(job.definition.executable, job.definition.arguments, QIODevice::ReadOnly);
    job.deadlineMilliseconds = job.startedMilliseconds + qint64(job.definition.timeoutSeconds) * 1000;
}

CustomMetricsService::CommandJob *CustomMetricsService::runningJob(const QString &id, quint64 run)
{
    const auto found = m_commandJobs.find(id);
    if (found == m_commandJobs.end() || !found->second->process || found->second->run != run)
        return nullptr;
    return found->second.get();
}

void CustomMetricsService::readCommandOutput(const QString &id, quint64 run)
{
    CommandJob *running = runningJob(id, run);
    if (!running)
        return;
    CommandJob &job = *running;
    const QByteArray out = job.process->readAllStandardOutput();
    const QByteArray err = job.process->readAllStandardError();
    if (job.standardOutput.size() + out.size() > MaxCommandOutputBytes
        || job.standardError.size() + err.size() > MaxCommandOutputBytes) {
        if (job.forcedError.isEmpty())
            job.forcedError = tr("Command output exceeds the %1 byte per-stream limit.")
                                  .arg(MaxCommandOutputBytes);
        signalProcessGroup(*job.process, SIGKILL);
        return;
    }
    job.standardOutput.append(out);
    job.standardError.append(err);
}

void CustomMetricsService::finishCommand(const QString &id, quint64 run, int exitCode,
                                         QProcess::ExitStatus exitStatus)
{
    CommandJob *running = runningJob(id, run);
    if (!running)
        return;
    CommandJob &job = *running;
    readCommandOutput(id, run);

    QString error = job.forcedError;
    State failure = State::Error;
    if (error.isEmpty() && exitStatus != QProcess::NormalExit)
        error = tr("Command crashed.");
    if (error.isEmpty() && exitCode == UnavailableExitStatus) {
        failure = State::Unavailable;
        error = tr("Source is not available.");
        const QString diagnostic = QString::fromUtf8(job.standardError).trimmed();
        if (!diagnostic.isEmpty())
            error = diagnostic.left(512);
    } else if (error.isEmpty() && exitCode != 0) {
        error = tr("Command exited with status %1.").arg(exitCode);
        const QString diagnostic = diagnosticLine(job.standardError);
        if (!diagnostic.isEmpty())
            error += QStringLiteral(" ") + diagnostic;
    }

    std::optional<Snapshot> snapshot;
    if (error.isEmpty()) {
        const ScalarResult parsed = snapshotFromCommandOutput(job.definition, job.standardOutput);
        snapshot = parsed.snapshot;
        if (!parsed.error.isEmpty())
            error = tr("Invalid output: %1").arg(parsed.error);
    }
    job.process->disconnect(this);
    job.process.reset();
    job.finishedMilliseconds = m_clock.elapsed();
    job.nextDueMilliseconds = job.finishedMilliseconds + qint64(job.definition.intervalSeconds) * 1000;

    if (Source *source = sourceFor(id)) {
        if (snapshot) {
            source->snapshot = std::move(snapshot);
            setOutcome(*source, State::Available, {}); // clears the previous warning
        } else {
            setOutcome(*source, failure, error);
        }
    }
    publish();
    scheduleCommandWake();
}

void CustomMetricsService::failCommand(const QString &id, State state, const QString &message)
{
    const auto found = m_commandJobs.find(id);
    if (found == m_commandJobs.end())
        return;
    found->second->finishedMilliseconds = m_clock.elapsed();
    found->second->nextDueMilliseconds = found->second->finishedMilliseconds
        + qint64(found->second->definition.intervalSeconds) * 1000;
    if (Source *source = sourceFor(id))
        setOutcome(*source, state, message);
    publish();
    scheduleCommandWake();
}

void CustomMetricsService::setOutcome(Source &source, State state, const QString &message)
{
    // The state before this run started (it is Pending while running).
    const auto job = m_commandJobs.find(source.id);
    const State before = job != m_commandJobs.end() ? job->second->lastOutcome : source.state;
    const QString beforeMessage = job != m_commandJobs.end() ? job->second->lastMessage : source.message;
    const QString name = source.configuredId.isEmpty() ? source.path : source.configuredId;
    if (state == State::Error && (before != State::Error || beforeMessage != message))
        qCWarning(lcCustomMetrics).noquote() << QStringLiteral("custom metric \"%1\" failed: %2").arg(name, message);
    else if (state != State::Error && before == State::Error)
        qCInfo(lcCustomMetrics).noquote() << QStringLiteral("custom metric \"%1\" recovered").arg(name);
    source.state = state;
    source.message = message;
    if (job != m_commandJobs.end()) {
        job->second->lastOutcome = state;
        job->second->lastMessage = message;
    }
    updateWarnings(source, source.warnings);
}

void CustomMetricsService::disable(Source &source, const QString &title, const std::optional<QString> &symbol,
                                   const std::optional<IconReference> &icon)
{
    // Its name and icon (for Settings and the widget's configuration), no
    // values: the same skeleton as a command waiting for its first result.
    source.enabled = false;
    Snapshot skeleton;
    skeleton.title = title;
    skeleton.symbol = symbol;
    skeleton.icon = icon;
    source.snapshot = std::move(skeleton);
    source.state = State::Unavailable;
    source.message = tr("Disabled.");
}

const Source *CustomMetricsService::previousFor(const Source &source) const
{
    const auto previous = std::find_if(m_sources.cbegin(), m_sources.cend(), [&](const Source &old) {
        return old.id == source.id || old.path == source.path
            || (!source.explicitPath.isEmpty() && old.explicitPath == source.explicitPath);
    });
    return previous == m_sources.cend() ? nullptr : &*previous;
}

void CustomMetricsService::applyCommand(Source &source, const CommandParseResult &command, const Source *previous,
                                        QList<CommandDefinition> &definitions)
{
    source.type = SourceType::Command;
    if (command.definition && !command.definition->enabled) {
        // Disabled: no job (a running one is stopped by syncCommandJobs),
        // no values.
        if (source.configuredId.isEmpty())
            source.configuredId = command.definition->id;
        source.integration = command.definition->integration;
        disable(source, command.definition->title, command.definition->legacySymbol, command.definition->icon);
        if (source.parentId.isEmpty())
            source.configuredEnabled = false;
        else if (source.configuredEnabled)
            source.message = tr("Disabled by its group.");
    } else if (command.definition) {
        if (source.configuredId.isEmpty())
            source.configuredId = command.definition->id;
        source.integration = command.definition->integration;
        definitions.append(*command.definition);
        if (!previous || previous->type != SourceType::Command || !previous->enabled) {
            Snapshot skeleton;
            skeleton.title = command.definition->title;
            skeleton.symbol = command.definition->legacySymbol;
            skeleton.icon = command.definition->icon;
            skeleton.showBar = command.definition->showBar;
            source.snapshot = std::move(skeleton);
            source.state = State::Pending;
            source.message = tr("Waiting for the first command result.");
        } else {
            source.state = previous->state;
            source.message = previous->message;
        }
    } else {
        source.state = State::Error;
        source.message = command.error;
    }
}

void CustomMetricsService::appendGroup(Source group, const QJsonObject &object, QList<Source> &next,
                                       QList<CommandDefinition> &definitions)
{
    group.type = SourceType::Group;
    group.state = State::Available;
    group.message.clear();
    // Its title and icon for the frontends; never values.
    Snapshot presentation;
    QString error;
    const QString directory = QFileInfo(group.path).absolutePath();
    const QJsonValue id = object.value(QStringLiteral("id"));
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$"));
    bool ok = QFileInfo(group.path).fileName() == QLatin1String("metric.json");
    if (!ok)
        error = tr("A group must be a package's metric.json (its children are the package's subdirectories).");
    if (ok && !(id.isString() && validId.match(id.toString()).hasMatch())) {
        ok = false;
        error = QStringLiteral("field \"id\" must match [A-Za-z0-9][A-Za-z0-9._-]{0,99}");
    }
    if (ok && !(object.value(QStringLiteral("title")).isString() && !object.value(QStringLiteral("title")).toString().isEmpty())) {
        ok = false;
        error = QStringLiteral("missing required field \"title\"");
    }
    ok = ok && parseEnabled(object, group.configuredEnabled, error)
        && parseSymbol(object.value(QStringLiteral("symbol")), directory, presentation.symbol, presentation.icon, error);
    if (!ok) {
        group.state = State::Error;
        group.message = error;
        group.snapshot.reset();
        next.append(std::move(group));
        return; // an invalid group has no children
    }
    presentation.title = object.value(QStringLiteral("title")).toString();
    group.configuredId = id.toString();
    group.snapshot = presentation;
    group.enabled = group.configuredEnabled;
    if (!group.enabled)
        group.message = tr("Disabled.");
    const Inheritance inheritance = rootInheritance(object, directory, group.configuredId, group.enabled);
    QStringList visited{QFileInfo(directory).canonicalFilePath()};
    const QString groupId = group.id;
    next.append(std::move(group));
    appendChildren(groupId, directory, inheritance, 1, visited, next, definitions);
}

void CustomMetricsService::appendChildren(const QString &parentId, const QString &directory,
                                          const Inheritance &inheritance, int depth, QStringList &visited,
                                          QList<Source> &next, QList<CommandDefinition> &definitions)
{
    QStringList ids;
    QString unreadable;
    const QStringList children = childConfigurations(directory, visited, &unreadable);
    if (!unreadable.isEmpty() && m_reloadFailure.isEmpty())
        m_reloadFailure = unreadable;
    for (const QString &path : children) {
        Source child;
        child.id = identityFor(path);
        child.path = path;
        child.discovered = true;
        child.parentId = parentId;
        const Source *previous = previousFor(child);
        if (previous)
            child.snapshot = previous->snapshot;
        const QString childDirectory = QFileInfo(path).absolutePath();
        // Named after its directory until its own id is known (also when its
        // file cannot be read).
        child.configuredId = inheritance.idPrefix + u'/' + QFileInfo(childDirectory).fileName();

        const ConfigObject config = readConfigObject(path);
        QString id;
        QString error;
        bool own = true;
        if (!config.object || !childId(*config.object, childDirectory, id, error)
            || !parseEnabled(*config.object, own, error)) {
            child.type = SourceType::Command;
            child.state = State::Error;
            child.message = config.object ? error : config.error;
            child.configuration = child.message.toUtf8();
            child.snapshot.reset();
            next.append(std::move(child));
            continue;
        }
        child.configuredEnabled = own;
        child.configuredId = inheritance.idPrefix + u'/' + id;
        if (ids.contains(id)) {
            child.type = SourceType::Command;
            child.state = State::Error;
            child.message = tr("Duplicate id \"%1\" in this group.").arg(child.configuredId);
            child.configuration = child.message.toUtf8();
            child.snapshot.reset();
            next.append(std::move(child));
            continue;
        }
        ids.append(id);
        const bool enabled = inheritance.enabled && own;
        const QJsonObject effective = effectiveChild(inheritance, *config.object, childDirectory, id, enabled);
        // Inherited values are part of it: a group's change changes its children.
        child.configuration = compactJson(effective);

        if (isGroupObject(effective)) {
            // A nested group: a container again, its children one level down.
            child.type = SourceType::Group;
            child.state = depth < MaxGroupDepth ? State::Available : State::Error;
            child.message = depth < MaxGroupDepth ? QString() : tr("Groups are nested too deeply.");
            child.enabled = enabled;
            Snapshot presentation;
            presentation.title = effective.value(QStringLiteral("title")).toString(id);
            parseSymbol(effective.value(QStringLiteral("symbol")), childDirectory, presentation.symbol, presentation.icon, error);
            child.snapshot = presentation;
            const QString nestedId = child.id;
            next.append(std::move(child));
            if (depth < MaxGroupDepth)
                appendChildren(nestedId, childDirectory, childInheritance(inheritance, *config.object, childDirectory, id, enabled),
                               depth + 1, visited, next, definitions);
            continue;
        }
        const CommandParseResult command = parseCommandJson(QJsonDocument(effective).toJson(QJsonDocument::Compact), path);
        if (!command.recognized) {
            child.type = SourceType::Command;
            child.state = State::Error;
            child.message = tr("A group's child must be a command (\"source\": {\"type\": \"command\"}) or a group.");
            child.snapshot.reset();
        } else {
            applyCommand(child, command, previous, definitions);
            if (!command.definition)
                child.snapshot.reset();
        }
        next.append(std::move(child));
    }
}

void CustomMetricsService::updateWarnings(Source &source, const QStringList &before)
{
    source.warnings.clear();
    const QString icon = m_iconWarnings.value(source.id);
    if (!icon.isEmpty())
        source.warnings.append(icon);
    // Only for values on show: not the last ones of a failed or unavailable run.
    if (source.snapshot && source.state != State::Error && source.state != State::Unavailable) {
        const auto job = m_commandJobs.find(source.id);
        const QStringList values = valueWarnings(*source.snapshot,
                                                 job != m_commandJobs.end() ? &job->second->definition : nullptr);
        // Logged when values leave their range, not on each run outside it.
        if (!values.isEmpty() && before.size() <= (icon.isEmpty() ? 0 : 1))
            qCWarning(lcCustomMetrics).noquote()
                << QStringLiteral("custom metric \"%1\": %2").arg(reportName(source), values.join(QStringLiteral("; ")));
        source.warnings += values;
    }
}

Source *CustomMetricsService::sourceFor(const QString &id)
{
    const auto source = std::find_if(m_sources.begin(), m_sources.end(),
                                     [&](const Source &entry) { return entry.id == id; });
    return source == m_sources.end() ? nullptr : &*source;
}

void CustomMetricsService::publish()
{
    emit sourcesChanged(m_sources);
}

void CustomMetricsService::stopCommand(CommandJob &job)
{
    if (!job.process)
        return;
    job.process->disconnect(this);
    if (job.process->state() != QProcess::NotRunning) {
        signalProcessGroup(*job.process, SIGTERM);
        if (!job.process->waitForFinished(StopWaitMilliseconds)) {
            signalProcessGroup(*job.process, SIGKILL);
            job.process->waitForFinished(StopWaitMilliseconds);
        }
        // Group members that ignored SIGTERM outlive the leader.
        signalProcessGroup(*job.process, SIGKILL);
    }
    job.process.reset();
}

void CustomMetricsService::stopCommands()
{
    // Signal every group first so shutdown waits are bounded in total, not
    // per running command.
    std::vector<QProcess *> running;
    for (auto &[id, job] : m_commandJobs) {
        Q_UNUSED(id);
        if (!job->process)
            continue;
        job->process->disconnect(this);
        if (job->process->state() != QProcess::NotRunning) {
            signalProcessGroup(*job->process, SIGTERM);
            running.push_back(job->process.get());
        }
    }
    QElapsedTimer elapsed;
    elapsed.start();
    for (QProcess *process : running) {
        if (!process->waitForFinished(int(std::max<qint64>(0, StopWaitMilliseconds - elapsed.elapsed()))))
            signalProcessGroup(*process, SIGKILL);
    }
    for (QProcess *process : running) {
        if (process->state() != QProcess::NotRunning)
            process->waitForFinished(StopWaitMilliseconds);
        signalProcessGroup(*process, SIGKILL);
    }
    m_commandJobs.clear();
}

QString aqtruncat::custommetrics::reloadSummary(const ReloadReport &report)
{
    if (!report.ok)
        return QCoreApplication::translate("CustomMetricsService",
                                           "Custom Metrics not reloaded: %1 The active configuration is unchanged.")
            .arg(report.error);
    QString text = QCoreApplication::translate("CustomMetricsService",
                                               "Custom Metrics reloaded: %1 added, %2 changed, %3 removed, %4 unchanged.")
                       .arg(report.added.size()).arg(report.changed.size()).arg(report.removed.size())
                       .arg(report.unchanged);
    // A few named, enough to find them; Settings lists them all with their status.
    const auto named = [](const QList<ReloadEntry> &entries) {
        constexpr qsizetype Named = 3;
        QStringList names;
        for (const ReloadEntry &entry : entries.mid(0, Named))
            names.append(QStringLiteral("%1 (%2)").arg(entry.name, entry.message));
        if (entries.size() > Named)
            names.append(QCoreApplication::translate("CustomMetricsService", "%n more", nullptr,
                                                     int(entries.size() - Named)));
        return names.join(QStringLiteral("; "));
    };
    if (!report.errors.isEmpty())
        text += u' '
            + QCoreApplication::translate("CustomMetricsService", "%n with errors: ", nullptr, int(report.errors.size()))
            + named(report.errors) + u'.';
    if (!report.warnings.isEmpty())
        text += u' '
            + QCoreApplication::translate("CustomMetricsService", "%n with warnings: ", nullptr, int(report.warnings.size()))
            + named(report.warnings) + u'.';
    return text;
}
