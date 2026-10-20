#include "claudecode.h"
#include "claudeusage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace aqtruncat::claude {
namespace {

std::optional<qint64> nonNegativeInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number || number > 9.0e15)
        return std::nullopt;
    return qint64(number);
}

QByteArray readSmallFile(const QString &path, qint64 limit, bool &ok)
{
    QFile file(path);
    ok = false;
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QByteArray bytes = file.read(limit + 1);
    ok = bytes.size() <= limit;
    return bytes;
}

bool insideDirectory(const QString &path, const QString &directory)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanDirectory = QDir::cleanPath(directory);
    return cleanPath == cleanDirectory || cleanPath.startsWith(cleanDirectory + u'/')
        || cleanDirectory == QLatin1String("/");
}

QString percentText(double percent)
{
    if (percent > 0.0 && percent < 0.1)
        return QStringLiteral("<0.1%");
    return QString::number(percent, 'f', 1) + u'%';
}

QString compactPercentText(double percent)
{
    if (percent > 0.0 && percent < 1.0)
        return QStringLiteral("<1%");
    return QString::number(qRound64(percent)) + u'%';
}

QJsonObject item(const QString &title, const QString &value,
                 std::optional<double> normalized = std::nullopt)
{
    QJsonObject object{{QStringLiteral("title"), title}, {QStringLiteral("formattedValue"), value}};
    if (normalized)
        object.insert(QStringLiteral("normalizedValue"), *normalized);
    return object;
}

QString sessionLabel(const LiveSession &session)
{
    QString project = QFileInfo(QDir::cleanPath(session.cwd)).fileName();
    if (project.isEmpty())
        project = session.cwd.isEmpty() ? QStringLiteral("unknown directory") : QStringLiteral("/");
    return session.status.isEmpty() ? project : project + QStringLiteral(" · ") + session.status;
}

} // namespace

QString defaultConfigDirectory()
{
    const QString overridden = qEnvironmentVariable("CLAUDE_CONFIG_DIR");
    if (!overridden.isEmpty())
        return QDir::cleanPath(overridden);
    return QDir::home().filePath(QStringLiteral(".claude"));
}

bool processMatches(const QString &procRoot, qint64 pid, const QString &procStart)
{
    if (pid <= 0)
        return false;
    const QString processDirectory = procRoot + u'/' + QString::number(pid);
    if (!procStart.isEmpty()) {
        bool ok = false;
        const QByteArray stat = readSmallFile(processDirectory + QStringLiteral("/stat"), 4096, ok);
        if (!ok)
            return false;
        // "pid (comm) state ppid …": comm may contain spaces and ')'.
        const qsizetype close = stat.lastIndexOf(')');
        if (close < 0)
            return false;
        const QList<QByteArray> fields = stat.mid(close + 1).simplified().split(' ');
        constexpr int StartTimeAfterComm = 22 - 3; // field 22, counted after pid and comm
        return fields.size() > StartTimeAfterComm
            && fields.at(StartTimeAfterComm) == procStart.toLatin1();
    }
    bool ok = false;
    const QByteArray commandLine = readSmallFile(processDirectory + QStringLiteral("/cmdline"), 64 * 1024, ok);
    return ok && commandLine.contains("claude");
}

RegistryScan scanRegistry(const QString &configDirectory, const QString &procRoot)
{
    RegistryScan scan;
    QDir sessions(configDirectory + QStringLiteral("/sessions"));
    static const QRegularExpression entryName(QStringLiteral("^[0-9]+\\.json$"));
    const QStringList names = sessions.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        if (!entryName.match(name).hasMatch())
            continue;
        if (scan.entries >= MaximumRegistryEntries)
            break;
        ++scan.entries;
        bool ok = false;
        const QByteArray bytes = readSmallFile(sessions.filePath(name), MaximumRegistryFileBytes, ok);
        const QJsonDocument document = ok ? QJsonDocument::fromJson(bytes) : QJsonDocument();
        const QJsonObject object = document.object();
        const std::optional<qint64> pid = nonNegativeInteger(object.value(QStringLiteral("pid")));
        const QString sessionId = object.value(QStringLiteral("sessionId")).toString();
        static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9-]{0,127}$"));
        if (!document.isObject() || !pid || !safeId.match(sessionId).hasMatch()) {
            ++scan.unsupported;
            continue;
        }
        const QJsonValue procStart = object.value(QStringLiteral("procStart"));
        if (!processMatches(procRoot, *pid, procStart.isString() ? procStart.toString() : QString()))
            continue; // stale entry of an exited (or crashed) session

        LiveSession session;
        session.pid = *pid;
        session.sessionId = sessionId;
        session.cwd = object.value(QStringLiteral("cwd")).toString();
        session.status = object.value(QStringLiteral("status")).toString().left(32);
        session.updatedAtMilliseconds = nonNegativeInteger(object.value(QStringLiteral("updatedAt"))).value_or(0);
        session.transcriptPath = findTranscript(configDirectory, sessionId);
        if (!session.transcriptPath.isEmpty())
            session.transcriptModified = QFileInfo(session.transcriptPath).lastModified(QTimeZone::UTC);
        scan.live.append(session);
    }
    return scan;
}

