#pragma once

#include "metrics/systemmetrics.h"
#include "custommetrics/custommetric.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace aqtruncat { class AppSettings; }
class AutostartManager;
class CpuMonitor;
class CustomMetricsService;
class MetricSampler;
class ProviderIntegration;
class RunnerSpeedController;
class UpdatesService;

// F4.1: the AQTRunCat backend. It owns the only instances of the metric
// engine (system sampler, Custom Metrics with the F2/F3 adapters, runner
// speed from CPU load, autostart, official-integration management) and
// publishes them on the D-Bus session bus as io.github.psykedady.AQTRunCat1
// (docs/backend-dbus-api-cli.md). QtCore + QtDBus only.
class BackendService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.psykedady.AQTRunCat1")

public:
    struct Config {
        QString defaultRunnerId = QStringLiteral("cat");
        QString settingsFile;        // empty: the user's AQTRunCat.ini
        QString discoveryDirectory;  // empty: $XDG_DATA_HOME/AQTRunCat/metrics
        QString frontendExecutable;  // autostart Exec; empty: no autostart handling
        QString claudeAdapter;       // empty: not offered
        QString codexAdapter;
        std::string homePath;
        bool manageAutostart = true;
        // F5 update checks: tool search path and root prefix (tests); the
        // first check runs this long after start().
        QStringList updatesSearchPath;
        QString updatesRoot;
        int updatesFirstCheckDelayMs = 30000;
        // F4.2: GetRunners payload for the stored runner id (runnercatalog.h).
        // Unset: no "runners" capability (the catalogue needs QtGui, which
        // only aqtruncatd links).
        std::function<QJsonObject(const QString &requestedId)> runnerCatalog;
    };

    explicit BackendService(Config config, QObject *parent = nullptr);
    ~BackendService() override;

    // Exports the object and then claims the well-known name (never queued,
    // never replacing an owner). false: another backend owns the name.
    bool registerOn(QDBusConnection connection, QString *error);
    // Starts sampling, Custom Metrics and integrations (after registerOn).
    void start();

    // Payloads, also used in-process by tests.
    QJsonObject info() const;
    QJsonObject state() const;
    QJsonArray metrics() const { return m_metrics; }
    QJsonObject runner() const;
    QJsonArray integrations() const;
    QJsonArray capabilities() const;
    // CLOCK_MONOTONIC seconds of the latest system sample, null before it.
    QJsonValue sampleTime() const;
    quint64 generation() const { return m_generation; }

public slots:
    Q_SCRIPTABLE QString GetInfo();
    Q_SCRIPTABLE QString GetState();
    Q_SCRIPTABLE QString GetMetrics();
    Q_SCRIPTABLE QString GetSettings();
    Q_SCRIPTABLE QString GetRunner();
    Q_SCRIPTABLE QString GetIntegrations();
    Q_SCRIPTABLE QString GetRunners();
    Q_SCRIPTABLE void SetSetting(const QString &key, const QString &jsonValue);
    Q_SCRIPTABLE void RegisterCustomMetric(const QString &path);
    Q_SCRIPTABLE void UnregisterCustomMetric(const QString &path);
    // v2F1.2 (schema 3): writes "enabled" into a Custom Metric's file and
    // applies it at once. NotFound for an unknown source, InvalidArgument
    // for an integration's source, Failed when the file cannot be written.
    Q_SCRIPTABLE void SetCustomMetricEnabled(const QString &path, bool enabled);
    Q_SCRIPTABLE QString SetIntegrationEnabled(const QString &id, bool enabled);
    Q_SCRIPTABLE QString SetIntegrationLiveUpdates(const QString &id, bool on);

signals:
    Q_SCRIPTABLE void MetricsChanged(qulonglong generation, const QString &changes);
    Q_SCRIPTABLE void RunnerChanged(const QString &runner);
    Q_SCRIPTABLE void SettingsChanged(const QString &settings);
    Q_SCRIPTABLE void IntegrationsChanged(const QString &integrations);

private:
    struct PendingReply {
        QString integration;
        QDBusMessage message;
        QDBusConnection connection;
    };
    void scheduleMetrics();
    void publishMetrics();
    void publishRunner();
    void publishSettings();
    void applyAutostart();
    void watchSettingsFile();
    ProviderIntegration *integration(const QString &id) const;
    QString integrationTitle(const QString &id) const;
    bool fail(const QString &name, const QString &message);
    void integrationCommand(const QString &id, const QStringList &arguments);

    Config m_config;
    std::unique_ptr<aqtruncat::AppSettings> m_settings;
    std::unique_ptr<MetricSampler> m_sampler;
    std::unique_ptr<CustomMetricsService> m_custom;
    std::unique_ptr<CpuMonitor> m_cpu;
    std::unique_ptr<RunnerSpeedController> m_speed;
    std::unique_ptr<AutostartManager> m_autostart;
    std::unique_ptr<ProviderIntegration> m_claude;
    std::unique_ptr<ProviderIntegration> m_codex;
    std::unique_ptr<UpdatesService> m_updates;
    std::optional<aqtruncat::metrics::SystemMetricsSnapshot> m_system;
    QList<aqtruncat::custommetrics::Source> m_sources;
    QJsonArray m_metrics;                  // last published, in API order
    QHash<QString, QJsonObject> m_metricsById;
    quint64 m_generation = 0;
    int m_fps = 0;
    int m_targetFps = 0;
    QJsonObject m_lastRunner;
    QJsonObject m_lastSettings;
    QJsonArray m_lastIntegrations;
    QTimer m_coalesce;
    QTimer m_settingsReload;
    QFileSystemWatcher m_settingsWatcher;
    QList<PendingReply> m_pendingReplies;
    QDateTime m_startedAt;
};
