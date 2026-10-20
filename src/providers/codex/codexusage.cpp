#include "codexusage.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <unistd.h>

namespace aqtruncat::codex {
namespace {

using aqtruncat::providers::QuotaValue;

std::optional<double> finiteNonNegative(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0)
        return std::nullopt;
    return number;
}

std::optional<int> positiveMinutes(const QJsonValue &value)
{
    const std::optional<double> number = finiteNonNegative(value);
    if (!number || *number < 1 || *number > 1.0e7 || std::floor(*number) != *number)
        return std::nullopt;
    return int(*number);
}

// One window of a rollout event: {"used_percent", "window_minutes",
// "resets_at" (epoch s)}; older Codex versions wrote "resets_in_seconds".
std::optional<Window> rolloutWindow(const QJsonValue &value, const QDateTime &observedAt)
{
    const QJsonObject object = value.toObject();
    const auto percent = finiteNonNegative(object.value(QStringLiteral("used_percent")));
    const auto minutes = positiveMinutes(object.value(QStringLiteral("window_minutes")));
    if (!percent || !minutes)
        return std::nullopt;
    Window window;
    window.minutes = *minutes;
    window.value.usedPercent = *percent;
    window.value.observedAt = observedAt;
    if (const auto resets = finiteNonNegative(object.value(QStringLiteral("resets_at"))))
        window.value.resetsAt = QDateTime::fromSecsSinceEpoch(qint64(*resets), QTimeZone::UTC);
    else if (const auto in = finiteNonNegative(object.value(QStringLiteral("resets_in_seconds"))))
        window.value.resetsAt = observedAt.addSecs(qint64(*in));
    return window;
}

std::optional<Window> liveWindow(const QJsonValue &value, const QDateTime &observedAt)
{
    const QJsonObject object = value.toObject();
    const auto percent = finiteNonNegative(object.value(QStringLiteral("usedPercent")));
    const auto minutes = positiveMinutes(object.value(QStringLiteral("windowDurationMins")));
    if (!percent || !minutes)
        return std::nullopt;
    Window window;
    window.minutes = *minutes;
    window.value.usedPercent = *percent;
    window.value.observedAt = observedAt;
    if (const auto resets = finiteNonNegative(object.value(QStringLiteral("resetsAt"))))
        window.value.resetsAt = QDateTime::fromSecsSinceEpoch(qint64(*resets), QTimeZone::UTC);
    return window;
}

void addWindow(Report &report, const std::optional<Window> &window)
{
    // A bucket reports each window length once; keep the first.
    if (window && std::none_of(report.windows.cbegin(), report.windows.cend(),
                               [&](const Window &w) { return w.minutes == window->minutes; }))
        report.windows.append(*window);
}

bool isCodexBucket(const QJsonValue &limitId)
{
    return limitId.isUndefined() || limitId.isNull() || limitId.toString() == CodexLimitId;
}

QList<QFileInfo> newestRollouts(const QString &codexHome)
{
    QList<QFileInfo> files;
    int scanned = 0;
    for (const QString &root : {QStringLiteral("/sessions"), QStringLiteral("/archived_sessions")}) {
        QDirIterator it(codexHome + root, {QStringLiteral("rollout-*.jsonl")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext() && scanned < MaximumScannedFiles) {
            it.next();
            ++scanned;
            files.append(it.fileInfo());
        }
    }
    // A resumed session appends to its old file: modification time, not
    // the date in the path, says what is newest.
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        if (a.lastModified() != b.lastModified())
            return a.lastModified() > b.lastModified();
        return a.filePath() > b.filePath();
    });
    if (files.size() > MaximumRolloutFiles)
        files.resize(MaximumRolloutFiles);
    return files;
}

// Newest codex report in the file, reading its tail backwards and growing
// the tail up to MaximumTailBytes.
std::optional<Report> newestInFile(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot read a Codex session file: %1").arg(file.errorString());
        return std::nullopt;
    }
    const qint64 size = file.size();
    qint64 window = std::min(InitialTailBytes, MaximumTailBytes);
    for (;;) {
        const qint64 start = std::max<qint64>(0, size - window);
        if (!file.seek(start)) {
            error = QStringLiteral("cannot read a Codex session file: %1").arg(file.errorString());
            return std::nullopt;
        }
        QList<QByteArray> lines = file.read(size - start).split('\n');
        if (start > 0 && !lines.isEmpty())
            lines.removeFirst(); // partial line cut by the window
        for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
            if (!line->contains("\"token_count\"") || !line->contains("\"rate_limits\""))
                continue;
            if (std::optional<Report> report = reportFromRolloutLine(*line))
                return report;
        }
        if (start == 0 || window >= MaximumTailBytes)
            return std::nullopt;
        window = std::min(window * 4, MaximumTailBytes);
    }
}

