#include "providerintegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {

// Management commands only touch a few small files.
constexpr int CommandTimeoutMs = 15000;
constexpr qsizetype MaxOutputBytes = 64 * 1024;

} // namespace

ProviderIntegration::ProviderIntegration(QString id, QString executable, QObject *parent)
    : QObject(parent)
    , m_id(std::move(id))
    , m_executable(std::move(executable))
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(200);
    connect(&m_debounce, &QTimer::timeout, this, &ProviderIntegration::refresh);
    // Changes made from the command line (or by the provider) show up here.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
}

ProviderIntegration::~ProviderIntegration()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        delete m_process;
    }
}

QString ProviderIntegration::findAdapter(const QString &name)
{
    const QFileInfo sibling(QDir(QCoreApplication::applicationDirPath()).filePath(name));
    if (sibling.isFile() && sibling.isExecutable())
        return sibling.absoluteFilePath();
    return QStandardPaths::findExecutable(name);
}

void ProviderIntegration::refresh()
{
    if (!installed())
        return;
    // One pending status query is enough.
    for (const Request &request : std::as_const(m_queue)) {
        if (request.statusQuery)
            return;
    }
    m_queue.append({{QStringLiteral("status"), QStringLiteral("--json")}, true});
    startNext();
}

void ProviderIntegration::run(const QStringList &arguments)
{
    if (!installed()) {
        emit commandFinished(false, tr("The adapter for this integration is not installed."));
        return;
    }
    m_queue.append({arguments, false});
    startNext();
}

void ProviderIntegration::startNext()
{
    if (m_process || m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    m_process = new QProcess(this);
    m_process->setProgram(m_executable);
    m_process->setArguments(m_current.arguments);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) { finished(exitCode, status == QProcess::CrashExit); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(-1, true);
    });
    QTimer::singleShot(CommandTimeoutMs, m_process, [process = m_process] { process->kill(); });
    if (!m_current.statusQuery)
        emit busyChanged(true);
    m_process->start();
}

void ProviderIntegration::finished(int exitCode, bool crashed)
{
    QProcess *process = m_process;
    if (!process)
        return;
    m_process = nullptr;
    const QString out = QString::fromUtf8(process->readAllStandardOutput().left(MaxOutputBytes)).trimmed();
    const QString err = QString::fromUtf8(process->readAllStandardError().left(MaxOutputBytes)).trimmed();
    const QString startError = process->error() == QProcess::FailedToStart ? process->errorString() : QString();
    process->disconnect(this);
    process->deleteLater();
    const bool ok = !crashed && exitCode == 0;

    if (m_current.statusQuery) {
        const QJsonObject status = ok ? QJsonDocument::fromJson(out.toUtf8()).object() : QJsonObject();
        if (status.value(QStringLiteral("integration")).toString() == m_id) {
            m_lastError.clear();
            if (status != m_status) {
                m_status = status;
                watchReportedFiles();
                emit statusChanged();
            }
        } else {
            m_lastError = !startError.isEmpty() ? startError
                : !err.isEmpty() ? err : tr("The adapter did not report its status.");
            emit statusChanged();
        }
    } else {
        m_lastMessage = ok ? out : (!err.isEmpty() ? err : !startError.isEmpty() ? startError
                                                                                   : tr("The command failed."));
        emit busyChanged(false);
        emit commandFinished(ok, m_lastMessage);
        refresh();
    }
    startNext();
}

void ProviderIntegration::watchReportedFiles()
{
    // The metric file, its package directory and the metrics directory
    // above it (package created or removed), plus the provider settings file
    // the adapter may have edited. Files replaced atomically drop out of the
    // watch, so the list is rebuilt after every status.
    QStringList paths;
    const QString metricFile = m_status.value(QStringLiteral("metricFile")).toString();
    if (!metricFile.isEmpty()) {
        const QFileInfo info(metricFile);
        paths << info.absoluteFilePath() << info.absolutePath() << QFileInfo(info.absolutePath()).absolutePath();
    }
    const QString settingsFile = m_status.value(QStringLiteral("settingsFile")).toString();
    if (!settingsFile.isEmpty())
        paths << settingsFile;
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    QStringList existing;
    for (const QString &path : std::as_const(paths)) {
        if (QFileInfo::exists(path) && !existing.contains(path))
            existing.append(path);
    }
    if (!existing.isEmpty())
        m_watcher.addPaths(existing);
}
