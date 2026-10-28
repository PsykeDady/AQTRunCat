#include "updatesservice.h"

#include "metrics/systeminfo.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <unistd.h>

using namespace aqtruncat::metrics;

namespace {

// Even when the user picks a shorter interval, metadata is downloaded at
// most hourly.
constexpr int MinimumRemoteMinutes = 60;

std::string text(const QByteArray &bytes)
{
    return bytes.toStdString();
}

} // namespace

UpdatesService::UpdatesService(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    if (m_options.home.isEmpty())
        m_options.home = QDir::homePath();
    for (Job *job : {&m_packages, &m_flatpak}) {
        job->due.setSingleShot(true);
        job->debounce.setSingleShot(true);
        job->debounce.setInterval(m_options.changeDebounceMs);
        job->metrics.status = MetricStatus::pending("waiting for the first update check");
    }
    m_flatpak.metrics.manager = "flatpak";
    connect(&m_packages.due, &QTimer::timeout, this, &UpdatesService::checkPackages);
    connect(&m_flatpak.due, &QTimer::timeout, this, &UpdatesService::checkFlatpak);
    connect(&m_packages.debounce, &QTimer::timeout, this, &UpdatesService::checkPackages);
    connect(&m_flatpak.debounce, &QTimer::timeout, this, &UpdatesService::checkFlatpak);
    connect(&m_packages.process, &BoundedProcess::finished, this, &UpdatesService::finishPackages);
    connect(&m_flatpak.process, &BoundedProcess::finished, this, &UpdatesService::finishFlatpak);
    auto changedPath = [this](const QString &changed) {
        for (Job *job : {&m_packages, &m_flatpak}) {
            if (job->watched.contains(changed) && m_active && m_intervalMinutes > 0)
                job->debounce.start(); // restarted by every event: one check after the burst
        }
        // A replaced file drops out of the watch: add it again.
        if (QFileInfo::exists(changed) && !m_watcher.files().contains(changed) && !m_watcher.directories().contains(changed))
            m_watcher.addPath(changed);
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, changedPath);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, changedPath);
}

UpdatesService::~UpdatesService()
{
    stop();
}

qint64 UpdatesService::now() const
{
    return m_options.now ? m_options.now() : QDateTime::currentSecsSinceEpoch();
}

void UpdatesService::setPolicy(int intervalMinutes, bool remote)
{
    intervalMinutes = std::max(0, intervalMinutes);
    if (intervalMinutes == m_intervalMinutes && remote == m_remote)
        return;
    m_intervalMinutes = intervalMinutes;
    m_remote = remote;
    if (!m_active)
        return;
    if (m_intervalMinutes == 0) {
        stop();
        m_active = true;
        for (Job *job : {&m_packages, &m_flatpak}) {
            job->metrics.checking = false;
            job->metrics.count.reset();
            job->metrics.status = MetricStatus::unavailable("update checks are turned off");
        }
        emit changed();
        return;
    }
    schedule(m_packages, m_options.policyChangeDelayMs);
    schedule(m_flatpak, m_options.policyChangeDelayMs);
}

void UpdatesService::start()
{
    if (m_active)
        return;
    m_active = true;
    if (m_intervalMinutes == 0) {
        for (Job *job : {&m_packages, &m_flatpak})
            job->metrics.status = MetricStatus::unavailable("update checks are turned off");
        emit changed();
        return;
    }
    schedule(m_packages, m_options.firstCheckDelayMs);
    schedule(m_flatpak, m_options.firstCheckDelayMs);
}

void UpdatesService::stop()
{
    m_active = false;
    for (Job *job : {&m_packages, &m_flatpak}) {
        job->due.stop();
        job->debounce.stop();
        job->process.stop();
        job->queue.clear();
        job->rerun = false;
        job->watched.clear();
    }
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
}

void UpdatesService::schedule(Job &job, int delayMs)
{
    if (!m_active || m_intervalMinutes == 0)
        return;
    if (job.process.isRunning()) {
        job.rerun = true;
        return;
    }
    job.due.start(std::max(0, delayMs));
}

QString UpdatesService::findTool(const QString &name) const
{
    return m_options.searchPath.isEmpty() ? QStandardPaths::findExecutable(name)
                                          : QStandardPaths::findExecutable(name, m_options.searchPath);
}