QJsonArray windowsToJson(const QList<Window> &windows)
{
    QJsonArray array;
    for (const Window &w : windows) {
        QJsonObject object{{QStringLiteral("minutes"), w.minutes},
                           {QStringLiteral("usedPercent"), w.value.usedPercent}};
        if (w.value.resetsAt.isValid())
            object.insert(QStringLiteral("resetsAt"), double(w.value.resetsAt.toSecsSinceEpoch()));
        array.append(object);
    }
    return array;
}

QString readError(QProcess &process, const QByteArray &standardError)
{
    QString text = QString::fromUtf8(standardError).trimmed();
    if (text.isEmpty())
        text = process.errorString();
    return text.left(300);
}

} // namespace

QString defaultCodexHome()
{
    const QString overridden = qEnvironmentVariable("CODEX_HOME");
    if (!overridden.isEmpty())
        return QDir::cleanPath(overridden);
    return QDir::home().filePath(QStringLiteral(".codex"));
}

std::optional<Report> reportFromRolloutLine(const QByteArray &line)
{
    const QJsonObject event = QJsonDocument::fromJson(line).object();
    const QJsonObject payload = event.value(QStringLiteral("payload")).toObject();
    if (event.value(QStringLiteral("type")) != QLatin1String("event_msg")
        || payload.value(QStringLiteral("type")) != QLatin1String("token_count"))
        return std::nullopt;
    const QJsonObject limits = payload.value(QStringLiteral("rate_limits")).toObject();
    if (limits.isEmpty() || !isCodexBucket(limits.value(QStringLiteral("limit_id"))))
        return std::nullopt;
    const QDateTime observedAt = QDateTime::fromString(event.value(QStringLiteral("timestamp")).toString(),
                                                       Qt::ISODateWithMs);
    if (!observedAt.isValid())
        return std::nullopt;
    Report report;
    report.observedAt = observedAt;
    addWindow(report, rolloutWindow(limits.value(QStringLiteral("primary")), observedAt));
    addWindow(report, rolloutWindow(limits.value(QStringLiteral("secondary")), observedAt));
    if (report.windows.isEmpty())
        return std::nullopt; // carries no usage information
    report.planType = limits.value(QStringLiteral("plan_type")).toString().left(40);
    report.limitReachedType = limits.value(QStringLiteral("rate_limit_reached_type")).toString().left(80);
    return report;
}

SourceResult readLocalReport(const QString &codexHome)
{
    SourceResult result;
    for (const QFileInfo &file : newestRollouts(codexHome)) {
        QString error;
        std::optional<Report> report = newestInFile(file.filePath(), error);
        if (!error.isEmpty() && result.error.isEmpty())
            result.error = error;
        if (report && (!result.report || report->observedAt > result.report->observedAt))
            result.report = report;
        // Files are newest first: a report in a newer file is not older
        // than any report in the following files, unless clocks disagree;
        // stop at the first one found.
        if (result.report)
            break;
    }
    if (result.report)
        result.error.clear(); // an unreadable older file does not matter
    return result;
}

