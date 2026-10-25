#pragma once

#include "app/custommetricsmodel.h"
#include "metrics/systemmetrics.h"

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <optional>

// F4.1: a frontend's view of the backend (aqtruncatd), through the public
// D-Bus API only. It turns the API state back into the types the Qt
// frontend already uses (SystemMetricsSnapshot, Custom Metrics sources) and
// keeps it current from MetricsChanged deltas, resynchronising with
// GetState after a gap, a reconnection or a backend restart. QtCore + QtDBus.
//
// Backend absent: D-Bus activation starts it; if the bus cannot (no
// activation file, e.g. a build tree), the client starts backendExecutable
// itself, at most every 10 s. A lost backend keeps the last values for the
// grace period (no flicker on a quick restart), then they are reported as
// unavailable until it is back.
class BackendClient : public CustomMetricsModel
{
    Q_OBJECT

public:
    struct Options {
        QDBusConnection connection = QDBusConnection::sessionBus();
        QString backendExecutable; // fallback start; empty: activation only
        int graceMs = 3000;
        int retryMs = 3000;
    };

    explicit BackendClient(Options options, QObject *parent = nullptr);

    void start();
    bool connected() const { return m_synced; }
    quint64 generation() const { return m_generation; }
    const QJsonArray &metrics() const { return m_metrics; }
    QJsonObject runner() const { return m_runner; }

    // CustomMetricsModel (Settings window): the backend's sources.
    const QList<aqtruncat::custommetrics::Source> &sources() const override { return m_sources; }
    QString discoveryDirectory() const override { return m_discoveryDirectory; }
    bool registerFile(const QString &path, QString *error = nullptr) override;
    void unregisterFile(const QString &path) override;
    bool setEnabled(const QString &path, bool enabled, QString *error = nullptr) override;
    // v2F1.6: asynchronous ReloadCustomMetrics (never blocks the GUI); a
    // request made while one is pending shares its answer.
    void reloadConfigurations() override;
    // v2F1.7: asynchronous RefreshCustomMetric; a second request for a
    // metric whose request is still pending is dropped (the backend
    // coalesces the rest).
    void refreshMetric(const QString &sourceId) override;

signals:
    void systemSnapshot(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);
    // fps: the runner speed the backend computed from CPU load.
    void runnerChanged(int fps, const QString &runnerId);
    // Settings changed in the shared file: reload them.
    void settingsChanged();
    void connectionChanged(bool connected);

private slots:
    void onMetricsChanged(qulonglong generation, const QString &changes);
    void onRunnerChanged(const QString &runner);
    void onSettingsChanged(const QString &settings);

private:
    void requestState();
    void applyState(const QJsonObject &state);
    void applyRunner(const QJsonObject &runner);
    void publishMetrics(bool system, bool custom);
    void lost();
    void reportUnavailable();
    void startBackendIfPossible();

    Options m_options;
    QDBusServiceWatcher m_watcher;
    QTimer m_grace;
    QTimer m_retry;
    QElapsedTimer m_lastStart;
    // "sampleTime" of the state: the rebuilt snapshots carry it as
    // timeSeconds, which SnapshotThrottle needs.
    std::optional<double> m_sampleTime;
    bool m_synced = false;
    bool m_requesting = false;
    bool m_reloading = false;
    QSet<QString> m_refreshing;
    quint64 m_generation = 0;
    QJsonArray m_metrics;
    QJsonObject m_runner;
    QList<aqtruncat::custommetrics::Source> m_sources;
    QString m_discoveryDirectory;
};
