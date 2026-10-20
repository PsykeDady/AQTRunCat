#pragma once

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>

class QProcess;

// An officially supported provider integration (F2: Claude Code) as seen by
// the Settings window. The provider's adapter executable owns the whole
// configuration (its F1 metric package and any provider-side opt-in), and
// this class only runs the adapter's management commands:
//
//   <adapter> status --json            → status()
//   <adapter> enable|disable|live-updates … (run())
//
// so the Settings window and the adapter's command line always read and
// write the same state. Commands run asynchronously, one at a time; status is
// refreshed after each command and when a file it reports changes. The
// metric values themselves still flow only through CustomMetricsService.
class ProviderIntegration : public QObject
{
    Q_OBJECT

public:
    // executable: absolute path of the adapter, empty when not installed.
    ProviderIntegration(QString id, QString executable, QObject *parent = nullptr);
    ~ProviderIntegration() override;

    // <directory of the running aqtruncat>/<name>, else <name> found in PATH.
    static QString findAdapter(const QString &name);

    QString id() const { return m_id; }
    QString executable() const { return m_executable; }
    bool installed() const { return !m_executable.isEmpty(); }
    bool busy() const { return m_process != nullptr; }
    bool statusKnown() const { return !m_status.isEmpty(); }
    const QJsonObject &status() const { return m_status; }
    QString lastError() const { return m_lastError; }
    QString lastMessage() const { return m_lastMessage; }

public slots:
    void refresh();
    void run(const QStringList &arguments);

signals:
    void statusChanged();
    void busyChanged(bool busy);
    // A management command finished; message is its output or error text.
    void commandFinished(bool ok, const QString &message);

private:
    struct Request {
        QStringList arguments;
        bool statusQuery = false;
    };
    void startNext();
    void finished(int exitCode, bool crashed);
    void watchReportedFiles();

    QString m_id;
    QString m_executable;
    QJsonObject m_status;
    QString m_lastError;
    QString m_lastMessage;
    QList<Request> m_queue;
    Request m_current;
    QProcess *m_process = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};
