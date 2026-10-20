#include "claudeusage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <cmath>

namespace aqtruncat::claude {
namespace {

const QString FiveHourKey = QStringLiteral("five_hour");
const QString SevenDayKey = QStringLiteral("seven_day");

std::optional<double> finiteNonNegative(const QJsonValue &value)
{
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0)
        return std::nullopt;
    return number;
}

QByteArray readBounded(const QString &path, qint64 limit, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return {};
    }
    QByteArray bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return {};
    }
    if (bytes.size() > limit) {
        error = QStringLiteral("larger than %1 bytes").arg(limit);
        return {};
    }
    return bytes;
}

// cachedUsageUtilization.utilization.<window>: {"utilization": 3,
// "resets_at": "2026-10-20T11:30:00.431975+00:00", …}; null when the plan has
// no such window.
bool cacheWindow(const QJsonObject &utilization, const QString &key, const QDateTime &fetchedAt,
                 std::optional<QuotaReading> &out)
{
    const QJsonValue value = utilization.value(key);
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isObject())
        return false;
    const QJsonObject window = value.toObject();
    const std::optional<double> percent = finiteNonNegative(window.value(QStringLiteral("utilization")));
    if (!percent) {
        // A window object without a percentage is "not reported", not 0.
        return window.value(QStringLiteral("utilization")).isNull();
    }
    QuotaReading reading;
    reading.usedPercent = *percent;
    reading.observedAt = fetchedAt;
    reading.source = QuotaSource::UsageCache;
    const QJsonValue resets = window.value(QStringLiteral("resets_at"));
    if (resets.isString()) {
        reading.resetsAt = QDateTime::fromString(resets.toString(), Qt::ISODateWithMs);
        if (!reading.resetsAt.isValid())
            return false;
    } else if (!resets.isUndefined() && !resets.isNull()) {
        return false;
    }
    out = reading;
    return true;
}

QJsonObject readStateObject(const QString &stateFile)
{
    QString error;
    const QByteArray bytes = readBounded(stateFile, MaximumBridgeStateBytes, error);
    return QJsonDocument::fromJson(bytes).object();
}

} // namespace

QString globalConfigFile(const QString &configDirectory, bool configDirectoryOverridden)
{
    if (configDirectoryOverridden)
        return QDir(configDirectory).filePath(QStringLiteral(".claude.json"));
    return QDir::home().filePath(QStringLiteral(".claude.json"));
}

QuotaSourceResult readUsageCache(const QString &globalConfigFile)
{
    QuotaSourceResult result;
    if (!QFileInfo::exists(globalConfigFile))
        return result;
    // Claude Code rewrites the file in place; a read that catches it half
    // written is retried once before it counts as an error.
    QJsonObject root;
    for (int attempt = 0; attempt < 2; ++attempt) {
        QString readError;
        const QByteArray bytes = readBounded(globalConfigFile, MaximumGlobalConfigBytes, readError);
        if (!readError.isEmpty()) {
            result.error = QStringLiteral("cannot read %1: %2").arg(QFileInfo(globalConfigFile).fileName(), readError);
            return result;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            root = document.object();
            break;
        }
        if (attempt == 1) {
            result.error = QStringLiteral("%1 is not valid JSON").arg(QFileInfo(globalConfigFile).fileName());
            return result;
        }
        QThread::msleep(100);
    }

    const QJsonValue cacheValue = root.value(QStringLiteral("cachedUsageUtilization"));
    if (cacheValue.isUndefined() || cacheValue.isNull())
        return result; // /usage never fetched (or not a claude.ai account)
    const QJsonObject cache = cacheValue.toObject();
    const std::optional<double> fetchedMs = finiteNonNegative(cache.value(QStringLiteral("fetchedAtMs")));
    const QJsonValue utilizationValue = cache.value(QStringLiteral("utilization"));
    if (!cacheValue.isObject() || !fetchedMs || !utilizationValue.isObject()) {
        result.error = QStringLiteral("unsupported Claude Code usage cache format");
        return result;
    }
    const QString cacheAccount = cache.value(QStringLiteral("accountUuid")).toString();
    const QString account = root.value(QStringLiteral("oauthAccount")).toObject()
                                .value(QStringLiteral("accountUuid")).toString();
    if (account.isEmpty()) {
        result.note = QStringLiteral("Claude Code is not signed in with a Claude account");
        return result;
    }
    if (!cacheAccount.isEmpty() && cacheAccount != account) {
        result.note = QStringLiteral("the cached usage belongs to another Claude account");
        return result;
    }
    const QDateTime fetchedAt = QDateTime::fromMSecsSinceEpoch(qint64(*fetchedMs), QTimeZone::UTC);
    const QJsonObject utilization = utilizationValue.toObject();
    if (!cacheWindow(utilization, FiveHourKey, fetchedAt, result.fiveHour)
        || !cacheWindow(utilization, SevenDayKey, fetchedAt, result.sevenDay)) {
        result.fiveHour.reset();
        result.sevenDay.reset();
        result.error = QStringLiteral("unsupported Claude Code usage cache format");
        return result;
    }
    result.present = result.fiveHour || result.sevenDay;
    return result;
}

QString bridgeStateFile()
{
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty() || !QDir::isAbsolutePath(runtime))
        runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return QDir(runtime).filePath(QStringLiteral("aqtruncat/claude-rate-limits.json"));
}

