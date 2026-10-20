#include "custommetricsservice.h"

#include "appsettings.h"
#include "custommetricpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

#include <algorithm>
#include <limits>
#include <set>
#include <vector>

#include <csignal>
#include <unistd.h>

using namespace aqtruncat::custommetrics;

namespace {
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
    const qint64 pid = process.processId();
    if (pid > 0)
        ::kill(-pid_t(pid), signal);
}
}

struct CustomMetricsService::CommandJob {
    CommandDefinition definition;
    std::unique_ptr<QProcess> process;
    QByteArray standardOutput;
    QByteArray standardError;
    qint64 nextDueMilliseconds = 0;
    qint64 deadlineMilliseconds = 0;
    QString forcedError;
};

CustomMetricsService::CustomMetricsService(aqtruncat::AppSettings *settings,
                                           QString discoveryDirectory, QObject *parent)
    : QObject(parent)
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
        const CommandParseResult command = parseCommandFile(absolute);
        const ParseResult parsed = command.recognized ? ParseResult{} : parseFile(absolute);
        if ((command.recognized && !command.definition)
            || (!command.recognized && !parsed.snapshot)) {
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

void CustomMetricsService::scheduleReload()
{
    if (m_active)
        m_debounce.start();
}

void CustomMetricsService::reload(bool finalAttempt)
{
    if (!m_active)
        return;

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

        const auto previous = std::find_if(m_sources.cbegin(), m_sources.cend(), [&](const Source &old) {
            return old.id == source.id || old.path == source.path
                || (!source.explicitPath.isEmpty() && old.explicitPath == source.explicitPath);
        });
        if (previous != m_sources.cend())
            source.snapshot = previous->snapshot;

        const QFileInfo info(candidate.path);
        if (!info.exists() || !info.isFile()) {
            source.message = tr("File is not available.");
            if (!finalAttempt && source.explicitlyRegistered && source.snapshot) {
                source.state = State::Pending;
                retryNeeded = true;
            } else {
                source.state = State::Unavailable;
            }
        } else {
            const CommandParseResult command = parseCommandFile(candidate.path);
            if (command.recognized) {
                source.type = SourceType::Command;
                if (command.definition) {
                    source.configuredId = command.definition->id;
                    commandDefinitions.append(*command.definition);
                    if (previous == m_sources.cend() || previous->type != SourceType::Command) {
                        Snapshot skeleton;
                        skeleton.title = command.definition->title;
                        skeleton.symbol = command.definition->legacySymbol;
                        skeleton.icon = command.definition->icon;
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
            } else {
                const ParseResult parsed = parseFile(candidate.path);
                if (parsed.snapshot) {
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
        }
        next.append(std::move(source));
    }
    m_sources = std::move(next);
    syncCommandJobs(commandDefinitions);
    rebuildWatches();
    publish();

    if (retryNeeded && !finalAttempt)
        m_retry.start();
    else
        m_retry.stop();
    if (hasDegradedExplicitSource() || std::any_of(m_sources.cbegin(), m_sources.cend(), [](const Source &s) {
            return s.type == SourceType::Snapshot && s.state == State::Error;
        }))
        m_recovery.start();
    else
        m_recovery.stop();
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
        return source.type == SourceType::Snapshot && source.explicitlyRegistered
            && source.state != State::Available;
    });
}

void CustomMetricsService::syncCommandJobs(const QList<CommandDefinition> &definitions)
{
    std::set<QString> wanted;
    for (const CommandDefinition &definition : definitions) {
        const QString id = identityFor(definition.configurationPath);
        wanted.insert(id);
        const auto existing = m_commandJobs.find(id);
        if (existing != m_commandJobs.end() && existing->second->definition == definition)
            continue;
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
    const QFileInfo executable(job.definition.executable);
    if (!executable.exists() || !executable.isFile()) {
        failCommand(id, State::Unavailable,
                    tr("Executable is not available: %1").arg(job.definition.executable));
        return;
    }
    if (!executable.isExecutable()) {
        failCommand(id, State::Unavailable,
                    tr("File is not executable: %1").arg(job.definition.executable));
        return;
    }

    job.standardOutput.clear();
    job.standardError.clear();
    job.forcedError.clear();
    job.process = std::make_unique<QProcess>();
    QProcess *process = job.process.get();
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setWorkingDirectory(job.definition.workingDirectory);
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id] { readCommandOutput(id); });
    connect(process, &QProcess::readyReadStandardError, this, [this, id] { readCommandOutput(id); });
    connect(process, &QProcess::finished, this,
            [this, id](int exitCode, QProcess::ExitStatus status) { finishCommand(id, exitCode, status); });
    connect(process, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const auto it = m_commandJobs.find(id);
        if (it == m_commandJobs.end() || !it->second->process)
            return;
        it->second->forcedError = tr("Could not start executable: %1")
                                      .arg(it->second->process->errorString());
        // FailedToStart may be emitted synchronously from QProcess::start().
        // Defer teardown so the emitting QProcess is never destroyed inside
        // its own signal delivery.
        QTimer::singleShot(0, this, [this, id] { finishCommand(id, -1, QProcess::CrashExit); });
    });

    if (Source *source = sourceFor(id)) {
        source->state = State::Pending;
        source->message = tr("Command is running.");
    }
    publish();
    process->start(job.definition.executable, job.definition.arguments, QIODevice::ReadOnly);
    job.deadlineMilliseconds = m_clock.elapsed() + qint64(job.definition.timeoutSeconds) * 1000;
}

void CustomMetricsService::readCommandOutput(const QString &id)
{
    const auto found = m_commandJobs.find(id);
    if (found == m_commandJobs.end() || !found->second->process)
        return;
    CommandJob &job = *found->second;
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

void CustomMetricsService::finishCommand(const QString &id, int exitCode, QProcess::ExitStatus exitStatus)
{
    const auto found = m_commandJobs.find(id);
    if (found == m_commandJobs.end() || !found->second->process)
        return;
    CommandJob &job = *found->second;
    readCommandOutput(id);

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
        const QString diagnostic = QString::fromUtf8(job.standardError).trimmed();
        if (!diagnostic.isEmpty())
            error += QStringLiteral(" ") + diagnostic.left(512);
    }

    std::optional<Snapshot> snapshot;
    if (error.isEmpty()) {
        const ScalarResult parsed = snapshotFromCommandOutput(job.definition, job.standardOutput);
        snapshot = parsed.snapshot;
        error = parsed.error;
    }
    job.process->disconnect(this);
    job.process.reset();
    job.nextDueMilliseconds = m_clock.elapsed() + qint64(job.definition.intervalSeconds) * 1000;

    if (Source *source = sourceFor(id)) {
        if (snapshot) {
            source->snapshot = std::move(snapshot);
            source->state = State::Available;
            source->message.clear();
        } else {
            source->state = failure;
            source->message = error;
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
    found->second->nextDueMilliseconds = m_clock.elapsed()
        + qint64(found->second->definition.intervalSeconds) * 1000;
    if (Source *source = sourceFor(id)) {
        source->state = state;
        source->message = message;
    }
    publish();
    scheduleCommandWake();
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