std::optional<Report> reportFromLiveResult(const QJsonObject &result, const QDateTime &now, QString *error)
{
    // Prefer the multi-bucket view's "codex" bucket; the single-bucket view
    // mirrors the historical payload.
    QJsonObject snapshot = result.value(QStringLiteral("rateLimitsByLimitId")).toObject()
                               .value(CodexLimitId).toObject();
    if (snapshot.isEmpty()) {
        const QJsonObject single = result.value(QStringLiteral("rateLimits")).toObject();
        if (isCodexBucket(single.value(QStringLiteral("limitId"))))
            snapshot = single;
    }
    if (snapshot.isEmpty()) {
        if (error)
            *error = QStringLiteral("Codex reported no usage limits for this account");
        return std::nullopt;
    }
    Report report;
    report.observedAt = now;
    addWindow(report, liveWindow(snapshot.value(QStringLiteral("primary")), now));
    addWindow(report, liveWindow(snapshot.value(QStringLiteral("secondary")), now));
    report.planType = snapshot.value(QStringLiteral("planType")).toString().left(40);
    report.limitReachedType = snapshot.value(QStringLiteral("rateLimitReachedType")).toString().left(80);
    const QJsonValue allowed = result.value(QStringLiteral("ordinaryUsageAllowed"));
    if (allowed.isBool())
        report.ordinaryUsageAllowed = allowed.toBool();
    if (report.windows.isEmpty()) {
        if (error)
            *error = QStringLiteral("Codex reported no usage windows for this account");
        return std::nullopt;
    }
    return report;
}

QString windowTitle(int minutes)
{
    if (minutes == 7 * 24 * 60)
        return QStringLiteral("Weekly limit");
    if (minutes == 24 * 60)
        return QStringLiteral("Daily limit");
    if (minutes % (24 * 60) == 0)
        return QStringLiteral("%1-day limit").arg(minutes / (24 * 60));
    if (minutes % 60 == 0)
        return QStringLiteral("%1h limit").arg(minutes / 60);
    return QStringLiteral("%1-minute limit").arg(minutes);
}

QString windowBarLabel(int minutes)
{
    if (minutes % (24 * 60) == 0)
        return QStringLiteral("cx%1d").arg(minutes / (24 * 60));
    if (minutes % 60 == 0)
        return QStringLiteral("cx%1h").arg(minutes / 60);
    return QStringLiteral("cx%1m").arg(minutes);
}

QString windowId(int minutes)
{
    return QStringLiteral("window_%1m").arg(minutes);
}

QString liveStateFile()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty() || !QDir::isAbsolutePath(runtime))
        runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return QDir(runtime).filePath(QStringLiteral("aqtruncat/codex-rate-limits.json"));
}

LiveState readLiveState(const QString &stateFile, const QString &codexHome)
{
    LiveState state;
    QFile file(stateFile);
    if (!file.open(QIODevice::ReadOnly))
        return state;
    const QJsonObject object = QJsonDocument::fromJson(file.read(64 * 1024)).object();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || QDir::cleanPath(object.value(QStringLiteral("codexHome")).toString()) != QDir::cleanPath(codexHome))
        return state; // another Codex home (or format): not this account's data
    auto time = [&](const char *key) {
        const auto ms = finiteNonNegative(object.value(QLatin1String(key)));
        return ms ? QDateTime::fromMSecsSinceEpoch(qint64(*ms), QTimeZone::UTC) : QDateTime();
    };
    state.capturedAt = time("capturedAtMs");
    state.lastAttemptAt = time("lastAttemptMs");
    state.error = object.value(QStringLiteral("error")).toString().left(300);
    if (state.capturedAt.isValid()) {
        Report report;
        report.observedAt = state.capturedAt;
        for (const QJsonValue &value : object.value(QStringLiteral("windows")).toArray()) {
            const QJsonObject w = value.toObject();
            const auto minutes = positiveMinutes(w.value(QStringLiteral("minutes")));
            const auto percent = finiteNonNegative(w.value(QStringLiteral("usedPercent")));
            if (!minutes || !percent)
                continue;
            Window window;
            window.minutes = *minutes;
            window.value.usedPercent = *percent;
            window.value.observedAt = state.capturedAt;
            if (const auto resets = finiteNonNegative(w.value(QStringLiteral("resetsAt"))))
                window.value.resetsAt = QDateTime::fromSecsSinceEpoch(qint64(*resets), QTimeZone::UTC);
            addWindow(report, window);
        }
        report.planType = object.value(QStringLiteral("planType")).toString().left(40);
        report.limitReachedType = object.value(QStringLiteral("limitReachedType")).toString().left(80);
        if (object.value(QStringLiteral("ordinaryUsageAllowed")).isBool())
            report.ordinaryUsageAllowed = object.value(QStringLiteral("ordinaryUsageAllowed")).toBool();
        if (!report.windows.isEmpty())
            state.report = report;
    }
    return state;
}

