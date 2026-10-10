#pragma once

#include "custommetrics/custommetric.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace aqtruncat { class AppSettings; }

class CustomMetricsService : public QObject
{
    Q_OBJECT

public:
    explicit CustomMetricsService(aqtruncat::AppSettings *settings,
                                  QString discoveryDirectory = {}, QObject *parent = nullptr);

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
    void scheduleReload();
    void reload(bool finalAttempt);
    void rebuildWatches();
    bool hasDegradedExplicitSource() const;

    aqtruncat::AppSettings *m_settings = nullptr;
    QString m_discoveryDirectory;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_retry;
    QTimer m_recovery;
    QList<aqtruncat::custommetrics::Source> m_sources;
    int m_retryAttempt = 0;
    bool m_active = false;
};