QString UpdatesService::checkupdatesDatabase() const
{
    if (!m_options.checkupdatesDatabase.isEmpty())
        return m_options.checkupdatesDatabase;
    const QString configured = qEnvironmentVariable("CHECKUPDATES_DB");
    if (!configured.isEmpty())
        return configured;
    return QDir::tempPath() + QStringLiteral("/checkup-db-%1").arg(::getuid());
}

std::optional<qint64> UpdatesService::newestTime(const QStringList &paths, bool filesInside) const
{
    std::optional<qint64> newest;
    auto consider = [&newest](const QFileInfo &info) {
        if (!info.exists())
            return;
        const qint64 time = info.lastModified().toSecsSinceEpoch();
        newest = std::max(newest.value_or(time), time);
    };
    for (const QString &entry : paths) {
        if (!filesInside) {
            consider(QFileInfo(entry));
            continue;
        }
        const QDir dir(entry);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
            consider(info);
    }
    return newest;
}

void UpdatesService::setMetadata(UpdatesMetrics &metrics, std::optional<qint64> time) const
{
    metrics.metadataTime = time;
    metrics.metadataStale = time && now() - *time > StaleMetadataSeconds;
}

void UpdatesService::watch(Job &job, const QStringList &paths)
{
    QStringList existing;
    for (const QString &entry : paths) {
        if (QFileInfo::exists(entry))
            existing << entry;
    }
    if (existing == job.watched)
        return;
    QStringList unused;
    for (const QString &entry : std::as_const(job.watched)) {
        if (!existing.contains(entry))
            unused << entry;
    }
    if (!unused.isEmpty())
        m_watcher.removePaths(unused);
    job.watched = existing;
    if (!existing.isEmpty())
        m_watcher.addPaths(existing);
}

void UpdatesService::unavailable(Job &job, const QString &manager, const QString &why)
{
    job.metrics.checking = false;
    job.metrics.count.reset();
    job.metrics.manager = manager.toStdString();
    job.metrics.status = MetricStatus::unavailable(why.toStdString());
    watch(job, {});
    emit changed();
    // Look again later: the tool may be installed meanwhile.
    job.due.start(m_intervalMinutes * 60 * 1000);
}

void UpdatesService::checkPackages()
{
    Job &job = m_packages;
    if (!m_active || m_intervalMinutes == 0)
        return;
    if (job.process.isRunning()) {
        job.rerun = true;
        return;
    }
    job.due.stop();
    job.debounce.stop();

    PackageManagerAvailability available;
    available.pacman = !findTool(QStringLiteral("pacman")).isEmpty() && QFileInfo::exists(path(QStringLiteral("/var/lib/pacman/local")));
    available.apt = !findTool(QStringLiteral("apt-get")).isEmpty() && QFileInfo::exists(path(QStringLiteral("/var/lib/dpkg/status")));
    available.dnf = !findTool(QStringLiteral("dnf")).isEmpty()
        && (QFileInfo::exists(path(QStringLiteral("/var/lib/rpm"))) || QFileInfo::exists(path(QStringLiteral("/usr/lib/sysimage/rpm"))));
    const DistributionMetrics distribution = readDistribution(m_options.root.toStdString());
    const PackageManager manager = distribution.info
        ? choosePackageManager(available, distribution.info->id, distribution.info->idLike)
        : choosePackageManager(available, {}, {});
    std::string reason;
    const bool checkupdates = !findTool(QStringLiteral("checkupdates")).isEmpty();
    const auto spec = packageUpdateCommand(manager, m_remote, checkupdates, &reason);
    if (!spec) {
        unavailable(job, QString::fromLatin1(packageManagerName(manager)), QString::fromStdString(reason));
        return;
    }
    const QString program = findTool(QString::fromStdString(spec->program));
    if (program.isEmpty()) {
        unavailable(job, QString::fromStdString(packageManagerName(manager)),
                    tr("%1 is not installed").arg(QString::fromStdString(spec->program)));
        return;
    }
    job.manager = manager;
    job.remote = (manager == PackageManager::Pacman && m_remote)
        || (manager == PackageManager::Dnf && m_remote);
    job.metrics.manager = packageManagerName(manager);
    job.metrics.note = reason;
    switch (manager) {
    case PackageManager::Pacman:
        watch(job, {path(QStringLiteral("/var/lib/pacman/local")), path(QStringLiteral("/var/lib/pacman/sync"))});
        break;
    case PackageManager::Apt:
        watch(job, {path(QStringLiteral("/var/lib/dpkg")), path(QStringLiteral("/var/lib/apt/lists"))});
        break;
    case PackageManager::Dnf:
        watch(job, {path(QStringLiteral("/var/lib/rpm")), path(QStringLiteral("/usr/lib/sysimage/rpm")),
                    path(QStringLiteral("/var/cache/libdnf5")), path(QStringLiteral("/var/cache/dnf"))});
        break;
    case PackageManager::None:
        break;
    }
    QStringList arguments;
    for (const std::string &argument : spec->arguments)
        arguments << QString::fromStdString(argument);
    job.queue = {Command{program, arguments, {}, false}};
    job.metrics.checking = true;
    if (!job.metrics.lastSuccess)
        job.metrics.status = MetricStatus::pending("checking for updates");
    emit changed();
    job.process.start(program, arguments, m_options.timeoutMs, m_options.maxOutputBytes);
}