bool writeLiveState(const QString &stateFile, const QString &codexHome, const LiveState &state)
{
    // Only usage numbers, times and the Codex home: no account id, no
    // credential, no session data.
    QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("codexHome"), QDir::cleanPath(codexHome)}};
    if (state.lastAttemptAt.isValid())
        object.insert(QStringLiteral("lastAttemptMs"), double(state.lastAttemptAt.toMSecsSinceEpoch()));
    if (!state.error.isEmpty())
        object.insert(QStringLiteral("error"), state.error);
    if (state.report && state.capturedAt.isValid()) {
        object.insert(QStringLiteral("capturedAtMs"), double(state.capturedAt.toMSecsSinceEpoch()));
        object.insert(QStringLiteral("windows"), windowsToJson(state.report->windows));
        if (!state.report->planType.isEmpty())
            object.insert(QStringLiteral("planType"), state.report->planType);
        if (!state.report->limitReachedType.isEmpty())
            object.insert(QStringLiteral("limitReachedType"), state.report->limitReachedType);
        if (state.report->ordinaryUsageAllowed)
            object.insert(QStringLiteral("ordinaryUsageAllowed"), *state.report->ordinaryUsageAllowed);
    }
    const QString directory = QFileInfo(stateFile).absolutePath();
    if (!QDir().mkpath(directory))
        return false;
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(stateFile);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && file.write(bytes) == bytes.size() && file.commit();
}

SourceResult fetchLive(const QString &codexExecutable, const QString &codexHome, bool codexHomeOverridden,
                       const QDateTime &now, int timeoutMs)
{
    SourceResult result;
    if (codexExecutable.isEmpty()) {
        result.error = QStringLiteral("the codex executable was not found in PATH");
        return result;
    }
    QProcess process;
    if (codexHomeOverridden) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("CODEX_HOME"), codexHome);
        process.setProcessEnvironment(environment);
    }
    process.setProgram(codexExecutable);
    process.setArguments({QStringLiteral("app-server")});
    // Its own process group, so that a Codex that does not answer is
    // stopped together with anything it started.
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
    process.start();
    QElapsedTimer clock;
    clock.start();
    if (!process.waitForStarted(timeoutMs)) {
        result.error = QStringLiteral("cannot start codex app-server: %1").arg(process.errorString());
        return result;
    }
    auto send = [&](const QJsonObject &message) {
        process.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    };
    QByteArray buffer;
    QByteArray standardError;
    // Reads JSON lines until the response with this id (other messages,
    // e.g. notifications, are ignored) or the deadline.
    auto response = [&](int id) -> std::optional<QJsonObject> {
        for (;;) {
            const qsizetype newline = buffer.indexOf('\n');
            if (newline >= 0) {
                const QJsonObject message = QJsonDocument::fromJson(buffer.left(newline)).object();
                buffer.remove(0, newline + 1);
                if (message.value(QStringLiteral("id")).toInt(-1) == id)
                    return message;
                continue;
            }
            const qint64 remaining = timeoutMs - clock.elapsed();
            if (remaining <= 0 || buffer.size() > 4 * 1024 * 1024)
                return std::nullopt;
            if (!process.waitForReadyRead(int(remaining))) {
                if (process.state() != QProcess::Running)
                    return std::nullopt;
                continue;
            }
            buffer += process.readAllStandardOutput();
            standardError += process.readAllStandardError().left(4096);
        }
    };
    auto finish = [&] {
        // End of input is how the app-server is asked to exit.
        process.closeWriteChannel();
        const qint64 group = process.processId();
        if (!process.waitForFinished(2000)) {
            if (group > 0)
                ::kill(-pid_t(group), SIGTERM);
            if (!process.waitForFinished(1000)) {
                if (group > 0)
                    ::kill(-pid_t(group), SIGKILL);
                process.waitForFinished(1000);
            }
        }
        if (group > 0)
            ::kill(-pid_t(group), SIGKILL); // helpers it may have left behind
    };

    send({{QStringLiteral("id"), 1},
          {QStringLiteral("method"), QStringLiteral("initialize")},
          {QStringLiteral("params"), QJsonObject{{QStringLiteral("clientInfo"),
                                                  QJsonObject{{QStringLiteral("name"), QStringLiteral("aqtruncat")},
                                                              {QStringLiteral("title"), QStringLiteral("AQTRunCat")},
                                                              {QStringLiteral("version"), QStringLiteral("1")}}}}}});
    const std::optional<QJsonObject> initialized = response(1);
    if (!initialized || initialized->contains(QStringLiteral("error"))) {
        result.error = initialized ? QStringLiteral("codex app-server: %1").arg(
                                         initialized->value(QStringLiteral("error")).toObject()
                                             .value(QStringLiteral("message")).toString().left(300))
                                   : QStringLiteral("codex app-server did not answer: %1").arg(readError(process, standardError));
        finish();
        return result;
    }
    send({{QStringLiteral("method"), QStringLiteral("initialized")}});
    // excludeResetCreditDetails: the protocol's flag for background polls.
    send({{QStringLiteral("id"), 2},
          {QStringLiteral("method"), QStringLiteral("account/rateLimits/read")},
          {QStringLiteral("params"), QJsonObject{{QStringLiteral("excludeResetCreditDetails"), true}}}});
    const std::optional<QJsonObject> answer = response(2);
    finish();
    if (!answer) {
        result.error = QStringLiteral("codex app-server did not report usage in time");
        return result;
    }
    if (answer->contains(QStringLiteral("error"))) {
        result.error = QStringLiteral("Codex: %1").arg(answer->value(QStringLiteral("error")).toObject()
                                                           .value(QStringLiteral("message")).toString().left(300));
        return result;
    }
    QString error;
    result.report = reportFromLiveResult(answer->value(QStringLiteral("result")).toObject(), now, &error);
    result.error = error;
    return result;
}

