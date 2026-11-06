#include "usagerefresh.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>

namespace aqtruncat::claude {
namespace {

QDateTime timeOf(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0)
        return {};
    return QDateTime::fromMSecsSinceEpoch(qint64(value.toDouble()), QTimeZone::UTC);
}

double numberOf(const QJsonObject &object, const char *key)
{
    const double value = object.value(QLatin1String(key)).toDouble(0.0);
    return std::isfinite(value) ? value : 0.0;
}

QString firstLine(const QByteArray &bytes)
{
    for (const QByteArray &line : bytes.split('\n')) {
        const QString text = QString::fromUtf8(line).trimmed();
        if (!text.isEmpty())
            return text.left(200);
    }
    return {};
}

} // namespace

QString findClaudeExecutable()
{
    QString found = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (found.isEmpty()) {
        found = QStandardPaths::findExecutable(QStringLiteral("claude"),
                                               {QDir::home().filePath(QStringLiteral(".local/bin")),
                                                QDir::home().filePath(QStringLiteral(".claude/local"))});
    }
    return found;
}

QStringList usageCommandArguments()
{
    return {QStringLiteral("-p"), QStringLiteral("/usage"), QStringLiteral("--output-format"), QStringLiteral("json"),
            QStringLiteral("--no-session-persistence"), QStringLiteral("--strict-mcp-config")};
}

QString usageCommandText()
{
    return QStringLiteral("claude ") + usageCommandArguments().join(u' ');
}

QString refreshStateFile()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty() || !QDir::isAbsolutePath(runtime))
        runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return QDir(runtime).filePath(QStringLiteral("aqtruncat/claude-usage-refresh.json"));
}

RefreshState readRefreshState(const QString &stateFile, const QString &configDirectory)
{
    RefreshState state;
    QFile file(stateFile);
    if (!file.open(QIODevice::ReadOnly))
        return state;
    const QJsonObject object = QJsonDocument::fromJson(file.read(64 * 1024)).object();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || QDir::cleanPath(object.value(QStringLiteral("configDirectory")).toString()) != QDir::cleanPath(configDirectory))
        return state;
    state.lastAttemptAt = timeOf(object, "lastAttemptMs");
    state.lastSuccessAt = timeOf(object, "lastSuccessMs");
    state.error = object.value(QStringLiteral("error")).toString().left(300);
    state.stopped = object.value(QStringLiteral("stopped")).toString().left(300);
    return state;
}

bool writeRefreshState(const QString &stateFile, const QString &configDirectory, const RefreshState &state)
{
    // Times and short reasons only: nothing of the command's output.
    QJsonObject object{{QStringLiteral("version"), 1},
                       {QStringLiteral("configDirectory"), QDir::cleanPath(configDirectory)}};
    if (state.lastAttemptAt.isValid())
        object.insert(QStringLiteral("lastAttemptMs"), double(state.lastAttemptAt.toMSecsSinceEpoch()));
    if (state.lastSuccessAt.isValid())
        object.insert(QStringLiteral("lastSuccessMs"), double(state.lastSuccessAt.toMSecsSinceEpoch()));
    if (!state.error.isEmpty())
        object.insert(QStringLiteral("error"), state.error);
    if (!state.stopped.isEmpty())
        object.insert(QStringLiteral("stopped"), state.stopped);
    const QString directory = QFileInfo(stateFile).absolutePath();
    if (!QDir().mkpath(directory))
        return false;
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(stateFile);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && file.write(bytes) == bytes.size() && file.commit();
}

bool refreshDue(const RefreshState &state, const std::optional<QDateTime> &newestReport, RefreshTrigger trigger,
                qint64 refreshSeconds, const QDateTime &now)
{
    if (!state.stopped.isEmpty())
        return false;
    // A clock that went backwards must not block refreshes for good.
    const bool attempted = state.lastAttemptAt.isValid() && state.lastAttemptAt <= now;
    const qint64 sinceAttempt = attempted ? state.lastAttemptAt.secsTo(now) : -1;
    if (trigger == RefreshTrigger::Manual)
        return !attempted || sinceAttempt >= ManualRefreshSpacingSeconds;
    const bool old = !newestReport || !newestReport->isValid() || newestReport->secsTo(now) >= refreshSeconds;
    if (!old)
        return false;
    if (!attempted)
        return true;
    // After a success the cache is new, so "old" again means a full interval
    // went by; should the cache not move after a successful run, this still
    // keeps runs an interval apart.
    return sinceAttempt >= (state.error.isEmpty() ? refreshSeconds : std::min(refreshSeconds, RefreshRetrySeconds));
}

