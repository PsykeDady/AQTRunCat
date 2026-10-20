// F2 usage quotas: the Claude Code /usage cache in .claude.json, the live
// status-line capture and how the two are combined into the 5-hour and 7-day
// items. Synthetic files only: never the developer's ~/.claude.json, never
// credentials, never the network.

#include "claude/claudecode.h"
#include "claude/claudeusage.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::claude;

namespace {

const QString Account = QStringLiteral("2064db21-0000-4748-b5a9-000000000001");
const QByteArray PrivateEmail = "alice.private@example.com";
const QByteArray SecretToken = "sk-ant-FAKE-TOKEN-do-not-leak";

QDateTime utc(const char *text)
{
    return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODateWithMs);
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

// One cache window exactly as Claude Code 2.1.304 writes it.
QJsonValue cacheWindow(const QJsonValue &percent, const QString &resetsAt)
{
    return QJsonObject{{QStringLiteral("utilization"), percent},
                       {QStringLiteral("resets_at"), resetsAt.isEmpty() ? QJsonValue() : QJsonValue(resetsAt)},
                       {QStringLiteral("limit_dollars"), QJsonValue()},
                       {QStringLiteral("used_dollars"), QJsonValue()},
                       {QStringLiteral("remaining_dollars"), QJsonValue()},
                       {QStringLiteral("locked_reason"), QJsonValue()}};
}

QJsonObject globalConfig(const QJsonValue &fiveHour, const QJsonValue &sevenDay,
                         qint64 fetchedAtMs = utc("2026-10-20T08:28:39.571Z").toMSecsSinceEpoch(),
                         const QString &cacheAccount = Account, const QString &account = Account)
{
    QJsonObject root{
        {QStringLiteral("numStartups"), 42},
        {QStringLiteral("projects"), QJsonObject{{QStringLiteral("/home/alice/secret-project"), QJsonObject{}}}},
        {QStringLiteral("cachedUsageUtilization"), QJsonObject{
             {QStringLiteral("fetchedAtMs"), double(fetchedAtMs)},
             {QStringLiteral("accountUuid"), cacheAccount},
             {QStringLiteral("utilization"), QJsonObject{
                  {QStringLiteral("five_hour"), fiveHour},
                  {QStringLiteral("seven_day"), sevenDay},
                  {QStringLiteral("seven_day_opus"), QJsonValue()},
                  {QStringLiteral("extra_usage"), QJsonObject{{QStringLiteral("is_enabled"), false}}}}}}},
    };
    if (!account.isEmpty()) {
        root.insert(QStringLiteral("oauthAccount"), QJsonObject{
            {QStringLiteral("accountUuid"), account},
            {QStringLiteral("emailAddress"), QString::fromLatin1(PrivateEmail)}});
    }
    return root;
}

QJsonObject typicalConfig()
{
    return globalConfig(cacheWindow(3, QStringLiteral("2026-10-20T11:30:00.431975+00:00")),
                        cacheWindow(7, QStringLiteral("2026-10-23T18:00:00.432000+00:00")));
}

QByteArray statusLineInput(const QJsonValue &fiveHour, const QJsonValue &sevenDay)
{
    QJsonObject rateLimits;
    if (!fiveHour.isUndefined())
        rateLimits.insert(QStringLiteral("five_hour"), fiveHour);
    if (!sevenDay.isUndefined())
        rateLimits.insert(QStringLiteral("seven_day"), sevenDay);
    QJsonObject input{
        {QStringLiteral("session_id"), QStringLiteral("private-session-id")},
        {QStringLiteral("transcript_path"), QStringLiteral("/home/alice/.claude/projects/x/private.jsonl")},
        {QStringLiteral("cwd"), QStringLiteral("/home/alice/secret-project")},
        {QStringLiteral("model"), QJsonObject{{QStringLiteral("id"), QStringLiteral("claude-opus-5-5")}}},
    };
    if (!rateLimits.isEmpty())
        input.insert(QStringLiteral("rate_limits"), rateLimits);
    return QJsonDocument(input).toJson(QJsonDocument::Compact);
}

QJsonObject liveWindow(double percent, const char *resetsAt)
{
    return QJsonObject{{QStringLiteral("used_percentage"), percent},
                       {QStringLiteral("resets_at"), double(utc(resetsAt).toSecsSinceEpoch())}};
}

struct Fixture {
    QTemporaryDir temporary;
    QString config = temporary.filePath(QStringLiteral("claude config"));
    QString global = temporary.filePath(QStringLiteral("home/.claude.json"));
    QString state = temporary.filePath(QStringLiteral("runtime/aqtruncat/claude-rate-limits.json"));

