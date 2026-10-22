#include "boundedprocess.h"

#include "processgroup.h"

#include <QProcessEnvironment>

namespace {
constexpr int StopWaitMilliseconds = 250;
}

BoundedProcess::BoundedProcess(QObject *parent)
    : QObject(parent)
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (!m_process)
            return;
        if (m_result.error.isEmpty()) {
            m_result.timedOut = true;
            m_result.error = tr("timed out after %1 seconds").arg(m_timeoutMs / 1000);
        }
        aqtruncat::process::signalGroup(*m_process, SIGKILL);
    });
}

BoundedProcess::~BoundedProcess()
{
    stop();
}

void BoundedProcess::start(const QString &program, const QStringList &arguments, int timeoutMs, qsizetype maxBytesPerStream)
{
    if (m_process)
        return;
    m_result = Result();
    m_maxBytes = maxBytesPerStream;
    m_timeoutMs = timeoutMs;
    m_process = std::make_unique<QProcess>();
    QProcess *process = m_process.get();
    // Parseable, non-interactive output: C locale, no pager, no colours, no
    // prompts. Nothing else of the environment is changed.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    environment.remove(QStringLiteral("LANGUAGE"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    environment.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    environment.insert(QStringLiteral("PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("DEBIAN_FRONTEND"), QStringLiteral("noninteractive"));
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    aqtruncat::process::runInOwnGroup(*process);
    connect(process, &QProcess::readyReadStandardOutput, this, &BoundedProcess::read);
    connect(process, &QProcess::readyReadStandardError, this, &BoundedProcess::read);
    connect(process, &QProcess::finished, this, &BoundedProcess::finish);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        m_result.failedToStart = true;
        m_result.error = tr("could not start: %1").arg(m_process->errorString());
        // Never destroy the QProcess inside its own signal.
        QTimer::singleShot(0, this, [this] { finish(-1, QProcess::CrashExit); });
    });
    m_deadline.start(timeoutMs);
    process->start(program, arguments, QIODevice::ReadOnly);
}

void BoundedProcess::read()
{
    if (!m_process)
        return;
    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    if (m_result.standardOutput.size() + out.size() > m_maxBytes
        || m_result.standardError.size() + err.size() > m_maxBytes) {
        if (m_result.error.isEmpty()) {
            m_result.overflow = true;
            m_result.error = tr("output exceeds %1 bytes").arg(m_maxBytes);
        }
        aqtruncat::process::signalGroup(*m_process, SIGKILL);
        return;
    }
    m_result.standardOutput.append(out);
    m_result.standardError.append(err);
}

void BoundedProcess::finish(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    read();
    m_deadline.stop();
    m_process->disconnect(this);
    m_process.release()->deleteLater();
    m_result.exitCode = exitCode;
    m_result.normalExit = status == QProcess::NormalExit && !m_result.failedToStart;
    if (m_result.error.isEmpty() && !m_result.normalExit)
        m_result.error = tr("crashed");
    const Result result = m_result;
    emit finished(result);
}

void BoundedProcess::stop()
{
    m_deadline.stop();
    if (!m_process)
        return;
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        aqtruncat::process::signalGroup(*m_process, SIGTERM);
        if (!m_process->waitForFinished(StopWaitMilliseconds)) {
            aqtruncat::process::signalGroup(*m_process, SIGKILL);
            m_process->waitForFinished(StopWaitMilliseconds);
        }
        aqtruncat::process::signalGroup(*m_process, SIGKILL);
    }
    m_process.reset();
}