UsageCommandResult judgeUsageCommandOutput(const QByteArray &standardOutput)
{
    UsageCommandResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(standardOutput.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("claude printed no JSON result");
        return result;
    }
    const QJsonObject object = document.object();
    const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
    const double tokens = numberOf(usage, "input_tokens") + numberOf(usage, "output_tokens")
        + numberOf(usage, "cache_creation_input_tokens") + numberOf(usage, "cache_read_input_tokens");
    const QJsonValue localCommand = object.value(QStringLiteral("local_command"));
    if (tokens > 0 || numberOf(object, "total_cost_usd") > 0 || numberOf(object, "num_turns") > 0
        || (!localCommand.isUndefined() && localCommand != QLatin1String("usage"))) {
        result.modelUsed = true;
        result.error = QStringLiteral("this Claude Code version did not run /usage locally "
                                      "(it was sent to the model as a prompt)");
        return result;
    }
    if (object.value(QStringLiteral("is_error")).toBool()) {
        result.error = QStringLiteral("claude reported an error");
        return result;
    }
    result.ok = true;
    return result;
}

UsageCommandResult runUsageCommand(const QString &claudeExecutable, const QString &configDirectory,
                                   bool configDirectoryOverridden, int timeoutMs)
{
    UsageCommandResult result;
    if (claudeExecutable.isEmpty()) {
        result.error = QStringLiteral("the claude executable was not found in PATH");
        return result;
    }
    QProcess process;
    if (configDirectoryOverridden) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("CLAUDE_CONFIG_DIR"), configDirectory);
        process.setProcessEnvironment(environment);
    }
    // No terminal and no input: in print mode claude would otherwise wait
    // for stdin to be closed and append it to the prompt.
    process.setStandardInputFile(QProcess::nullDevice());
    process.setProgram(claudeExecutable);
    process.setArguments(usageCommandArguments());
    // Its own process group, so that a run that hangs is stopped together
    // with anything it started.
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(timeoutMs)) {
        result.error = QStringLiteral("cannot start claude: %1").arg(process.errorString());
        return result;
    }
    const qint64 group = process.processId();
    QElapsedTimer clock;
    clock.start();
    QByteArray standardOutput;
    QByteArray standardError;
    bool timedOut = false;
    while (process.state() != QProcess::NotRunning) {
        const qint64 remaining = timeoutMs - clock.elapsed();
        if (remaining <= 0) {
            timedOut = true;
            break;
        }
        process.waitForReadyRead(int(std::min<qint64>(remaining, 1000)));
        if (standardOutput.size() < MaximumCommandOutputBytes)
            standardOutput += process.readAllStandardOutput();
        else
            process.readAllStandardOutput();
        if (standardError.size() < 4096)
            standardError += process.readAllStandardError();
        else
            process.readAllStandardError();
    }
    if (timedOut) {
        if (group > 0)
            ::kill(-pid_t(group), SIGTERM);
        if (!process.waitForFinished(1000) && group > 0)
            ::kill(-pid_t(group), SIGKILL);
        process.waitForFinished(1000);
    }
    standardOutput += process.readAllStandardOutput();
    standardError += process.readAllStandardError();
    if (group > 0)
        ::kill(-pid_t(group), SIGKILL); // helpers it may have left behind
    if (timedOut) {
        result.error = QStringLiteral("claude did not finish within %1 s").arg(timeoutMs / 1000.0);
        return result;
    }
    // The envelope is judged first: also a failing run must stop automatic
    // refresh if it reached the model.
    UsageCommandResult judged = judgeUsageCommandOutput(standardOutput.left(MaximumCommandOutputBytes));
    if (judged.modelUsed)
        return judged;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString reason = firstLine(standardError);
        result.error = process.exitStatus() != QProcess::NormalExit
            ? QStringLiteral("claude crashed")
            : QStringLiteral("claude exited with status %1%2").arg(process.exitCode())
                  .arg(reason.isEmpty() ? QString() : QStringLiteral(": ") + reason);
        return result;
    }
    return judged;
}

} // namespace aqtruncat::claude
