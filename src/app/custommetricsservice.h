#pragma once

#include "custommetrics/commandmetric.h"
#include "custommetrics/custommetric.h"

#include <QFileSystemWatcher>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>

#include <map>
#include <memory>

namespace aqtruncat { class AppSettings; }

class CustomMetricsService : public QObject
{
    Q_OBJECT

public:
    explicit CustomMetricsService(aqtruncat::AppSettings *settings,
                                  QString discoveryDirectory = {}, QObject *parent = nullptr);
    ~CustomMetricsService() override;

    const QList<aqtruncat::custommetrics::Source> &sources() const { return m_sources; }
    QString discoveryDirectory() const { return m_discoveryDirectory; }

    bool registerFile(const QString &path, QString *error = nullptr);
    void unregisterFile(const QString &path);

public slots:
    void start();
    void stop();
    void rescan();

signals:
    void sourcesChanged(const QList<aqtruncat::custommetrics::Source> &sources);

private:
    static QString absolutePath(const QString &path);
    static QString identityFor(const QString &path);
    struct CommandJob;
    void scheduleReload();
    void reload(bool finalAttempt);
    void rebuildWatches();
    bool hasDegradedExplicitSource() const;
    void syncCommandJobs(const QList<aqtruncat::custommetrics::CommandDefinition> &definitions);
    void scheduleCommandWake();
    void processCommandWake();
    void startCommand(const QString &id);
    void readCommandOutput(const QString &id);
    void finishCommand(const QString &id, int exitCode, QProcess::ExitStatus exitStatus);
    void failCommand(const QString &id, aqtruncat::custommetrics::State state,
                     const QString &message);
    aqtruncat::custommetrics::Source *sourceFor(const QString &id);
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
};
