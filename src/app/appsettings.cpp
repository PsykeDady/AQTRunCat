#include "appsettings.h"

#include "metricsbarids.h"

#include <QRegularExpression>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace aqtruncat {

namespace {

constexpr auto RunnerKey = "runner/id";
constexpr auto SpeedKey = "runner/speedMode";
constexpr auto NetworkModeKey = "network/mode";
constexpr auto NetworkInterfacesKey = "network/interfaces";
constexpr auto RefreshKey = "metrics/dashboardRefreshSeconds";
constexpr auto AutostartKey = "desktop/autostart";
constexpr auto CustomMetricFilesKey = "customMetrics/files";
constexpr auto MetricsBarEnabledKey = "metricsBar/enabled";
constexpr auto MetricsBarItemsKey = "metricsBar/items";
constexpr auto GpuDeviceKey = "gpu/device";
constexpr auto FpsSourceKey = "fps/source";
constexpr auto UpdatesIntervalKey = "updates/intervalMinutes";
constexpr auto UpdatesRemoteKey = "updates/remoteRefresh";
constexpr auto ShowCpuTemperatureKey = "display/cpuTemperature";
constexpr auto ShowGpuMemoryKey = "display/gpuMemory";
constexpr auto ShowGpuTemperatureKey = "display/gpuTemperature";
constexpr auto ShowFpsKey = "display/fps";
constexpr int DefaultUpdatesInterval = 60;
constexpr qsizetype MaxMetricsBarIdLength = 4096;

SpeedMode readSpeedMode(const QVariant &value)
{
    return value.toString() == QLatin1String("slow-down") ? SpeedMode::SlowDown : SpeedMode::Normal;
}

metrics::NetworkPolicyMode readNetworkMode(const QVariant &value)
{
    return value.toString() == QLatin1String("manual")
        ? metrics::NetworkPolicyMode::Manual
        : metrics::NetworkPolicyMode::Automatic;
}

bool validRefresh(int seconds)
{
    return seconds == 1 || seconds == 2 || seconds == 5;
}

} // namespace

AppSettings::AppSettings(QString defaultRunnerId, QObject *parent)
    : QObject(parent)
    , m_defaultRunnerId(std::move(defaultRunnerId))
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("AQTRunCat"), QStringLiteral("AQTRunCat"))
{
    load();
}

AppSettings::AppSettings(QString defaultRunnerId, const QString &iniFile, QObject *parent)
    : QObject(parent)
    , m_defaultRunnerId(std::move(defaultRunnerId))
    , m_settings(iniFile, QSettings::IniFormat)
{
    load();
}

bool AppSettings::isValidStoredRunnerId(const QString &id)
{
    if (id.isEmpty() || id.size() > 255 || id == QLatin1String(".") || id == QLatin1String(".."))
        return false;
    for (const QChar c : id)
        if (c == u'/' || c == u'\\' || c.category() == QChar::Other_Control)
            return false;
    return true;
}

QString AppSettings::speedModeName(SpeedMode mode)
{
    return mode == SpeedMode::SlowDown ? QStringLiteral("slow-down") : QStringLiteral("normal");
}

QString AppSettings::networkModeName(metrics::NetworkPolicyMode mode)
{
    return mode == metrics::NetworkPolicyMode::Manual ? QStringLiteral("manual") : QStringLiteral("automatic");
}

QStringList AppSettings::normalizedInterfaces(QStringList interfaces)
{
    interfaces.removeAll(QString());
    interfaces.removeDuplicates();
    std::sort(interfaces.begin(), interfaces.end(), [](const QString &a, const QString &b) {
        return a < b;
    });
    return interfaces;
}

QStringList AppSettings::normalizedCustomMetricFiles(QStringList files)
{
    QStringList result;
    QSet<QString> identities;
    for (const QString &file : std::as_const(files)) {
        if (file.trimmed().isEmpty())
            continue;
        const QFileInfo info(file);
        const QString absolute = QDir::cleanPath(info.absoluteFilePath());
        const QString canonical = info.canonicalFilePath();
        const QString identity = canonical.isEmpty() ? absolute : canonical;
        if (identities.contains(identity))
            continue;
        identities.insert(identity);
        result.append(absolute);
    }
    return result;
}

bool AppSettings::isValidGpuDevice(const QString &id)
{
    // "auto", a PCI slot ("0000:03:00.0") or another bus's device name.
    static const QRegularExpression device(QStringLiteral("^(auto|[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\\.[0-7]|platform:[A-Za-z0-9._:-]{1,100})$"));
    return device.match(id).hasMatch();
}