namespace {

// Live data when due: a successful read every LiveRefreshSeconds, a failed
// one retried after LiveRetrySeconds; the previous data stays meanwhile.
LiveState refreshedLiveState(const Options &options)
{
    LiveState state = readLiveState(options.liveStateFile, options.codexHome);
    const bool due = !state.capturedAt.isValid() || state.capturedAt.secsTo(options.now) >= LiveRefreshSeconds;
    const bool mayRetry = !state.lastAttemptAt.isValid() || state.lastAttemptAt.secsTo(options.now) >= LiveRetrySeconds
        || state.lastAttemptAt > options.now;
    if (!due || !mayRetry)
        return state;
    state.lastAttemptAt = options.now;
    writeLiveState(options.liveStateFile, options.codexHome, state); // no parallel attempt meanwhile
    const SourceResult live = fetchLive(options.codexExecutable, options.codexHome, options.codexHomeOverridden,
                                        options.now);
    if (live.report) {
        state.report = live.report;
        state.capturedAt = live.report->observedAt;
        state.error.clear();
    } else {
        state.error = live.error;
    }
    writeLiveState(options.liveStateFile, options.codexHome, state);
    return state;
}

QString planText(const QString &plan)
{
    return plan.isEmpty() ? QString() : QStringLiteral("ChatGPT %1 plan").arg(plan);
}

} // namespace