QString findTranscript(const QString &configDirectory, const QString &sessionId)
{
    const QDir projects(configDirectory + QStringLiteral("/projects"));
    const QString fileName = sessionId + QStringLiteral(".jsonl");
    QString best;
    QDateTime bestModified;
    for (const QString &project : projects.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QFileInfo candidate(projects.filePath(project) + u'/' + fileName);
        if (!candidate.isFile())
            continue;
        const QDateTime modified = candidate.lastModified(QTimeZone::UTC);
        if (best.isEmpty() || modified > bestModified) {
            best = candidate.absoluteFilePath();
            bestModified = modified;
        }
    }
    return best;
}

UsageResult readLatestUsage(const QString &transcriptPath, qint64 maximumBytes)
{
    UsageResult result;
    QFile file(transcriptPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.status = UsageStatus::Unreadable;
        result.error = QStringLiteral("cannot read the session transcript: %1").arg(file.errorString());
        return result;
    }
    const qint64 size = file.size();
    qint64 window = std::min(InitialTailBytes, maximumBytes);
    for (;;) {
        const qint64 start = std::max<qint64>(0, size - window);
        if (!file.seek(start)) {
            result.status = UsageStatus::Unreadable;
            result.error = QStringLiteral("cannot read the session transcript: %1").arg(file.errorString());
            return result;
        }
        const QByteArray tail = file.read(size - start);
        if (tail.size() != size - start) {
            result.status = UsageStatus::Unreadable;
            result.error = QStringLiteral("cannot read the session transcript: %1").arg(file.errorString());
            return result;
        }
        QList<QByteArray> lines = tail.split('\n');
        if (start > 0 && !lines.isEmpty())
            lines.removeFirst(); // partial line cut by the window

        bool compacted = false;
        for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
            const bool assistant = line->contains("\"type\":\"assistant\"");
            const bool boundary = line->contains("\"compact_boundary\"");
            if (!assistant && !boundary)
                continue;
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(*line, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
                continue; // the newest line may still be being written
            const QJsonObject entry = document.object();
            const QString type = entry.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("system")
                && entry.value(QStringLiteral("subtype")) == QLatin1String("compact_boundary")) {
                compacted = true;
                continue;
            }
            if (type != QLatin1String("assistant") || entry.value(QStringLiteral("isSidechain")).toBool())
                continue;
            const QJsonObject message = entry.value(QStringLiteral("message")).toObject();
            const QString model = message.value(QStringLiteral("model")).toString();
            if (model.isEmpty() || model.startsWith(u'<')) // "<synthetic>": local, not an API response
                continue;
            const QJsonValue usageValue = message.value(QStringLiteral("usage"));
            if (!usageValue.isObject())
                continue;
            const QJsonObject usage = usageValue.toObject();
            const auto input = nonNegativeInteger(usage.value(QStringLiteral("input_tokens")));
            const auto output = nonNegativeInteger(usage.value(QStringLiteral("output_tokens")));
            // Cache counters are optional in the Messages API: absent means
            // no cache use. Present but not a count is a format change.
            const QJsonValue creationValue = usage.value(QStringLiteral("cache_creation_input_tokens"));
            const QJsonValue readValue = usage.value(QStringLiteral("cache_read_input_tokens"));
            const auto creation = creationValue.isUndefined() || creationValue.isNull()
                ? std::optional<qint64>(0) : nonNegativeInteger(creationValue);
            const auto read = readValue.isUndefined() || readValue.isNull()
                ? std::optional<qint64>(0) : nonNegativeInteger(readValue);
            if (!input || !output || !creation || !read) {
                result.status = UsageStatus::Unsupported;
                result.error = QStringLiteral("unsupported transcript format: usage token counts missing or invalid");
                return result;
            }
            Usage found;
            found.inputTokens = *input;
            found.outputTokens = *output;
            found.cacheCreationInputTokens = *creation;
            found.cacheReadInputTokens = *read;
            found.model = model.left(128);
            found.requestedModel = entry.value(QStringLiteral("requestedModel")).toString().left(128);
            found.timestamp = QDateTime::fromString(entry.value(QStringLiteral("timestamp")).toString(),
                                                    Qt::ISODateWithMs);
            found.compactedSince = compacted;
            result.status = UsageStatus::Found;
            result.usage = found;
            return result;
        }
        if (start == 0) {
            result.status = UsageStatus::NoResponse;
            return result;
        }
        if (window >= maximumBytes) {
            result.status = UsageStatus::Unsupported;
            result.error = QStringLiteral("no response usage within the last %1 bytes of the transcript")
                               .arg(maximumBytes);
            return result;
        }
        window = std::min(window * 4, maximumBytes);
    }
}