bool AppSettings::isValidFpsSource(const QString &source)
{
    return source == QLatin1String("mangohud") || source == QLatin1String("off");
}

QList<int> AppSettings::updatesIntervals()
{
    return {0, 15, 30, 60, 180, 360, 720, 1440};
}

bool AppSettings::isValidUpdatesInterval(int minutes)
{
    return updatesIntervals().contains(minutes);
}

QStringList AppSettings::defaultMetricsBarItems()
{
    // Pre-selected so that enabling the bar shows something useful; the bar
    // itself stays disabled until the user turns it on.
    return {QStringLiteral("cpu")};
}

QStringList AppSettings::normalizedMetricsBarItems(QStringList ids)
{
    // Deterministic for any stored value, including legacy or hand-edited
    // lists: drop empty, malformed, unknown and duplicate ids, then keep the
    // first metricsbar::MaxItems in the stored order.
    QStringList result;
    for (const QString &id : std::as_const(ids)) {
        if (result.size() == metricsbar::MaxItems)
            break;
        if (id.size() > MaxMetricsBarIdLength || !metricsbar::isKnownId(id) || result.contains(id))
            continue;
        result.append(id);
    }
    return result;
}

void AppSettings::load()
{
    const QString storedRunner = m_settings.value(QLatin1String(RunnerKey)).toString();
    m_runnerId = isValidStoredRunnerId(storedRunner) ? storedRunner : m_defaultRunnerId;
    m_speedMode = readSpeedMode(m_settings.value(QLatin1String(SpeedKey)));
    m_networkMode = readNetworkMode(m_settings.value(QLatin1String(NetworkModeKey)));
    m_networkInterfaces = normalizedInterfaces(m_settings.value(QLatin1String(NetworkInterfacesKey)).toStringList());
    const int refresh = m_settings.value(QLatin1String(RefreshKey), 1).toInt();
    m_dashboardRefreshSeconds = validRefresh(refresh) ? refresh : 1;
    m_autostartEnabled = m_settings.value(QLatin1String(AutostartKey), false).toBool();
    m_customMetricFiles = normalizedCustomMetricFiles(
        m_settings.value(QLatin1String(CustomMetricFilesKey)).toStringList());
    m_metricsBarEnabled = m_settings.value(QLatin1String(MetricsBarEnabledKey), false).toBool();
    m_metricsBarItems = m_settings.contains(QLatin1String(MetricsBarItemsKey))
        ? normalizedMetricsBarItems(m_settings.value(QLatin1String(MetricsBarItemsKey)).toStringList())
        : defaultMetricsBarItems();
    const QString gpu = m_settings.value(QLatin1String(GpuDeviceKey), QStringLiteral("auto")).toString();
    m_gpuDevice = isValidGpuDevice(gpu) ? gpu : QStringLiteral("auto");
    const QString fps = m_settings.value(QLatin1String(FpsSourceKey), QStringLiteral("mangohud")).toString();
    m_fpsSource = isValidFpsSource(fps) ? fps : QStringLiteral("mangohud");
    const int interval = m_settings.value(QLatin1String(UpdatesIntervalKey), DefaultUpdatesInterval).toInt();
    m_updatesIntervalMinutes = isValidUpdatesInterval(interval) ? interval : DefaultUpdatesInterval;
    m_updatesRemoteRefresh = m_settings.value(QLatin1String(UpdatesRemoteKey), false).toBool();
    m_showCpuTemperature = m_settings.value(QLatin1String(ShowCpuTemperatureKey), true).toBool();
    m_showGpuMemory = m_settings.value(QLatin1String(ShowGpuMemoryKey), true).toBool();
    m_showGpuTemperature = m_settings.value(QLatin1String(ShowGpuTemperatureKey), true).toBool();
    m_showFps = m_settings.value(QLatin1String(ShowFpsKey), true).toBool();
}

void AppSettings::write(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
    m_settings.sync();
}

metrics::NetworkSelection AppSettings::networkSelection() const
{
    metrics::NetworkSelection result;
    result.mode = m_networkMode;
    for (const QString &name : m_networkInterfaces)
        result.interfaces.insert(name.toStdString());
    return result;
}

void AppSettings::setRunnerId(const QString &id)
{
    if (!isValidStoredRunnerId(id) || id == m_runnerId)
        return;
    m_runnerId = id;
    write(QLatin1String(RunnerKey), id);
    emit runnerIdChanged(id);
}

void AppSettings::setSpeedMode(SpeedMode mode)
{
    if (mode != SpeedMode::Normal && mode != SpeedMode::SlowDown)
        mode = SpeedMode::Normal;
    if (mode == m_speedMode)
        return;
    m_speedMode = mode;
    write(QLatin1String(SpeedKey), speedModeName(mode));
    emit speedModeChanged(mode);
}

