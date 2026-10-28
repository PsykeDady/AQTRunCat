#pragma once

#include "core/speedcurve.h"
#include "metrics/network.h"

#include <QObject>
#include <QSettings>
#include <QStringList>

namespace aqtruncat {

// Typed, UI-independent application configuration. Widgets only call these
// setters; every consumer reads the same state and reacts to its signals.
class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QString defaultRunnerId, QObject *parent = nullptr);
    AppSettings(QString defaultRunnerId, const QString &iniFile, QObject *parent = nullptr);

    QString runnerId() const { return m_runnerId; }
    SpeedMode speedMode() const { return m_speedMode; }
    metrics::NetworkPolicyMode networkMode() const { return m_networkMode; }
    QStringList networkInterfaces() const { return m_networkInterfaces; }
    metrics::NetworkSelection networkSelection() const;
    int dashboardRefreshSeconds() const { return m_dashboardRefreshSeconds; }
    bool autostartEnabled() const { return m_autostartEnabled; }
    QStringList customMetricFiles() const { return m_customMetricFiles; }
    // Metrics Bar: disabled by default. Items are at most three stable metric
    // ids (built-in or "custom:<path>") in the user's order; a custom source
    // that is temporarily missing stays selected.
    bool metricsBarEnabled() const { return m_metricsBarEnabled; }
    QStringList metricsBarItems() const { return m_metricsBarItems; }
    static QStringList defaultMetricsBarItems();
    // F5 built-in metrics: the GPU single-GPU consumers follow ("auto" or a
    // device id), the FPS source ("mangohud" or "off"), and the update-check
    // policy (interval in minutes, 0 = off; remote metadata refresh opt-in).
    QString gpuDevice() const { return m_gpuDevice; }
    QString fpsSource() const { return m_fpsSource; }
    int updatesIntervalMinutes() const { return m_updatesIntervalMinutes; }
    bool updatesRemoteRefresh() const { return m_updatesRemoteRefresh; }
    bool showCpuTemperature() const { return m_showCpuTemperature; }
    bool showGpuMemory() const { return m_showGpuMemory; }
    bool showGpuTemperature() const { return m_showGpuTemperature; }
    bool showFps() const { return m_showFps; }
    static bool isValidGpuDevice(const QString &id);
    static bool isValidFpsSource(const QString &source);
    static bool isValidUpdatesInterval(int minutes);
    static QList<int> updatesIntervals();
    QString fileName() const { return m_settings.fileName(); }

    static bool isValidStoredRunnerId(const QString &id);
    static QString speedModeName(SpeedMode mode);
    static QString networkModeName(metrics::NetworkPolicyMode mode);

public slots:
    void setRunnerId(const QString &id);
    void setSpeedMode(SpeedMode mode);
    void setNetworkMode(metrics::NetworkPolicyMode mode);
    void setNetworkInterfaces(QStringList interfaces);
    void setDashboardRefreshSeconds(int seconds);
    void setAutostartEnabled(bool enabled);
    void setCustomMetricFiles(QStringList files);
    void setMetricsBarEnabled(bool enabled);
    void setMetricsBarItems(QStringList ids);
    void setGpuDevice(const QString &id);
    void setFpsSource(const QString &source);
    void setUpdatesIntervalMinutes(int minutes);
    void setUpdatesRemoteRefresh(bool remote);
    void setShowCpuTemperature(bool show);
    void setShowGpuMemory(bool show);
    void setShowGpuTemperature(bool show);
    void setShowFps(bool show);
    void restoreDefaults();
    // Re-reads the file (written by another process: the backend, a
    // frontend, "aqtruncat autostart") and emits the signals of the values
    // that actually changed. Returns whether anything changed.
    bool reload();

signals:
    void runnerIdChanged(const QString &id);
    void speedModeChanged(aqtruncat::SpeedMode mode);
    void networkSettingsChanged();
    void dashboardRefreshSecondsChanged(int seconds);
    void autostartEnabledChanged(bool enabled);
    void customMetricFilesChanged(const QStringList &files);
    void metricsBarChanged();
    void systemMetricsSettingsChanged(); // settings that change backend metric collection
    void displaySettingsChanged(); // global presentation visibility preferences

private:
    void load();
    void write(const QString &key, const QVariant &value);
    static QStringList normalizedInterfaces(QStringList interfaces);
    static QStringList normalizedCustomMetricFiles(QStringList files);
    static QStringList normalizedMetricsBarItems(QStringList ids);

    QString m_defaultRunnerId;
    QSettings m_settings;
    QString m_runnerId;
    SpeedMode m_speedMode = SpeedMode::Normal;
    metrics::NetworkPolicyMode m_networkMode = metrics::NetworkPolicyMode::Automatic;
    QStringList m_networkInterfaces;
    int m_dashboardRefreshSeconds = 1;
    bool m_autostartEnabled = false;
    QStringList m_customMetricFiles;
    bool m_metricsBarEnabled = false;
    QStringList m_metricsBarItems;
    QString m_gpuDevice = QStringLiteral("auto");
    QString m_fpsSource = QStringLiteral("mangohud");
    int m_updatesIntervalMinutes = 60;
    bool m_updatesRemoteRefresh = false;
    bool m_showCpuTemperature = true;
    bool m_showGpuMemory = true;
    bool m_showGpuTemperature = true;
    bool m_showFps = true;
};

} // namespace aqtruncat
