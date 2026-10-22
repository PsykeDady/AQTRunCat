#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <memory>

// One bounded, non-interactive subprocess run (F5 update checks), with the
// F1 command-metric rules: a direct executable and argument list (no shell,
// no elevation), its own process group, stdin from /dev/null, a C locale,
// a wall-clock timeout and per-stream output limits that kill the whole
// group, and a bounded stop. At most one run at a time.
class BoundedProcess : public QObject
{
    Q_OBJECT

public:
    struct Result {
        int exitCode = -1;
        bool normalExit = false;
        bool failedToStart = false;
        bool timedOut = false;
        bool overflow = false;
        QString error; // set for failedToStart/timedOut/overflow/crash
        QByteArray standardOutput;
        QByteArray standardError;
        bool ok() const { return error.isEmpty(); }
    };

    explicit BoundedProcess(QObject *parent = nullptr);
    ~BoundedProcess() override;

    bool isRunning() const { return m_process != nullptr; }
    // program: an absolute path (resolved by the caller). finished() is
    // always emitted, also when the start fails (asynchronously).
    void start(const QString &program, const QStringList &arguments, int timeoutMs, qsizetype maxBytesPerStream);
    // SIGTERM to the group, a bounded wait, then SIGKILL; no finished().
    void stop();

signals:
    void finished(const BoundedProcess::Result &result);

private:
    void read();
    void finish(int exitCode, QProcess::ExitStatus status);

    std::unique_ptr<QProcess> m_process;
    QTimer m_deadline;
    Result m_result;
    qsizetype m_maxBytes = 0;
    int m_timeoutMs = 0;
};