void AppSettings::setNetworkMode(metrics::NetworkPolicyMode mode)
{
    if (mode != metrics::NetworkPolicyMode::Automatic && mode != metrics::NetworkPolicyMode::Manual)
        mode = metrics::NetworkPolicyMode::Automatic;
    if (mode == m_networkMode)
        return;
    m_networkMode = mode;
    write(QLatin1String(NetworkModeKey), networkModeName(mode));
    emit networkSettingsChanged();
}

void AppSettings::setNetworkInterfaces(QStringList interfaces)
{
    interfaces = normalizedInterfaces(std::move(interfaces));
    if (interfaces == m_networkInterfaces)
        return;
    m_networkInterfaces = std::move(interfaces);
    write(QLatin1String(NetworkInterfacesKey), m_networkInterfaces);
    emit networkSettingsChanged();
}

void AppSettings::setDashboardRefreshSeconds(int seconds)
{
    if (!validRefresh(seconds))
        seconds = 1;
    if (seconds == m_dashboardRefreshSeconds)
        return;
    m_dashboardRefreshSeconds = seconds;
    write(QLatin1String(RefreshKey), seconds);
    emit dashboardRefreshSecondsChanged(seconds);
}

void AppSettings::setAutostartEnabled(bool enabled)
{
    if (enabled == m_autostartEnabled)
        return;
    m_autostartEnabled = enabled;
    write(QLatin1String(AutostartKey), enabled);
    emit autostartEnabledChanged(enabled);
}

void AppSettings::setCustomMetricFiles(QStringList files)
{
    files = normalizedCustomMetricFiles(std::move(files));
    if (files == m_customMetricFiles)
        return;
    m_customMetricFiles = std::move(files);
    write(QLatin1String(CustomMetricFilesKey), m_customMetricFiles);
    emit customMetricFilesChanged(m_customMetricFiles);
}

void AppSettings::setMetricsBarEnabled(bool enabled)
{
    if (enabled == m_metricsBarEnabled)
        return;
    m_metricsBarEnabled = enabled;
    write(QLatin1String(MetricsBarEnabledKey), enabled);
    emit metricsBarChanged();
}

void AppSettings::setMetricsBarItems(QStringList ids)
{
    ids = normalizedMetricsBarItems(std::move(ids));
    if (ids == m_metricsBarItems)
        return;
    m_metricsBarItems = std::move(ids);
    // Written even when empty: an explicit empty selection must survive a
    // restart instead of falling back to the default.
    write(QLatin1String(MetricsBarItemsKey), m_metricsBarItems);
    emit metricsBarChanged();
}

void AppSettings::setGpuDevice(const QString &id)
{
    if (!isValidGpuDevice(id) || id == m_gpuDevice)
        return;
    m_gpuDevice = id;
    write(QLatin1String(GpuDeviceKey), id);
    emit systemMetricsSettingsChanged();
}

void AppSettings::setFpsSource(const QString &source)
{
    if (!isValidFpsSource(source) || source == m_fpsSource)
        return;
    m_fpsSource = source;
    write(QLatin1String(FpsSourceKey), source);
    emit systemMetricsSettingsChanged();
}

void AppSettings::setUpdatesIntervalMinutes(int minutes)
{
    if (!isValidUpdatesInterval(minutes) || minutes == m_updatesIntervalMinutes)
        return;
    m_updatesIntervalMinutes = minutes;
    write(QLatin1String(UpdatesIntervalKey), minutes);
    emit systemMetricsSettingsChanged();
}

void AppSettings::setUpdatesRemoteRefresh(bool remote)
{
    if (remote == m_updatesRemoteRefresh)
        return;
    m_updatesRemoteRefresh = remote;
    write(QLatin1String(UpdatesRemoteKey), remote);
    emit systemMetricsSettingsChanged();
}

void AppSettings::setShowCpuTemperature(bool show)
{
    if (show == m_showCpuTemperature) return;
    m_showCpuTemperature = show;
    write(QLatin1String(ShowCpuTemperatureKey), show);
    emit displaySettingsChanged();
}

void AppSettings::setShowGpuMemory(bool show)
{
    if (show == m_showGpuMemory) return;
    m_showGpuMemory = show;
    write(QLatin1String(ShowGpuMemoryKey), show);
    emit displaySettingsChanged();
}

void AppSettings::setShowGpuTemperature(bool show)
{
    if (show == m_showGpuTemperature) return;
    m_showGpuTemperature = show;
    write(QLatin1String(ShowGpuTemperatureKey), show);
    emit displaySettingsChanged();
}