void UpdatesService::finishPackages(const BoundedProcess::Result &result)
{
    Job &job = m_packages;
    job.queue.clear();
    job.metrics.checking = false;
    UpdateCount parsed;
    if (!result.ok()) {
        parsed.error = result.error.toStdString();
    } else if (job.manager == PackageManager::Pacman) {
        parsed = parseCheckupdates(text(result.standardOutput), result.exitCode, text(result.standardError));
    } else if (job.manager == PackageManager::Apt) {
        parsed = parseAptSimulation(text(result.standardOutput), result.exitCode, text(result.standardError));
    } else if (job.manager == PackageManager::Dnf) {
        parsed = parseDnfCheckUpdate(text(result.standardOutput), result.exitCode, text(result.standardError));
    }
    if (parsed.count) {
        job.metrics.count = parsed.count;
        job.metrics.ignored = parsed.ignored;
        job.metrics.lastSuccess = now();
        job.metrics.remote = job.remote;
        job.metrics.status = MetricStatus::okStatus();
        std::optional<qint64> metadata;
        if (job.remote)
            metadata = now();
        else if (job.manager == PackageManager::Pacman)
            metadata = newestTime({checkupdatesDatabase() + QStringLiteral("/sync")}, true);
        else if (job.manager == PackageManager::Apt)
            metadata = newestTime({path(QStringLiteral("/var/lib/apt/periodic/update-success-stamp")),
                                   path(QStringLiteral("/var/lib/apt/lists"))}, false);
        else
            metadata = newestTime({path(QStringLiteral("/var/cache/libdnf5")), path(QStringLiteral("/var/cache/dnf"))}, true);
        setMetadata(job.metrics, metadata);
    } else {
        job.metrics.count.reset();
        job.metrics.status = MetricStatus::error(parsed.error.empty() ? "the update check failed" : parsed.error);
    }
    completed(job);
}

QStringList UpdatesService::flatpakInstallations() const
{
    QStringList names{QStringLiteral("user"), QStringLiteral("system")};
    const QDir dir(path(QStringLiteral("/etc/flatpak/installations.d")));
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        for (const std::string &name : parseFlatpakInstallations(file.read(64 * 1024).toStdString())) {
            const QString installation = QString::fromStdString(name);
            if (!names.contains(installation))
                names << installation;
        }
    }
    return names;
}