QuotaSourceResult readBridgeState(const QString &stateFile, const QString &configDirectory)
{
    QuotaSourceResult result;
    if (!QFileInfo::exists(stateFile))
        return result;
    const QJsonObject state = readStateObject(stateFile);
    if (state.value(QStringLiteral("version")).toInt() != 1) {
        result.error = QStringLiteral("unsupported live usage state file");
        return result;
    }
    if (QDir::cleanPath(state.value(QStringLiteral("configDirectory")).toString()) != QDir::cleanPath(configDirectory)) {
        result.note = QStringLiteral("live usage was captured for another Claude Code directory");
        return result;
    }
    const QJsonObject windows = state.value(QStringLiteral("windows")).toObject();
    auto window = [&](const QString &key, std::optional<QuotaReading> &out) {
        const QJsonObject object = windows.value(key).toObject();
        const auto percent = finiteNonNegative(object.value(QStringLiteral("usedPercentage")));
        const auto captured = finiteNonNegative(object.value(QStringLiteral("capturedAtMs")));
        if (!percent || !captured)
            return;
        QuotaReading reading;
        reading.usedPercent = *percent;
        reading.observedAt = QDateTime::fromMSecsSinceEpoch(qint64(*captured), QTimeZone::UTC);
        reading.source = QuotaSource::StatusLine;
        if (const auto resets = finiteNonNegative(object.value(QStringLiteral("resetsAt"))))
            reading.resetsAt = QDateTime::fromSecsSinceEpoch(qint64(*resets), QTimeZone::UTC);
        out = reading;
    };
    window(FiveHourKey, result.fiveHour);
    window(SevenDayKey, result.sevenDay);
    result.present = result.fiveHour || result.sevenDay;
    return result;
}

bool captureStatusLine(const QByteArray &statusLineJson, const QString &stateFile,
                       const QString &configDirectory, const QDateTime &now, QString *reason)
{
    auto fail = [&](const QString &text) {
        if (reason)
            *reason = text;
        return false;
    };
    const QJsonObject input = QJsonDocument::fromJson(statusLineJson).object();
    const QJsonObject rateLimits = input.value(QStringLiteral("rate_limits")).toObject();
    QJsonObject captured;
    for (const QString &key : {FiveHourKey, SevenDayKey}) {
        const QJsonObject window = rateLimits.value(key).toObject();
        const auto percent = finiteNonNegative(window.value(QStringLiteral("used_percentage")));
        if (!percent)
            continue;
        QJsonObject entry{{QStringLiteral("usedPercentage"), *percent},
                          {QStringLiteral("capturedAtMs"), double(now.toMSecsSinceEpoch())}};
        if (const auto resets = finiteNonNegative(window.value(QStringLiteral("resets_at"))))
            entry.insert(QStringLiteral("resetsAt"), *resets);
        captured.insert(key, entry);
    }
    if (captured.isEmpty())
        return fail(QStringLiteral("no rate_limits in the status line input"));

    // Windows this session did not report keep their previous capture: the
    // reader judges each window by its own capture time and reset.
    QJsonObject state = readStateObject(stateFile);
    QJsonObject windows;
    if (state.value(QStringLiteral("version")).toInt() == 1
        && QDir::cleanPath(state.value(QStringLiteral("configDirectory")).toString()) == QDir::cleanPath(configDirectory))
        windows = state.value(QStringLiteral("windows")).toObject();
    for (auto it = captured.constBegin(); it != captured.constEnd(); ++it)
        windows.insert(it.key(), it.value());
    state = QJsonObject{{QStringLiteral("version"), 1},
                        {QStringLiteral("configDirectory"), QDir::cleanPath(configDirectory)},
                        {QStringLiteral("windows"), windows}};

    const QString directory = QFileInfo(stateFile).absolutePath();
    if (!QDir().mkpath(directory))
        return fail(QStringLiteral("cannot create %1").arg(directory));
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile file(stateFile);
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return fail(QStringLiteral("cannot write %1: %2").arg(stateFile, file.errorString()));
    return true;
}

QuotaChoice chooseQuota(QuotaWindow window, const QuotaSourceResult &cache, const QuotaSourceResult &bridge,
                        const QDateTime &now)
{
    QuotaChoice choice;
    const std::optional<QuotaReading> &a = cache.window(window);
    const std::optional<QuotaReading> &b = bridge.window(window);
    if (a && b)
        choice.reading = b->observedAt >= a->observedAt ? b : a;
    else
        choice.reading = a ? a : b;
    if (!choice.reading)
        return choice;
    const QuotaReading &reading = *choice.reading;
    const qint64 age = reading.observedAt.secsTo(now);
    if (reading.resetsAt.isValid() && reading.resetsAt <= now)
        choice.presence = QuotaPresence::Expired;
    else if (age > MaximumReportAgeSeconds)
        choice.presence = QuotaPresence::TooOld;
    else
        choice.presence = QuotaPresence::Current;
    choice.stale = choice.presence == QuotaPresence::Current && age > FreshForSeconds;
    return choice;
}

QString quotaPercentText(double percent)
{
    if (std::floor(percent) == percent)
        return QString::number(qint64(percent)) + u'%';
    return QString::number(percent, 'f', 1) + u'%';
}

QString quotaCompactText(double percent)
{
    if (percent > 0.0 && percent < 1.0)
        return QStringLiteral("<1%");
    return QString::number(qRound64(percent)) + u'%';
}

QString formatMoment(const QDateTime &moment, const QDateTime &now, const QTimeZone &zone)
{
    const QDateTime local = moment.toTimeZone(zone);
    const QString time = local.time().toString(QStringLiteral("HH:mm"));
    if (local.date() == now.toTimeZone(zone).date())
        return time;
    return QLocale::c().toString(local.date(), QStringLiteral("MMM d")) + QStringLiteral(", ") + time;
}

} // namespace aqtruncat::claude