std::optional<qint64> contextWindowFor(const QString &requestedModel)
{
    if (requestedModel.isEmpty())
        return std::nullopt;
    if (requestedModel.endsWith(QLatin1String("[1m]"), Qt::CaseInsensitive))
        return ExtendedContextWindow;
    return StandardContextWindow;
}

QString formatTokens(qint64 tokens)
{
    const QLocale c = QLocale::c();
    auto trimmed = [&](double value, int decimals) {
        QString text = c.toString(value, 'f', decimals);
        if (text.contains(u'.')) {
            while (text.endsWith(u'0'))
                text.chop(1);
            if (text.endsWith(u'.'))
                text.chop(1);
        }
        return text;
    };
    if (tokens >= 1000000)
        return trimmed(double(tokens) / 1.0e6, 2) + u'M';
    if (tokens >= 1000)
        return trimmed(double(tokens) / 1.0e3, 1) + u'k';
    return QString::number(tokens);
}

namespace {

// The context-window section (--context): secondary, so a missing session or
// an unreadable transcript is reported in its items instead of failing the
// whole snapshot.
void appendContext(QJsonArray &metrics, const Options &options)
{
    const RegistryScan scan = scanRegistry(options.configDirectory, options.procRoot);
    QList<LiveSession> candidates;
    for (const LiveSession &session : scan.live) {
        if (options.cwdFilter.isEmpty() || insideDirectory(session.cwd, options.cwdFilter))
            candidates.append(session);
    }
    auto contextItem = [](const QString &value, const QString &detail = {}) {
        QJsonObject object = item(QStringLiteral("Context window"), value);
        object.insert(QStringLiteral("id"), QStringLiteral("context"));
        if (!detail.isEmpty())
            object.insert(QStringLiteral("detail"), detail);
        return object;
    };
    if (candidates.isEmpty()) {
        if (scan.entries > 0 && scan.unsupported == scan.entries) {
            metrics.append(contextItem(QStringLiteral("Error"),
                                       QStringLiteral("unsupported Claude Code session registry format")));
        } else {
            metrics.append(contextItem(options.cwdFilter.isEmpty()
                                           ? QStringLiteral("No running session")
                                           : QStringLiteral("No running session in %1").arg(options.cwdFilter)));
        }
        return;
    }

    // Most recently active: newest transcript write, then registry update.
    // The registry does not say which terminal has focus, so this is
    // "most recently active", not necessarily "the one you are looking at".
    const LiveSession selected = *std::max_element(candidates.cbegin(), candidates.cend(),
        [](const LiveSession &a, const LiveSession &b) {
            const QDateTime aTime = a.transcriptModified.isValid() ? a.transcriptModified
                : QDateTime::fromMSecsSinceEpoch(a.updatedAtMilliseconds, QTimeZone::UTC);
            const QDateTime bTime = b.transcriptModified.isValid() ? b.transcriptModified
                : QDateTime::fromMSecsSinceEpoch(b.updatedAtMilliseconds, QTimeZone::UTC);
            if (aTime != bTime)
                return aTime < bTime;
            return a.pid < b.pid;
        });

    UsageResult usage;
    if (!selected.transcriptPath.isEmpty())
        usage = readLatestUsage(selected.transcriptPath);
    if (usage.status == UsageStatus::Unreadable || usage.status == UsageStatus::Unsupported) {
        metrics.append(contextItem(QStringLiteral("Error"), usage.error));
    } else if (usage.status == UsageStatus::Found) {
        const Usage &u = *usage.usage;
        const std::optional<qint64> window = contextWindowFor(u.requestedModel);
        if (u.compactedSince) {
            metrics.append(contextItem(QStringLiteral("Unknown until the next response after /compact")));
        } else if (window) {
            const double percent = 100.0 * double(u.contextTokens()) / double(*window);
            QJsonObject object = item(QStringLiteral("Context window"),
                                      QStringLiteral("%1 of %2 (%3 tokens)")
                                          .arg(percentText(percent), formatTokens(*window), formatTokens(u.contextTokens())),
                                      percent / 100.0);
            object.insert(QStringLiteral("id"), QStringLiteral("context"));
            object.insert(QStringLiteral("metricsBarValue"), compactPercentText(percent));
            object.insert(QStringLiteral("metricsBarLabel"), QStringLiteral("ctx"));
            if (u.timestamp.isValid())
                object.insert(QStringLiteral("detail"), QStringLiteral("Last response at %1")
                                                            .arg(formatMoment(u.timestamp, options.now, options.timeZone)));
            metrics.append(object);
        } else {
            metrics.append(contextItem(QStringLiteral("%1 tokens (window size unknown)").arg(formatTokens(u.contextTokens()))));
        }
        QString model = u.model;
        if (window == ExtendedContextWindow)
            model += QStringLiteral(" · 1M context");
        metrics.append(item(QStringLiteral("Model"), model));
    } else {
        metrics.append(contextItem(QStringLiteral("No response yet")));
    }
    metrics.append(item(QStringLiteral("Session"), sessionLabel(selected)));
    metrics.append(item(QStringLiteral("Running sessions"),
                        candidates.size() == 1 ? QStringLiteral("1")
                            : QStringLiteral("%1 (showing the most recently active)").arg(candidates.size())));
}

QString sourceName(QuotaSource source)
{
    return source == QuotaSource::StatusLine ? QStringLiteral("live, Claude Code status line")
                                             : QStringLiteral("Claude Code /usage");
}

QJsonObject quotaItem(const QString &id, const QString &title, const QString &barLabel, const QuotaChoice &choice,
                      const QString &missingHint, const Options &options)
{
    QJsonObject object{{QStringLiteral("id"), id}, {QStringLiteral("title"), title},
                       {QStringLiteral("metricsBarLabel"), barLabel}};
    const auto moment = [&](const QDateTime &time) { return formatMoment(time, options.now, options.timeZone); };
    switch (choice.presence) {
    case QuotaPresence::Current: {
        const QuotaReading &reading = *choice.reading;
        QString value = QStringLiteral("%1 used").arg(quotaPercentText(reading.usedPercent));
        if (reading.usedPercent >= 100.0)
            value += QStringLiteral(" — limit reached");
        QString detail;
        if (reading.resetsAt.isValid()) {
            const QString when = moment(reading.resetsAt);
            detail = when.contains(u',') ? QStringLiteral("Resets %1").arg(when) : QStringLiteral("Resets at %1").arg(when);
        } else {
            detail = QStringLiteral("Reset time not reported");
        }
        if (choice.stale)
            detail += QStringLiteral(" · as of %1").arg(moment(reading.observedAt));
        object.insert(QStringLiteral("formattedValue"), value);
        object.insert(QStringLiteral("normalizedValue"), reading.usedPercent / 100.0);
        object.insert(QStringLiteral("detail"), detail);
        object.insert(QStringLiteral("metricsBarValue"), quotaCompactText(reading.usedPercent));
        break;
    }
    case QuotaPresence::Expired:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Unknown"));
        object.insert(QStringLiteral("detail"), QStringLiteral("The window reset at %1; no newer report yet")
                                                    .arg(moment(choice.reading->resetsAt)));
        break;
    case QuotaPresence::TooOld:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Unknown"));
        object.insert(QStringLiteral("detail"), QStringLiteral("The last report (%1) is too old to show")
                                                    .arg(moment(choice.reading->observedAt)));
        break;
    case QuotaPresence::Missing:
        object.insert(QStringLiteral("formattedValue"), QStringLiteral("Not reported"));
        object.insert(QStringLiteral("detail"), missingHint);
        break;
    }
    return object;
}

} // namespace