Outcome collect(const Options &options)
{
    Outcome outcome;
    if (!QFileInfo(options.codexHome).isDir() && options.codexExecutable.isEmpty()) {
        outcome.exitCode = 69;
        outcome.diagnostic = QStringLiteral("Codex not found: no %1 and no codex executable in PATH").arg(options.codexHome);
        return outcome;
    }
    const SourceResult local = readLocalReport(options.codexHome);
    LiveState live;
    if (options.live && !options.liveStateFile.isEmpty())
        live = refreshedLiveState(options);

    // Windows by length, from both sources; the newer report of each wins.
    QList<int> lengths;
    auto collectLengths = [&](const std::optional<Report> &report) {
        if (!report)
            return;
        for (const Window &w : report->windows) {
            if (!lengths.contains(w.minutes))
                lengths.append(w.minutes);
        }
    };
    collectLengths(local.report);
    collectLengths(live.report);
    std::sort(lengths.begin(), lengths.end());

    const QString liveError = options.live ? live.error : QString();
    if (lengths.isEmpty()) {
        if (!liveError.isEmpty() || !local.error.isEmpty()) {
            outcome.exitCode = 1;
            outcome.diagnostic = !liveError.isEmpty() ? liveError : local.error;
            return outcome;
        }
        outcome.exitCode = 69;
        outcome.diagnostic = options.live
            ? QStringLiteral("No Codex usage report yet.")
            : QStringLiteral("No Codex usage report yet: use Codex once, or turn on live updates in AQTRunCat Settings.");
        return outcome;
    }

    auto windowOf = [](const std::optional<Report> &report, int minutes) -> std::optional<QuotaValue> {
        if (!report)
            return std::nullopt;
        for (const Window &w : report->windows) {
            if (w.minutes == minutes)
                return w.value;
        }
        return std::nullopt;
    };
    QJsonArray metrics;
    QDateTime updated;
    std::optional<QString> barValue; // the shortest current window
    bool usedLocal = false;
    bool usedLive = false;
    for (const int minutes : std::as_const(lengths)) {
        const std::optional<QuotaValue> fromLocal = windowOf(local.report, minutes);
        const std::optional<QuotaValue> fromLive = windowOf(live.report, minutes);
        const std::optional<QuotaValue> newest = aqtruncat::providers::newerQuota(fromLocal, fromLive);
        const auto judgement = aqtruncat::providers::judgeQuota(newest, options.now, FreshForSeconds,
                                                                MaximumReportAgeSeconds);
        const aqtruncat::providers::QuotaItemSpec spec{
            windowId(minutes), windowTitle(minutes), windowBarLabel(minutes),
            QStringLiteral("Use Codex, or turn on live updates in AQTRunCat Settings"), true};
        metrics.append(aqtruncat::providers::quotaItem(spec, judgement, options.now, options.timeZone));
        if (judgement.presence == aqtruncat::providers::QuotaPresence::Current) {
            const bool isLive = fromLive && newest->observedAt == fromLive->observedAt;
            (isLive ? usedLive : usedLocal) = true;
            if (!updated.isValid() || newest->observedAt > updated)
                updated = newest->observedAt;
            if (!barValue)
                barValue = aqtruncat::providers::quotaCompactText(newest->usedPercent);
        }
    }

    QStringList sources;
    if (usedLive)
        sources << QStringLiteral("live, Codex app-server");
    if (usedLocal)
        sources << QStringLiteral("Codex sessions");
    QJsonObject source{{QStringLiteral("id"), QStringLiteral("source")},
                       {QStringLiteral("title"), QStringLiteral("Usage data")},
                       {QStringLiteral("formattedValue"),
                        sources.isEmpty() ? QStringLiteral("No current report") : sources.join(QStringLiteral(" + "))}};
    // Plan and limit state of the newest report.
    const std::optional<Report> &newestReport =
        (live.report && (!local.report || live.report->observedAt >= local.report->observedAt)) ? live.report : local.report;
    QStringList details;
    bool allCurrent = true;
    for (const QJsonValue &value : std::as_const(metrics))
        allCurrent = allCurrent && value.toObject().contains(QStringLiteral("metricsBarValue"));
    // Without live updates an out-of-date window is expected: say why and
    // what changes it, rather than leaving only "Out of date".
    if (!options.live && !allCurrent) {
        source.insert(QStringLiteral("warning"), true);
        details << QStringLiteral("Live updates are off: values change only while you use Codex. "
                                  "Turn them on in AQTRunCat Settings › Integrations.");
    }
    if (newestReport) {
        if (!newestReport->planType.isEmpty())
            details << planText(newestReport->planType);
        if (!newestReport->limitReachedType.isEmpty())
            details << QStringLiteral("limit reached (%1)").arg(newestReport->limitReachedType);
        if (newestReport->ordinaryUsageAllowed == false)
            details << QStringLiteral("ordinary usage currently not allowed");
    }
    if (!liveError.isEmpty())
        details << QStringLiteral("live update failed: %1").arg(liveError);
    else if (!local.error.isEmpty())
        details << local.error;
    if (!details.isEmpty())
        source.insert(QStringLiteral("detail"), details.join(QStringLiteral(" · ")));
    metrics.append(source);

    const QJsonObject snapshot{
        {QStringLiteral("title"), QStringLiteral("Codex")},
        {QStringLiteral("metricsBarValue"), barValue.value_or(QStringLiteral("—"))},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("lastUpdatedDate"),
         (updated.isValid() ? updated : options.now).toUTC().toString(Qt::ISODateWithMs)},
    };
    outcome.standardOutput = QJsonDocument(snapshot).toJson(QJsonDocument::Compact) + '\n';
    return outcome;
}

} // namespace aqtruncat::codex