void AppSettings::setShowFps(bool show)
{
    if (show == m_showFps) return;
    m_showFps = show;
    write(QLatin1String(ShowFpsKey), show);
    emit displaySettingsChanged();
}

bool AppSettings::reload()
{
    const QString runner = m_runnerId;
    const SpeedMode speed = m_speedMode;
    const metrics::NetworkPolicyMode networkMode = m_networkMode;
    const QStringList interfaces = m_networkInterfaces;
    const int refresh = m_dashboardRefreshSeconds;
    const bool autostart = m_autostartEnabled;
    const QStringList customFiles = m_customMetricFiles;
    const bool barEnabled = m_metricsBarEnabled;
    const QStringList barItems = m_metricsBarItems;
    const QString gpu = m_gpuDevice;
    const QString fps = m_fpsSource;
    const int interval = m_updatesIntervalMinutes;
    const bool remote = m_updatesRemoteRefresh;
    const bool showCpuTemperature = m_showCpuTemperature;
    const bool showGpuMemory = m_showGpuMemory;
    const bool showGpuTemperature = m_showGpuTemperature;
    const bool showFps = m_showFps;
    m_settings.sync(); // no pending local writes: this picks up the file on disk
    load();
    bool changed = false;
    if (runner != m_runnerId) {
        changed = true;
        emit runnerIdChanged(m_runnerId);
    }
    if (speed != m_speedMode) {
        changed = true;
        emit speedModeChanged(m_speedMode);
    }
    if (networkMode != m_networkMode || interfaces != m_networkInterfaces) {
        changed = true;
        emit networkSettingsChanged();
    }
    if (refresh != m_dashboardRefreshSeconds) {
        changed = true;
        emit dashboardRefreshSecondsChanged(m_dashboardRefreshSeconds);
    }
    if (autostart != m_autostartEnabled) {
        changed = true;
        emit autostartEnabledChanged(m_autostartEnabled);
    }
    if (customFiles != m_customMetricFiles) {
        changed = true;
        emit customMetricFilesChanged(m_customMetricFiles);
    }
    if (barEnabled != m_metricsBarEnabled || barItems != m_metricsBarItems) {
        changed = true;
        emit metricsBarChanged();
    }
    if (gpu != m_gpuDevice || fps != m_fpsSource || interval != m_updatesIntervalMinutes
        || remote != m_updatesRemoteRefresh) {
        changed = true;
        emit systemMetricsSettingsChanged();
    }
    if (showCpuTemperature != m_showCpuTemperature || showGpuMemory != m_showGpuMemory
        || showGpuTemperature != m_showGpuTemperature || showFps != m_showFps) {
        changed = true;
        emit displaySettingsChanged();
    }
    return changed;
}

void AppSettings::restoreDefaults()
{
    // Restore application preferences, but keep explicit Custom Metrics
    // registrations: removing a producer is a separate, deliberate action.
    for (const char *key : {RunnerKey, SpeedKey, NetworkModeKey, NetworkInterfacesKey,
                            RefreshKey, AutostartKey, MetricsBarEnabledKey, MetricsBarItemsKey,
                            GpuDeviceKey, FpsSourceKey, UpdatesIntervalKey, UpdatesRemoteKey,
                            ShowCpuTemperatureKey, ShowGpuMemoryKey, ShowGpuTemperatureKey, ShowFpsKey})
        m_settings.remove(QLatin1String(key));
    m_settings.sync();
    m_runnerId = m_defaultRunnerId;
    m_speedMode = SpeedMode::Normal;
    m_networkMode = metrics::NetworkPolicyMode::Automatic;
    m_networkInterfaces.clear();
    m_dashboardRefreshSeconds = 1;
    m_autostartEnabled = false;
    m_metricsBarEnabled = false;
    m_metricsBarItems = defaultMetricsBarItems();
    m_gpuDevice = QStringLiteral("auto");
    m_fpsSource = QStringLiteral("mangohud");
    m_updatesIntervalMinutes = DefaultUpdatesInterval;
    m_updatesRemoteRefresh = false;
    m_showCpuTemperature = true;
    m_showGpuMemory = true;
    m_showGpuTemperature = true;
    m_showFps = true;
    emit runnerIdChanged(m_runnerId);
    emit speedModeChanged(m_speedMode);
    emit networkSettingsChanged();
    emit dashboardRefreshSecondsChanged(m_dashboardRefreshSeconds);
    emit autostartEnabledChanged(m_autostartEnabled);
    emit metricsBarChanged();
    emit systemMetricsSettingsChanged();
    emit displaySettingsChanged();
}

} // namespace aqtruncat
