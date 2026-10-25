#pragma once

#include "custommetricsmodel.h"
#include "custommetrics/commandmetric.h"
#include "custommetrics/custommetric.h"
#include "custommetrics/metricgroup.h"

#include <QFileSystemWatcher>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <map>
#include <memory>

namespace aqtruncat { class AppSettings; }

class CustomMetricsService : public CustomMetricsModel
{
    Q_OBJECT

public:
    explicit CustomMetricsService(aqtruncat::AppSettings *settings,
                                  QString discoveryDirectory = {}, QObject *parent = nullptr);
    ~CustomMetricsService() override;

    const QList<aqtruncat::custommetrics::Source> &sources() const override { return m_sources; }
    QString discoveryDirectory() const override { return m_discoveryDirectory; }

    bool registerFile(const QString &path, QString *error = nullptr) override;
    void unregisterFile(const QString &path) override;
    bool setEnabled(const QString &path, bool enabled, QString *error = nullptr) override;
    // v2F1.6: the explicit reload (Settings, D-Bus ReloadCustomMetrics):
    // reloadNow answers at once, reloadConfigurations also emits it.
    void reloadConfigurations() override;
    aqtruncat::custommetrics::ReloadReport reloadNow();
    // v2F1.7: a manual refresh (an icon clicked; D-Bus RefreshCustomMetric):
    // refreshNow answers at once, refreshMetric also emits it.
    void refreshMetric(const QString &sourceId) override;
    aqtruncat::custommetrics::RefreshResult refreshNow(const QString &sourceId);

public slots:
    void start();
    void stop();
    void rescan();

private:
    static QString absolutePath(const QString &path);
    static QString identityFor(const QString &path);
    struct CommandJob;
    void scheduleReload();
    aqtruncat::custommetrics::ReloadReport reload(bool finalAttempt);
    void rebuildWatches();
    bool hasDegradedExplicitSource() const;
    void syncCommandJobs(const QList<aqtruncat::custommetrics::CommandDefinition> &definitions);
    void scheduleCommandWake();
    void processCommandWake();
    void startCommand(const QString &id);
    // v2F1.7: starts a manual run of a command source unless it is running
    // or ran less than ManualRefreshSpacing ago (then it is coalesced).
    void requestRun(const QString &id, aqtruncat::custommetrics::RefreshResult &result);
    // run: the job's run the call belongs to; a call for an earlier run (a
    // replaced or removed job) is ignored.
    CommandJob *runningJob(const QString &id, quint64 run);
    void readCommandOutput(const QString &id, quint64 run);
    void finishCommand(const QString &id, quint64 run, int exitCode, QProcess::ExitStatus exitStatus);
    void failCommand(const QString &id, aqtruncat::custommetrics::State state,
                     const QString &message);
    aqtruncat::custommetrics::Source *sourceFor(const QString &id);
    const aqtruncat::custommetrics::Source *previousFor(const aqtruncat::custommetrics::Source &source) const;
    void applyCommand(aqtruncat::custommetrics::Source &source, const aqtruncat::custommetrics::CommandParseResult &command,
                      const aqtruncat::custommetrics::Source *previous,
                      QList<aqtruncat::custommetrics::CommandDefinition> &definitions);
    // v2F1.5: a group's source and, recursively, its children's.
    void appendGroup(aqtruncat::custommetrics::Source group, const QJsonObject &object,
                     QList<aqtruncat::custommetrics::Source> &next,
                     QList<aqtruncat::custommetrics::CommandDefinition> &definitions);
    void appendChildren(const QString &parentId, const QString &directory,
                        const aqtruncat::custommetrics::Inheritance &inheritance, int depth, QStringList &visited,
                        QList<aqtruncat::custommetrics::Source> &next,
                        QList<aqtruncat::custommetrics::CommandDefinition> &definitions);
    void setOutcome(aqtruncat::custommetrics::Source &source, aqtruncat::custommetrics::State state,
                    const QString &message);
    void disable(aqtruncat::custommetrics::Source &source, const QString &title,
                 const std::optional<QString> &symbol,
                 const std::optional<aqtruncat::custommetrics::IconReference> &icon);
    void publish();
    void stopCommand(CommandJob &job);
    void stopCommands();

    aqtruncat::AppSettings *m_settings = nullptr;
    QString m_discoveryDirectory;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_retry;
    QTimer m_recovery;
    QTimer m_commandWake;
    QElapsedTimer m_clock;
    QList<aqtruncat::custommetrics::Source> m_sources;
    std::map<QString, std::unique_ptr<CommandJob>> m_commandJobs;
    int m_retryAttempt = 0;
    bool m_active = false;
    // v2F1.6: the reload being built (a reload never runs inside another),
    // why it cannot be applied, the last failure logged, and the next run id.
    bool m_reloading = false;
    QString m_reloadFailure;
    QString m_loggedFailure;
    quint64 m_nextRun = 0;
    // v2F1.6: each source's icon warning (Source::warnings), checked by
    // every reload (an icon installed later is found by the next one).
    QHash<QString, QString> m_iconWarnings;
};