void UpdatesService::checkFlatpak()
{
    Job &job = m_flatpak;
    if (!m_active || m_intervalMinutes == 0)
        return;
    if (job.process.isRunning()) {
        job.rerun = true;
        return;
    }
    job.due.stop();
    job.debounce.stop();
    const QString flatpak = findTool(QStringLiteral("flatpak"));
    if (flatpak.isEmpty()) {
        unavailable(job, QStringLiteral("flatpak"), tr("Flatpak is not installed"));
        return;
    }
    watch(job, {m_options.home + QStringLiteral("/.local/share/flatpak/.changed"),
                path(QStringLiteral("/var/lib/flatpak/.changed")),
                m_options.home + QStringLiteral("/.local/share/flatpak"), path(QStringLiteral("/var/lib/flatpak"))});
    job.remote = m_remote;
    job.queue.clear();
    job.partial.clear();
    for (const QString &installation : flatpakInstallations()) {
        const QString scope = installation == QLatin1String("user")     ? QStringLiteral("--user")
            : installation == QLatin1String("system")                  ? QStringLiteral("--system")
                                                                       : QStringLiteral("--installation=") + installation;
        for (const bool runtimes : {false, true}) {
            QStringList arguments{QStringLiteral("remote-ls"), scope, QStringLiteral("--updates")};
            // --cached: the summaries Flatpak already downloaded, no network.
            if (!m_remote)
                arguments << QStringLiteral("--cached");
            arguments << (runtimes ? QStringLiteral("--runtime") : QStringLiteral("--app")) << QStringLiteral("--columns=ref");
            job.queue << Command{flatpak, arguments, installation, runtimes};
        }
        FlatpakInstallationCount count;
        count.name = installation.toStdString();
        count.status = MetricStatus::okStatus();
        job.partial << count;
    }
    job.metrics.checking = true;
    job.metrics.note.clear();
    if (!job.metrics.lastSuccess)
        job.metrics.status = MetricStatus::pending("checking for updates");
    emit changed();
    runNext(job);
}

void UpdatesService::runNext(Job &job)
{
    if (job.queue.isEmpty())
        return;
    const Command &command = job.queue.first();
    job.process.start(command.program, command.arguments, m_options.timeoutMs, m_options.maxOutputBytes);
}

void UpdatesService::finishFlatpak(const BoundedProcess::Result &result)
{
    Job &job = m_flatpak;
    if (job.queue.isEmpty())
        return;
    const Command command = job.queue.takeFirst();
    const UpdateCount parsed = result.ok()
        ? parseFlatpakRefs(text(result.standardOutput), result.exitCode, text(result.standardError))
        : UpdateCount{std::nullopt, 0, 0, 0, result.error.toStdString()};
    for (FlatpakInstallationCount &entry : job.partial) {
        if (entry.name != command.installation.toStdString())
            continue;
        if (!parsed.count) {
            if (entry.status.ok())
                entry.status = MetricStatus::error(parsed.error);
        } else {
            (command.runtimes ? entry.runtimes : entry.apps) = *parsed.count;
        }
    }
    if (!job.queue.isEmpty()) {
        runNext(job);
        return;
    }

    job.metrics.checking = false;
    job.metrics.installations.assign(job.partial.cbegin(), job.partial.cend());
    QStringList failures;
    int apps = 0;
    int runtimes = 0;
    for (const FlatpakInstallationCount &entry : std::as_const(job.partial)) {
        if (!entry.status.ok())
            failures << QStringLiteral("%1 installation: %2").arg(QString::fromStdString(entry.name),
                                                                   QString::fromStdString(entry.status.message));
        apps += entry.apps;
        runtimes += entry.runtimes;
    }
    if (failures.isEmpty()) {
        job.metrics.apps = apps;
        job.metrics.runtimes = runtimes;
        job.metrics.count = apps + runtimes;
        job.metrics.lastSuccess = now();
        job.metrics.remote = job.remote;
        job.metrics.status = MetricStatus::okStatus();
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        setMetadata(job.metrics, job.remote ? std::optional<qint64>(now())
                                            : newestTime({cache + QStringLiteral("/flatpak/system-cache/summaries"),
                                                          data + QStringLiteral("/flatpak/repo/tmp/cache/summaries"),
                                                          path(QStringLiteral("/var/lib/flatpak/repo/tmp/cache/summaries"))},
                                                         true));
    } else {
        // A partial sum would understate the updates: no count at all.
        job.metrics.count.reset();
        job.metrics.status = MetricStatus::error(failures.join(QStringLiteral("; ")).toStdString());
    }
    completed(job);
}

void UpdatesService::completed(Job &job)
{
    emit changed();
    if (!m_active || m_intervalMinutes == 0)
        return;
    if (job.rerun) {
        job.rerun = false;
        job.debounce.start();
        return;
    }
    const int minutes = job.remote ? std::max(m_intervalMinutes, MinimumRemoteMinutes) : m_intervalMinutes;
    job.due.start(minutes * 60 * 1000);
}