Outcome collect(const Options &options)
{
    Outcome outcome;
    const bool installed = QFileInfo(options.configDirectory).isDir() || QFileInfo::exists(options.globalConfigFile);
    if (!installed) {
        outcome.exitCode = 69;
        outcome.diagnostic = QStringLiteral("Claude Code not found: neither %1 nor %2 exists")
                                 .arg(options.configDirectory, options.globalConfigFile);
        return outcome;
    }

    const QuotaSourceResult cache = readUsageCache(options.globalConfigFile);
    const QuotaSourceResult bridge = options.bridgeStateFile.isEmpty()
        ? QuotaSourceResult() : readBridgeState(options.bridgeStateFile, options.configDirectory);
    const QuotaChoice fiveHour = chooseQuota(QuotaWindow::FiveHour, cache, bridge, options.now);
    const QuotaChoice sevenDay = chooseQuota(QuotaWindow::SevenDay, cache, bridge, options.now);
    const QString sourceError = !cache.error.isEmpty() ? cache.error : bridge.error;

    if (!fiveHour.reading && !sevenDay.reading && !options.includeContext) {
        if (!sourceError.isEmpty()) {
            outcome.exitCode = 1;
            outcome.diagnostic = sourceError;
            return outcome;
        }
        outcome.exitCode = 69;
        outcome.diagnostic = QStringLiteral("No Claude Code usage report yet: open /usage in Claude Code once, "
                                            "or turn on live updates in AQTRunCat Settings.");
        if (!cache.note.isEmpty())
            outcome.diagnostic += QStringLiteral(" (%1)").arg(cache.note);
        return outcome;
    }

    QString missingHint = cache.note.isEmpty()
        ? QStringLiteral("Open /usage in Claude Code, or turn on live updates in AQTRunCat Settings")
        : cache.note;
    QJsonArray metrics;
    metrics.append(quotaItem(QStringLiteral("five_hour"), QStringLiteral("Current session (5h)"), QStringLiteral("5h"),
                             fiveHour, missingHint, options));
    metrics.append(quotaItem(QStringLiteral("seven_day"), QStringLiteral("Current week (all models)"), QStringLiteral("7d"),
                             sevenDay, missingHint, options));

    QStringList sources;
    for (const QuotaChoice *choice : {&fiveHour, &sevenDay}) {
        if (choice->presence == QuotaPresence::Current && !sources.contains(sourceName(choice->reading->source)))
            sources.append(sourceName(choice->reading->source));
    }
    QString sourceText = sources.isEmpty() ? QStringLiteral("No current report") : sources.join(QStringLiteral(" + "));
    QJsonObject sourceItem = item(QStringLiteral("Usage data"), sourceText);
    sourceItem.insert(QStringLiteral("id"), QStringLiteral("source"));
    if (!sourceError.isEmpty())
        sourceItem.insert(QStringLiteral("detail"), sourceError);
    metrics.append(sourceItem);

    if (options.includeContext)
        appendContext(metrics, options);

    QDateTime updated;
    for (const QuotaChoice *choice : {&fiveHour, &sevenDay}) {
        if (choice->presence == QuotaPresence::Current && (!updated.isValid() || choice->reading->observedAt > updated))
            updated = choice->reading->observedAt;
    }
    if (!updated.isValid())
        updated = options.now;
    const QString barValue = fiveHour.presence == QuotaPresence::Current
        ? quotaCompactText(fiveHour.reading->usedPercent) : QStringLiteral("—");

    const QJsonObject snapshot{
        {QStringLiteral("title"), QStringLiteral("Claude Code")},
        {QStringLiteral("metricsBarValue"), barValue},
        {QStringLiteral("metrics"), metrics},
        {QStringLiteral("lastUpdatedDate"), updated.toUTC().toString(Qt::ISODateWithMs)},
    };
    outcome.standardOutput = QJsonDocument(snapshot).toJson(QJsonDocument::Compact) + '\n';
    return outcome;
}

} // namespace aqtruncat::claude