    Fixture()
    {
        QDir().mkpath(config);
        writeFile(config + QStringLiteral("/.credentials.json"), "{\"accessToken\":\"" + SecretToken + "\"}");
    }

    void cache(const QJsonObject &root) { writeFile(global, QJsonDocument(root).toJson(QJsonDocument::Indented)); }

    void live(const QByteArray &input, const char *at)
    {
        QString reason;
        QVERIFY2(captureStatusLine(input, state, config, utc(at), &reason), qPrintable(reason));
    }

    Outcome run(const char *now, bool liveUpdatesOn = false) const
    {
        Options options;
        options.liveUpdatesOn = liveUpdatesOn;
        options.configDirectory = config;
        options.globalConfigFile = global;
        options.bridgeStateFile = state;
        options.now = utc(now);
        options.timeZone = QTimeZone(QByteArrayLiteral("Europe/Rome"));
        return collect(options);
    }
};

QJsonObject snapshotOf(const Outcome &outcome)
{
    if (outcome.exitCode != 0)
        qFatal("adapter exited %d: %s", outcome.exitCode, qPrintable(outcome.diagnostic));
    return QJsonDocument::fromJson(outcome.standardOutput).object();
}

QJsonObject itemById(const QJsonObject &snapshot, const QString &id)
{
    for (const QJsonValue &value : snapshot.value(QStringLiteral("metrics")).toArray()) {
        if (value.toObject().value(QStringLiteral("id")) == id)
            return value.toObject();
    }
    return {};
}

QString text(const QJsonObject &item, const char *field)
{
    return item.value(QLatin1String(field)).toString();
}

} // namespace

