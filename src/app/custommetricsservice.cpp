#include "custommetricsservice.h"

#include "appsettings.h"
#include "custommetricpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

using namespace aqtruncat::custommetrics;

namespace {
constexpr int DebounceMilliseconds = 100;
constexpr int RetryMilliseconds = 200;
constexpr int RetryCount = 3;
constexpr int RecoveryMilliseconds = 5000;
}

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

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReload(); });
    connect(&m_debounce, &QTimer::timeout, this, [this] {
        m_retryAttempt = 0;
        reload(false);
    });
    connect(&m_retry, &QTimer::timeout, this, [this] { reload(++m_retryAttempt >= RetryCount); });
    connect(&m_recovery, &QTimer::timeout, this, [this] { reload(true); });
    connect(m_settings, &aqtruncat::AppSettings::customMetricFilesChanged,
            this, [this] { scheduleReload(); });
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
        const ParseResult parsed = parseFile(absolute);
        if (!parsed.snapshot) {
            if (error)
                *error = parsed.error;
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
    QDir().mkpath(m_discoveryDirectory);
    reload(true);
}

void CustomMetricsService::stop()
{
    m_active = false;
    m_debounce.stop();
    m_retry.stop();
    m_recovery.stop();
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

    QList<Source> next;
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
        next.append(std::move(source));
    }
    m_sources = std::move(next);
    rebuildWatches();
    emit sourcesChanged(m_sources);

    if (retryNeeded && !finalAttempt)
        m_retry.start();
    else
        m_retry.stop();
    if (hasDegradedExplicitSource() || std::any_of(m_sources.cbegin(), m_sources.cend(), [](const Source &s) {
            return s.state == State::Error;
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
        return source.explicitlyRegistered && source.state != State::Available;
    });
}
