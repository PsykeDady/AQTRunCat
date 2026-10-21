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
};

} // namespace aqtruncat