class TestClaudeUsage : public QObject
{
    Q_OBJECT

private slots:
    void cacheGivesBothQuotasLikeUsageScreen()
    {
        Fixture f;
        f.cache(typicalConfig());
        const Outcome outcome = f.run("2026-10-20T08:30:00Z");
        const QJsonObject snapshot = snapshotOf(outcome);

        const QJsonObject session = itemById(snapshot, QStringLiteral("five_hour"));
        QCOMPARE(text(session, "title"), QStringLiteral("Current session (5h)"));
        QCOMPARE(text(session, "formattedValue"), QStringLiteral("3% used"));
        QCOMPARE(session.value(QStringLiteral("normalizedValue")).toDouble(), 0.03);
        QCOMPARE(text(session, "detail"), QStringLiteral("Resets at 13:30")); // 11:30 UTC in Rome (CEST)
        QCOMPARE(text(session, "metricsBarValue"), QStringLiteral("3%"));
        QCOMPARE(text(session, "metricsBarLabel"), QStringLiteral("5h"));

        const QJsonObject week = itemById(snapshot, QStringLiteral("seven_day"));
        QCOMPARE(text(week, "title"), QStringLiteral("Current week (all models)"));
        QCOMPARE(text(week, "formattedValue"), QStringLiteral("7% used"));
        QCOMPARE(week.value(QStringLiteral("normalizedValue")).toDouble(), 0.07);
        QCOMPARE(text(week, "detail"), QStringLiteral("Resets Oct 23, 20:00"));
        QCOMPARE(text(week, "metricsBarValue"), QStringLiteral("7%"));
        QCOMPARE(text(week, "metricsBarLabel"), QStringLiteral("7d"));

        QCOMPARE(text(itemById(snapshot, QStringLiteral("source")), "formattedValue"), QStringLiteral("Claude Code /usage"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("3%"));
        // "Last updated" is when Claude Code fetched the data, not when it was read.
        QCOMPARE(snapshot.value(QStringLiteral("lastUpdatedDate")).toString(), QStringLiteral("2026-10-20T08:28:39.571Z"));
        // No context-window item unless asked for.
        QVERIFY(itemById(snapshot, QStringLiteral("context")).isEmpty());

        const QByteArray all = outcome.standardOutput + outcome.diagnostic.toUtf8();
        QVERIFY(!all.contains(PrivateEmail));
        QVERIFY(!all.contains(SecretToken));
        QVERIFY(!all.contains("secret-project"));
        QVERIFY(!all.contains(Account.toUtf8()));
    }

    void olderReportsSayAsOfWhen()
    {
        Fixture f;
        f.cache(typicalConfig());
        // Still fresh 5 minutes after the fetch, then marked with its time.
        QCOMPARE(text(itemById(snapshotOf(f.run("2026-10-20T08:33:39Z")), QStringLiteral("five_hour")), "detail"),
                 QStringLiteral("Resets at 13:30"));
        const QJsonObject later = snapshotOf(f.run("2026-10-20T10:00:00Z"));
        QCOMPARE(text(itemById(later, QStringLiteral("five_hour")), "detail"), QStringLiteral("Resets at 13:30 · as of 10:28"));
        QCOMPARE(text(itemById(later, QStringLiteral("seven_day")), "detail"),
                 QStringLiteral("Resets Oct 23, 20:00 · as of 10:28"));
        QCOMPARE(text(itemById(later, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("3% used"));
        // The next day the "as of" carries its date.
        QCOMPARE(text(itemById(snapshotOf(f.run("2026-10-21T07:00:00Z")), QStringLiteral("seven_day")), "detail"),
                 QStringLiteral("Resets Oct 23, 20:00 · as of Oct 20, 10:28"));
    }

    void reportFromBeforeTheResetIsNotShownAsCurrent()
    {
        Fixture f;
        f.cache(typicalConfig());
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-20T11:31:00Z"));
        const QJsonObject session = itemById(snapshot, QStringLiteral("five_hour"));
        QCOMPARE(text(session, "formattedValue"), QStringLiteral("Out of date"));
        QCOMPARE(text(session, "detail"), QStringLiteral("The window reset at 13:30; no newer report yet"));
        QVERIFY(session.value(QStringLiteral("warning")).toBool());
        QVERIFY(!session.contains(QStringLiteral("normalizedValue")));
        QVERIFY(!session.contains(QStringLiteral("metricsBarValue")));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("—"));
        const QJsonObject week = itemById(snapshot, QStringLiteral("seven_day"));
        QCOMPARE(text(week, "formattedValue"), QStringLiteral("7% used"));
        QVERIFY(!week.value(QStringLiteral("warning")).toBool());
        // Live updates off: the card says why and what to do.
        const QJsonObject source = itemById(snapshot, QStringLiteral("source"));
        QVERIFY(source.value(QStringLiteral("warning")).toBool());
        QVERIFY(text(source, "detail").startsWith(QStringLiteral("Live updates are off: values change only when you open /usage")));
        QVERIFY(text(source, "detail").contains(QStringLiteral("Settings › Integrations")));
        // With live updates on, only the window itself warns.
        const QJsonObject live = itemById(snapshotOf(f.run("2026-10-20T11:31:00Z", true)), QStringLiteral("source"));
        QVERIFY(!live.value(QStringLiteral("warning")).toBool());
        QVERIFY(!text(live, "detail").contains(QStringLiteral("Live updates are off")));
        // Everything current: no warning at all.
        const QJsonObject fresh = snapshotOf(f.run("2026-10-20T08:30:00Z"));
        for (const QJsonValue &value : fresh.value(QStringLiteral("metrics")).toArray())
            QVERIFY(!value.toObject().value(QStringLiteral("warning")).toBool());
    }

    void weeklyReportExpiresAfterADay()
    {
        Fixture f;
        f.cache(typicalConfig());
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-21T08:29:00Z")); // 24 h and 20 s later
        const QJsonObject week = itemById(snapshot, QStringLiteral("seven_day"));
        QCOMPARE(text(week, "formattedValue"), QStringLiteral("Out of date"));
        QVERIFY(week.value(QStringLiteral("warning")).toBool());
        QCOMPARE(text(week, "detail"), QStringLiteral("The last report (Oct 20, 10:28) is too old to show"));
        QVERIFY(!week.contains(QStringLiteral("metricsBarValue")));
    }

    void partialAndMissingQuotas()
    {
        Fixture f;
        // The plan reports only the weekly window.
        f.cache(globalConfig(QJsonValue(), cacheWindow(41.2, QStringLiteral("2026-10-23T18:00:00.432000+00:00"))));
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:30:00Z"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("Not reported"));
        QVERIFY(!itemById(snapshot, QStringLiteral("five_hour")).contains(QStringLiteral("normalizedValue")));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "formattedValue"), QStringLiteral("41.2% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "metricsBarValue"), QStringLiteral("41%"));

        // A window object whose percentage is null is "not reported", not 0.
        f.cache(globalConfig(cacheWindow(QJsonValue(), QString()), cacheWindow(5, QString())));
        snapshot = snapshotOf(f.run("2026-10-20T08:30:00Z"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("Not reported"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "detail"), QStringLiteral("Reset time not reported"));

        // Nothing at all: Unavailable, with what to do about it.
        QJsonObject none = typicalConfig();
        none.remove(QStringLiteral("cachedUsageUtilization"));
        f.cache(none);
        const Outcome outcome = f.run("2026-10-20T08:30:00Z");
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.standardOutput.isEmpty());
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("open /usage")));
    }

    void claudeCodeAbsentIsUnavailable()
    {
        QTemporaryDir temporary;
        Options options;
        options.configDirectory = temporary.filePath(QStringLiteral("none"));
        options.globalConfigFile = temporary.filePath(QStringLiteral("none.json"));
        const Outcome outcome = collect(options);
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("Claude Code not found")));
    }

    void cacheOfAnotherAccountOrSignedOutIsIgnored()
    {
        Fixture f;
        const QJsonObject other = globalConfig(cacheWindow(3, QStringLiteral("2026-10-20T11:30:00Z")), cacheWindow(7, QString()),
                             utc("2026-10-20T08:28:39Z").toMSecsSinceEpoch(), QStringLiteral("someone-else"));
        f.cache(other);
        Outcome outcome = f.run("2026-10-20T08:30:00Z");
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("another Claude account")));

        f.cache(globalConfig(cacheWindow(3, QString()), cacheWindow(7, QString()),
                             utc("2026-10-20T08:28:39Z").toMSecsSinceEpoch(), Account, QString()));
        outcome = f.run("2026-10-20T08:30:00Z");
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("not signed in")));
    }

    void unsupportedOrUnreadableCacheIsError_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QJsonObject stringPercent = globalConfig(cacheWindow(QStringLiteral("3%"), QString()), QJsonValue());
        QJsonObject badDate = globalConfig(cacheWindow(3, QStringLiteral("tomorrow")), QJsonValue());
        QJsonObject noFetch = typicalConfig();
        QJsonObject cache = noFetch.value(QStringLiteral("cachedUsageUtilization")).toObject();
        cache.remove(QStringLiteral("fetchedAtMs"));
        noFetch.insert(QStringLiteral("cachedUsageUtilization"), cache);
        QTest::newRow("percentage as text") << QJsonDocument(stringPercent).toJson();
        QTest::newRow("unparsable reset") << QJsonDocument(badDate).toJson();
        QTest::newRow("no fetch time") << QJsonDocument(noFetch).toJson();
        QTest::newRow("not JSON") << QByteArray("{\"cachedUsageUtilization\": {");
    }

    void unsupportedOrUnreadableCacheIsError()
    {
        QFETCH(QByteArray, contents);
        Fixture f;
        writeFile(f.global, contents);
        const Outcome outcome = f.run("2026-10-20T08:30:00Z");
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY(outcome.standardOutput.isEmpty());
        QVERIFY(!outcome.diagnostic.isEmpty());
        QVERIFY(!outcome.diagnostic.toUtf8().contains(PrivateEmail));
    }

    void limitReachedAndBeyond()
    {
        Fixture f;
        f.cache(globalConfig(cacheWindow(100, QStringLiteral("2026-10-20T11:30:00Z")),
                             cacheWindow(104, QStringLiteral("2026-10-23T18:00:00Z"))));
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:30:00Z"));
        const QJsonObject session = itemById(snapshot, QStringLiteral("five_hour"));
        QCOMPARE(text(session, "formattedValue"), QStringLiteral("100% used — limit reached"));
        QCOMPARE(session.value(QStringLiteral("normalizedValue")).toDouble(), 1.0);
        QCOMPARE(text(session, "metricsBarValue"), QStringLiteral("100%"));
        const QJsonObject week = itemById(snapshot, QStringLiteral("seven_day"));
        QCOMPARE(text(week, "formattedValue"), QStringLiteral("104% used — limit reached"));
        QCOMPARE(text(week, "metricsBarValue"), QStringLiteral("104%"));
    }

    void liveStatusLineAndCacheNewestWins()
    {
        Fixture f;
        f.cache(typicalConfig()); // fetched 08:28:39
        f.live(statusLineInput(liveWindow(23.5, "2026-10-20T11:30:00Z"), QJsonValue::Undefined), "2026-10-20T09:00:00Z");
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T09:01:00Z"));
        // 5h from the newer live report, 7d still from the cache.
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("23.5% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "metricsBarValue"), QStringLiteral("24%"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "formattedValue"), QStringLiteral("7% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("source")), "formattedValue"),
                 QStringLiteral("live, Claude Code status line + Claude Code /usage"));
        QCOMPARE(snapshot.value(QStringLiteral("lastUpdatedDate")).toString(), QStringLiteral("2026-10-20T09:00:00.000Z"));

        // /usage opened afterwards: the cache is newer again.
        QJsonObject newer = globalConfig(cacheWindow(25, QStringLiteral("2026-10-20T11:30:00Z")),
                                         cacheWindow(8, QStringLiteral("2026-10-23T18:00:00Z")),
                                         utc("2026-10-20T09:05:00Z").toMSecsSinceEpoch());
        f.cache(newer);
        snapshot = snapshotOf(f.run("2026-10-20T09:06:00Z"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("25% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "formattedValue"), QStringLiteral("8% used"));
    }

    void liveOnlyWithoutCacheAndResetTimeChanges()
    {
        Fixture f; // no .claude.json at all, only live reports
        f.live(statusLineInput(liveWindow(0.4, "2026-10-20T11:30:00Z"), liveWindow(12, "2026-10-23T18:00:00Z")),
               "2026-10-20T09:00:00Z");
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T09:00:30Z"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("0.4% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "metricsBarValue"), QStringLiteral("<1%"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("source")), "formattedValue"),
                 QStringLiteral("live, Claude Code status line"));

        // After the reset the next response reports a new window.
        f.live(statusLineInput(liveWindow(2, "2026-10-20T16:30:00Z"), QJsonValue::Undefined), "2026-10-20T11:45:00Z");
        snapshot = snapshotOf(f.run("2026-10-20T11:45:10Z"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "formattedValue"), QStringLiteral("2% used"));
        QCOMPARE(text(itemById(snapshot, QStringLiteral("five_hour")), "detail"), QStringLiteral("Resets at 18:30"));
        // The weekly window kept its own capture time.
        QCOMPARE(text(itemById(snapshot, QStringLiteral("seven_day")), "detail"),
                 QStringLiteral("Resets Oct 23, 20:00 · as of 11:00"));
    }

    void captureStoresOnlyTheTwoWindowsPrivately()
    {
        Fixture f;
        QString reason;
        QVERIFY(!captureStatusLine(statusLineInput(QJsonValue::Undefined, QJsonValue::Undefined), f.state, f.config,
                                   utc("2026-10-20T09:00:00Z"), &reason));
        QVERIFY(reason.contains(QStringLiteral("rate_limits")));
        QVERIFY(!QFileInfo::exists(f.state)); // no report: nothing written
        QVERIFY(!captureStatusLine("not json", f.state, f.config, utc("2026-10-20T09:00:00Z")));

        f.live(statusLineInput(liveWindow(3, "2026-10-20T11:30:00Z"), liveWindow(7, "2026-10-23T18:00:00Z")),
               "2026-10-20T09:00:00Z");
        QFile file(f.state);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray stored = file.readAll();
        QVERIFY(!stored.contains("private"));
        QVERIFY(!stored.contains("secret-project"));
        QVERIFY(!stored.contains("claude-opus"));
        QCOMPARE(QFileInfo(f.state).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
        QCOMPARE(QFileInfo(QFileInfo(f.state).absolutePath()).permissions()
                     & (QFileDevice::ExeGroup | QFileDevice::ExeOther), QFileDevice::Permissions());

        // A session before its first response (no rate_limits) keeps them.
        QVERIFY(!captureStatusLine(statusLineInput(QJsonValue::Undefined, QJsonValue::Undefined), f.state, f.config,
                                   utc("2026-10-20T09:10:00Z")));
        QCOMPARE(readBridgeState(f.state, f.config).fiveHour->usedPercent, 3.0);
    }

    void liveStateOfAnotherClaudeDirectoryIsIgnored()
    {
        Fixture f;
        QVERIFY(captureStatusLine(statusLineInput(liveWindow(50, "2026-10-20T11:30:00Z"), QJsonValue::Undefined),
                                  f.state, QStringLiteral("/other/claude"), utc("2026-10-20T09:00:00Z")));
        const QuotaSourceResult result = readBridgeState(f.state, f.config);
        QVERIFY(!result.present);
        QVERIFY(result.note.contains(QStringLiteral("another Claude Code directory")));
    }

    void formattingHelpers()
    {
        QCOMPARE(quotaPercentText(3), QStringLiteral("3%"));
        QCOMPARE(quotaPercentText(0), QStringLiteral("0%"));
        QCOMPARE(quotaPercentText(23.5), QStringLiteral("23.5%"));
        QCOMPARE(quotaCompactText(0), QStringLiteral("0%"));
        QCOMPARE(quotaCompactText(0.4), QStringLiteral("<1%"));
        QCOMPARE(quotaCompactText(99.6), QStringLiteral("100%"));
        const QTimeZone rome(QByteArrayLiteral("Europe/Rome"));
        QCOMPARE(formatMoment(utc("2026-10-20T11:30:00Z"), utc("2026-10-20T08:00:00Z"), rome), QStringLiteral("13:30"));
        QCOMPARE(formatMoment(utc("2026-10-20T22:30:00Z"), utc("2026-10-20T08:00:00Z"), rome), QStringLiteral("Oct 21, 00:30"));
        QCOMPARE(formatMoment(utc("2026-10-20T11:30:00Z"), utc("2026-10-20T08:00:00Z"), QTimeZone::UTC),
                 QStringLiteral("11:30"));
        QCOMPARE(globalConfigFile(QStringLiteral("/custom/dir"), true), QStringLiteral("/custom/dir/.claude.json"));
        QCOMPARE(globalConfigFile(QStringLiteral("/home/x/.claude"), false), QDir::home().filePath(QStringLiteral(".claude.json")));
    }
};

QTEST_GUILESS_MAIN(TestClaudeUsage)
#include "tst_claudeusage.moc"
