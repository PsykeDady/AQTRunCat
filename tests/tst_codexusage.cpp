// F3 Codex adapter: rollout parsing, live (app-server) parsing, the cached
// live state and how both are combined per window. Synthetic Codex homes and
// a fake "codex" executable only: never the developer's ~/.codex, never
// the network, never a real Codex process.

#include "codex/codexusage.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace aqtruncat::codex;

namespace {

const QByteArray SecretPrompt = "PRIVATE-PROMPT-do-not-leak";
const QByteArray SecretAnswer = "PRIVATE-ANSWER-do-not-leak";
const QString AccountId = QStringLiteral("acct-PRIVATE-do-not-store");

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

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonValue window(double used, int minutes, const char *resetsAt)
{
    return QJsonObject{{QStringLiteral("used_percent"), used},
                       {QStringLiteral("window_minutes"), minutes},
                       {QStringLiteral("resets_at"), double(utc(resetsAt).toSecsSinceEpoch())}};
}

// A token_count event exactly as Codex 0.163 writes it in a rollout.
QByteArray tokenCount(const char *timestamp, const QJsonValue &primary, const QJsonValue &secondary,
                      const QJsonValue &limitId = QStringLiteral("codex"))
{
    return json(QJsonObject{
        {QStringLiteral("timestamp"), QString::fromLatin1(timestamp)},
        {QStringLiteral("type"), QStringLiteral("event_msg")},
        {QStringLiteral("payload"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("token_count")},
             {QStringLiteral("info"), QJsonObject{{QStringLiteral("model_context_window"), 258400}}},
             {QStringLiteral("rate_limits"), QJsonObject{
                  {QStringLiteral("limit_id"), limitId},
                  {QStringLiteral("limit_name"), QJsonValue()},
                  {QStringLiteral("primary"), primary},
                  {QStringLiteral("secondary"), secondary},
                  {QStringLiteral("credits"), QJsonObject{{QStringLiteral("has_credits"), false}}},
                  {QStringLiteral("plan_type"), QStringLiteral("plus")},
                  {QStringLiteral("rate_limit_reached_type"), QJsonValue()}}}}}});
}

QByteArray privateLines()
{
    return json(QJsonObject{{QStringLiteral("timestamp"), QStringLiteral("2026-10-20T08:00:00Z")},
                            {QStringLiteral("type"), QStringLiteral("response_item")},
                            {QStringLiteral("payload"), QJsonObject{{QStringLiteral("type"), QStringLiteral("message")},
                                                                    {QStringLiteral("content"), QString::fromLatin1(SecretPrompt)}}}})
        + '\n'
        + json(QJsonObject{{QStringLiteral("timestamp"), QStringLiteral("2026-10-20T08:00:01Z")},
                           {QStringLiteral("type"), QStringLiteral("response_item")},
                           {QStringLiteral("payload"), QJsonObject{{QStringLiteral("type"), QStringLiteral("message")},
                                                                   {QStringLiteral("content"), QString::fromLatin1(SecretAnswer)}}}});
}

void touch(const QString &path, const QDateTime &time)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
}

struct Fixture {
    QTemporaryDir temporary;
    QString home = temporary.filePath(QStringLiteral("codex home"));
    QString state = temporary.filePath(QStringLiteral("runtime/aqtruncat/codex-rate-limits.json"));

    Fixture()
    {
        QDir().mkpath(home);
        writeFile(home + QStringLiteral("/auth.json"), "{\"tokens\":{\"access_token\":\"sk-FAKE-do-not-leak\"}}");
    }

    QString rollout(const QString &day, const QString &name, const QList<QByteArray> &lines)
    {
        const QString path = home + QStringLiteral("/sessions/") + day + QStringLiteral("/rollout-") + name + QStringLiteral(".jsonl");
        QByteArray contents;
        for (const QByteArray &line : lines)
            contents += line + '\n';
        writeFile(path, contents);
        return path;
    }

    Outcome run(const char *now, bool live = false, const QString &executable = {}) const
    {
        Options options;
        options.codexHome = home;
        options.codexHomeOverridden = true;
        options.codexExecutable = executable;
        options.live = live;
        options.liveStateFile = state;
        options.now = utc(now);
        options.timeZone = QTimeZone(QByteArrayLiteral("Europe/Rome"));
        return collect(options);
    }

    // A fake "codex" whose app-server answers account/rateLimits/read with
    // the given result (or error), logging the methods it receives.
    QString fakeCodex(const QJsonObject &response, const char *extraShell = "")
    {
        const QString responseFile = temporary.filePath(QStringLiteral("response.json"));
        writeFile(responseFile, json(response));
        const QString log = temporary.filePath(QStringLiteral("codex.log"));
        const QString path = temporary.filePath(QStringLiteral("bin/codex"));
        writeFile(path, QStringLiteral(
            "#!/bin/sh\n"
            "echo \"args: $*\" >> '%1'\n"
            "%3\n"
            "while IFS= read -r line; do\n"
            "  echo \"$line\" | sed -n 's/.*\"method\":\"\\([^\"]*\\)\".*/method: \\1/p' >> '%1'\n"
            "  case \"$line\" in\n"
            "    *'\"initialize\"'*) echo '{\"id\":1,\"result\":{\"userAgent\":\"fake\"}}' ;;\n"
            "    *'account/rateLimits/read'*) printf '{\"method\":\"remoteControl/status/changed\",\"params\":{}}\\n'; cat '%2'; echo ;;\n"
            "  esac\n"
            "done\n").arg(log, responseFile, QLatin1String(extraShell)).toUtf8());
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return path;
    }

    QByteArray codexLog() const
    {
        QFile file(temporary.filePath(QStringLiteral("codex.log")));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

QJsonObject liveResult(int fiveHour, int weekly, const char *fiveReset, const char *weekReset)
{
    const QJsonObject snapshot{
        {QStringLiteral("limitId"), QStringLiteral("codex")},
        {QStringLiteral("primary"), QJsonObject{{QStringLiteral("usedPercent"), fiveHour},
                                                {QStringLiteral("windowDurationMins"), 300},
                                                {QStringLiteral("resetsAt"), double(utc(fiveReset).toSecsSinceEpoch())}}},
        {QStringLiteral("secondary"), QJsonObject{{QStringLiteral("usedPercent"), weekly},
                                                  {QStringLiteral("windowDurationMins"), 10080},
                                                  {QStringLiteral("resetsAt"), double(utc(weekReset).toSecsSinceEpoch())}}},
        {QStringLiteral("planType"), QStringLiteral("plus")},
        {QStringLiteral("rateLimitReachedType"), QJsonValue()}};
    return QJsonObject{{QStringLiteral("rateLimits"), snapshot},
                       {QStringLiteral("rateLimitsByLimitId"), QJsonObject{{QStringLiteral("codex"), snapshot}}},
                       {QStringLiteral("ordinaryUsageAllowed"), true},
                       {QStringLiteral("accountId"), AccountId}};
}

QJsonObject snapshotOf(const Outcome &outcome)
{
    if (outcome.exitCode != 0)
        qFatal("adapter exited %d: %s", outcome.exitCode, qPrintable(outcome.diagnostic));
    return QJsonDocument::fromJson(outcome.standardOutput).object();
}

QJsonObject item(const QJsonObject &snapshot, const QString &id)
{
    for (const QJsonValue &value : snapshot.value(QStringLiteral("metrics")).toArray()) {
        if (value.toObject().value(QStringLiteral("id")) == id)
            return value.toObject();
    }
    return {};
}

QString text(const QJsonObject &object, const char *field)
{
    return object.value(QLatin1String(field)).toString();
}

void verifyNoPrivateData(const Outcome &outcome)
{
    const QByteArray all = outcome.standardOutput + outcome.diagnostic.toUtf8();
    QVERIFY(!all.contains(SecretPrompt));
    QVERIFY(!all.contains(SecretAnswer));
    QVERIFY(!all.contains("sk-FAKE"));
    QVERIFY(!all.contains(AccountId.toUtf8()));
}

} // namespace

class TestCodexUsage : public QObject
{
    Q_OBJECT

private slots:
    void localSessionGivesBothWindowsWithLabelsFromTheirLength()
    {
        Fixture f;
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"), {
            privateLines(),
            tokenCount("2026-10-20T08:00:00.000Z", window(10, 300, "2026-10-20T12:00:00Z"), window(30, 10080, "2026-10-25T18:00:00Z")),
            privateLines(),
            tokenCount("2026-10-20T08:28:00.500Z", window(12.5, 300, "2026-10-20T12:00:00Z"), window(31, 10080, "2026-10-25T18:00:00Z")),
            privateLines()});
        const Outcome outcome = f.run("2026-10-20T08:30:00Z");
        const QJsonObject snapshot = snapshotOf(outcome);
        QCOMPARE(snapshot.value(QStringLiteral("title")).toString(), QStringLiteral("Codex"));

        const QJsonObject five = item(snapshot, QStringLiteral("window_300m"));
        QCOMPARE(text(five, "title"), QStringLiteral("5h limit"));
        QCOMPARE(text(five, "formattedValue"), QStringLiteral("12.5% used"));
        QCOMPARE(five.value(QStringLiteral("normalizedValue")).toDouble(), 0.125);
        // Remaining is derived from the same quota; reset in local time.
        QCOMPARE(text(five, "detail"), QStringLiteral("87.5% left · Resets at 14:00"));
        QCOMPARE(text(five, "metricsBarValue"), QStringLiteral("13%"));
        QCOMPARE(text(five, "metricsBarLabel"), QStringLiteral("cx5h"));

        const QJsonObject week = item(snapshot, QStringLiteral("window_10080m"));
        QCOMPARE(text(week, "title"), QStringLiteral("Weekly limit"));
        QCOMPARE(text(week, "formattedValue"), QStringLiteral("31% used"));
        QCOMPARE(text(week, "detail"), QStringLiteral("69% left · Resets Oct 25, 19:00"));
        QCOMPARE(text(week, "metricsBarLabel"), QStringLiteral("cx7d"));

        QCOMPARE(text(item(snapshot, QStringLiteral("source")), "formattedValue"), QStringLiteral("Codex sessions"));
        QCOMPARE(text(item(snapshot, QStringLiteral("source")), "detail"), QStringLiteral("ChatGPT plus plan"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("13%"));
        QCOMPARE(snapshot.value(QStringLiteral("lastUpdatedDate")).toString(), QStringLiteral("2026-10-20T08:28:00.500Z"));
        verifyNoPrivateData(outcome);
    }

    void onlyTheCodexBucketAndEventsWithWindowsCount()
    {
        Fixture f;
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"), {
            tokenCount("2026-10-20T08:00:00Z", window(10, 300, "2026-10-20T12:00:00Z"), QJsonValue()),
            // Another quota category, and a codex event without windows: neither replaces it.
            tokenCount("2026-10-20T08:10:00Z", window(99, 300, "2026-10-20T12:00:00Z"), QJsonValue(), QStringLiteral("premium")),
            tokenCount("2026-10-20T08:20:00Z", QJsonValue(), QJsonValue()),
            "{\"type\":\"event_msg\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"primary\":{\"used_perc"}); // being written
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:30:00Z"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("10% used"));
        QVERIFY(item(snapshot, QStringLiteral("window_10080m")).isEmpty()); // not reported: no item
    }

    void weeklyOnlyPlanIsNotMistakenForFiveHours()
    {
        Fixture f;
        // Some plans report a single window, as "primary", of 7 days.
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"),
                  {tokenCount("2026-10-20T08:00:00Z", window(40, 10080, "2026-10-25T18:00:00Z"), QJsonValue())});
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:01:00Z"));
        QVERIFY(item(snapshot, QStringLiteral("window_300m")).isEmpty());
        QCOMPARE(text(item(snapshot, QStringLiteral("window_10080m")), "title"), QStringLiteral("Weekly limit"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_10080m")), "formattedValue"), QStringLiteral("40% used"));
    }

    void olderFormatsAreUnderstood()
    {
        // limit_id absent and resets_in_seconds (relative to the event).
        const QByteArray line = json(QJsonObject{
            {QStringLiteral("timestamp"), QStringLiteral("2026-10-20T08:00:00Z")},
            {QStringLiteral("type"), QStringLiteral("event_msg")},
            {QStringLiteral("payload"), QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("token_count")},
                 {QStringLiteral("rate_limits"), QJsonObject{
                      {QStringLiteral("primary"), QJsonObject{{QStringLiteral("used_percent"), 5.0},
                                                              {QStringLiteral("window_minutes"), 300},
                                                              {QStringLiteral("resets_in_seconds"), 3600}}}}}}}});
        const std::optional<Report> report = reportFromRolloutLine(line);
        QVERIFY(report);
        QCOMPARE(report->windows.size(), 1);
        QCOMPARE(report->windows.first().value.resetsAt, utc("2026-10-20T09:00:00Z"));
        QVERIFY(!reportFromRolloutLine("not json"));
        QVERIFY(!reportFromRolloutLine(tokenCount("not a time", window(1, 300, "2026-10-20T09:00:00Z"), QJsonValue())));
    }

    void newestSessionWinsAlsoWhenAnOldOneIsResumed()
    {
        Fixture f;
        const QString older = f.rollout(QStringLiteral("2026/09/01"), QStringLiteral("resumed"),
                                        {tokenCount("2026-10-20T08:20:00Z", window(50, 300, "2026-10-20T12:00:00Z"), QJsonValue())});
        const QString newer = f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("today"),
                                        {tokenCount("2026-10-20T07:00:00Z", window(20, 300, "2026-10-20T12:00:00Z"), QJsonValue())});
        // The old-dated file was resumed and written last.
        touch(newer, utc("2026-10-20T07:00:00Z"));
        touch(older, utc("2026-10-20T08:20:00Z"));
        QCOMPARE(text(item(snapshotOf(f.run("2026-10-20T08:21:00Z")), QStringLiteral("window_300m")), "formattedValue"),
                 QStringLiteral("50% used"));
        // Archived sessions count as well.
        const QString archived = f.home + QStringLiteral("/archived_sessions/rollout-x.jsonl");
        writeFile(archived, tokenCount("2026-10-20T08:25:00Z", window(60, 300, "2026-10-20T12:00:00Z"), QJsonValue()) + '\n');
        touch(archived, utc("2026-10-20T08:25:00Z"));
        QCOMPARE(text(item(snapshotOf(f.run("2026-10-20T08:26:00Z")), QStringLiteral("window_300m")), "formattedValue"),
                 QStringLiteral("60% used"));
    }

    void freshnessResetAndAge()
    {
        Fixture f;
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"),
                  {tokenCount("2026-10-20T08:00:00Z", window(10, 300, "2026-10-20T12:00:00Z"), window(30, 10080, "2026-10-25T18:00:00Z"))});
        // Within 10 minutes: no "as of".
        QCOMPARE(text(item(snapshotOf(f.run("2026-10-20T08:09:00Z")), QStringLiteral("window_300m")), "detail"),
                 QStringLiteral("90% left · Resets at 14:00"));
        QCOMPARE(text(item(snapshotOf(f.run("2026-10-20T09:00:00Z")), QStringLiteral("window_300m")), "detail"),
                 QStringLiteral("90% left · Resets at 14:00 · as of 10:00"));
        // After the reset the old value is not shown (not as 0 either).
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T12:01:00Z"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("Out of date"));
        QVERIFY(item(snapshot, QStringLiteral("window_300m")).value(QStringLiteral("warning")).toBool());
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "detail"),
                 QStringLiteral("The window reset at 14:00; no newer report yet"));
        // Live updates off: the card says why and what to do.
        QVERIFY(item(snapshot, QStringLiteral("source")).value(QStringLiteral("warning")).toBool());
        QVERIFY(text(item(snapshot, QStringLiteral("source")), "detail")
                    .startsWith(QStringLiteral("Live updates are off: values change only while you use Codex")));
        QVERIFY(!item(snapshot, QStringLiteral("window_300m")).contains(QStringLiteral("metricsBarValue")));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("30%")); // the weekly one
        // A day later even the weekly report is too old.
        snapshot = snapshotOf(f.run("2026-10-21T08:01:00Z"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_10080m")), "formattedValue"), QStringLiteral("Out of date"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("—"));
        QCOMPARE(text(item(snapshot, QStringLiteral("source")), "formattedValue"), QStringLiteral("No current report"));
        // Current values: no warning anywhere.
        snapshot = snapshotOf(f.run("2026-10-20T08:05:00Z"));
        for (const QJsonValue &value : snapshot.value(QStringLiteral("metrics")).toArray())
            QVERIFY(!value.toObject().value(QStringLiteral("warning")).toBool());
    }

    void limitReachedIsExplicit()
    {
        Fixture f;
        QByteArray line = tokenCount("2026-10-20T08:00:00Z", window(100, 300, "2026-10-20T12:00:00Z"), window(16, 10080, "2026-10-25T18:00:00Z"));
        line.replace("\"rate_limit_reached_type\":null", "\"rate_limit_reached_type\":\"rate_limit_reached\"");
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"), {line});
        const QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:01:00Z"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("100% used — limit reached"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "detail"), QStringLiteral("0% left · Resets at 14:00"));
        QVERIFY(text(item(snapshot, QStringLiteral("source")), "detail").contains(QStringLiteral("limit reached (rate_limit_reached)")));
    }

    void absentNeverUsedAndUnreadable()
    {
        Options options;
        QTemporaryDir temporary;
        options.codexHome = temporary.filePath(QStringLiteral("none"));
        Outcome outcome = collect(options);
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("Codex not found")));

        Fixture f; // installed, never used
        outcome = f.run("2026-10-20T08:00:00Z");
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("use Codex once")));
        QVERIFY(outcome.standardOutput.isEmpty());

        if (::geteuid() != 0) {
            const QString path = f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"),
                                           {tokenCount("2026-10-20T08:00:00Z", window(1, 300, "2026-10-20T12:00:00Z"), QJsonValue())});
            QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));
            outcome = f.run("2026-10-20T08:00:00Z");
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QCOMPARE(outcome.exitCode, 1);
            QVERIFY(outcome.diagnostic.contains(QStringLiteral("cannot read a Codex session file")));
        }
    }

    void liveResultParsing()
    {
        QString error;
        std::optional<Report> report = reportFromLiveResult(liveResult(7, 20, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z"),
                                                            utc("2026-10-20T08:00:00Z"), &error);
        QVERIFY2(report, qPrintable(error));
        QCOMPARE(report->windows.size(), 2);
        QCOMPARE(report->windows.at(0).minutes, 300);
        QCOMPARE(report->windows.at(0).value.usedPercent, 7.0);
        QCOMPARE(report->windows.at(1).minutes, 10080);
        QCOMPARE(report->planType, QStringLiteral("plus"));
        QCOMPARE(*report->ordinaryUsageAllowed, true);

        // The multi-bucket "codex" entry wins over another single bucket.
        QJsonObject other = liveResult(7, 20, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z");
        QJsonObject single = other.value(QStringLiteral("rateLimits")).toObject();
        single.insert(QStringLiteral("limitId"), QStringLiteral("premium"));
        other.insert(QStringLiteral("rateLimits"), single);
        QCOMPARE(reportFromLiveResult(other, utc("2026-10-20T08:00:00Z"), &error)->windows.at(0).value.usedPercent, 7.0);
        other.remove(QStringLiteral("rateLimitsByLimitId"));
        QVERIFY(!reportFromLiveResult(other, utc("2026-10-20T08:00:00Z"), &error)); // only "premium": not this quota
        QVERIFY(error.contains(QStringLiteral("no usage limits")));

        QJsonObject empty = liveResult(7, 20, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z");
        QJsonObject noWindows{{QStringLiteral("limitId"), QStringLiteral("codex")}, {QStringLiteral("primary"), QJsonValue()}};
        empty.insert(QStringLiteral("rateLimitsByLimitId"), QJsonObject{{QStringLiteral("codex"), noWindows}});
        QVERIFY(!reportFromLiveResult(empty, utc("2026-10-20T08:00:00Z"), &error));
        QVERIFY(error.contains(QStringLiteral("no usage windows")));
    }

    void liveStateIsPrivateAndBoundToTheCodexHome()
    {
        Fixture f;
        LiveState state;
        state.report = reportFromLiveResult(liveResult(7, 20, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z"),
                                            utc("2026-10-20T08:00:00Z"), nullptr);
        state.capturedAt = utc("2026-10-20T08:00:00Z");
        state.lastAttemptAt = state.capturedAt;
        QVERIFY(writeLiveState(f.state, f.home, state));
        QFile file(f.state);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray stored = file.readAll();
        QVERIFY(!stored.contains(AccountId.toUtf8()));
        QCOMPARE(QFileInfo(f.state).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());

        const LiveState read = readLiveState(f.state, f.home);
        QVERIFY(read.report);
        QCOMPARE(read.report->windows.size(), 2);
        QCOMPARE(read.capturedAt, state.capturedAt);
        QCOMPARE(read.report->planType, QStringLiteral("plus"));
        // Data of another Codex home (another account setup) is not used.
        QVERIFY(!readLiveState(f.state, f.temporary.filePath(QStringLiteral("other"))).report);
    }

    void liveAndLocalNewestWinsPerWindow()
    {
        Fixture f;
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("a"),
                  {tokenCount("2026-10-20T08:00:00Z", window(10, 300, "2026-10-20T12:00:00Z"), window(30, 10080, "2026-10-25T18:00:00Z"))});
        LiveState state;
        state.report = reportFromLiveResult(liveResult(15, 31, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z"),
                                            utc("2026-10-20T08:05:00Z"), nullptr);
        state.capturedAt = utc("2026-10-20T08:05:00Z");
        state.lastAttemptAt = state.capturedAt;
        QVERIFY(writeLiveState(f.state, f.home, state));
        // Live is newer (and not yet due: no Codex is started).
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:06:00Z", true, QStringLiteral("/nonexistent/codex")));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("15% used"));
        QCOMPARE(text(item(snapshot, QStringLiteral("source")), "formattedValue"), QStringLiteral("live, Codex app-server"));
        // Live off: its cached data is not used at all.
        snapshot = snapshotOf(f.run("2026-10-20T08:06:00Z"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("10% used"));
        // A newer session event beats the cached live read.
        f.rollout(QStringLiteral("2026/10/20"), QStringLiteral("b"),
                  {tokenCount("2026-10-20T08:07:00Z", window(18, 300, "2026-10-20T12:00:00Z"), QJsonValue())});
        snapshot = snapshotOf(f.run("2026-10-20T08:08:00Z", true, QStringLiteral("/nonexistent/codex")));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("18% used"));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_10080m")), "formattedValue"), QStringLiteral("31% used"));
        QCOMPARE(text(item(snapshot, QStringLiteral("source")), "formattedValue"),
                 QStringLiteral("live, Codex app-server + Codex sessions"));
    }

    void liveReadThroughTheAppServerProtocol()
    {
        Fixture f;
        const QString codex = f.fakeCodex(QJsonObject{{QStringLiteral("id"), 2},
                                                      {QStringLiteral("result"), liveResult(4, 9, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z")}});
        const SourceResult live = fetchLive(codex, f.home, true, utc("2026-10-20T08:00:00Z"), 10000);
        QVERIFY2(live.report, qPrintable(live.error));
        QCOMPARE(live.report->windows.at(0).value.usedPercent, 4.0);
        // Only the app-server, and only the two read-only requests: no prompt, no thread.
        const QByteArray log = f.codexLog();
        QVERIFY(log.contains("args: app-server"));
        QCOMPARE(log.count("method: "), 3);
        QVERIFY(log.contains("method: initialize"));
        QVERIFY(log.contains("method: initialized"));
        QVERIFY(log.contains("method: account/rateLimits/read"));
    }

    void liveFailuresKeepTheLastValueAndRetryLater()
    {
        Fixture f;
        // First live read succeeds.
        QString codex = f.fakeCodex(QJsonObject{{QStringLiteral("id"), 2},
                                                {QStringLiteral("result"), liveResult(4, 9, "2026-10-20T12:00:00Z", "2026-10-25T18:00:00Z")}});
        QJsonObject snapshot = snapshotOf(f.run("2026-10-20T08:00:00Z", true, codex));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("4% used"));
        QCOMPARE(f.codexLog().count("args: app-server"), 1);
        // Not due yet: no new Codex process.
        snapshotOf(f.run("2026-10-20T08:04:00Z", true, codex));
        QCOMPARE(f.codexLog().count("args: app-server"), 1);
        // Due, but Codex reports an error (e.g. signed out): the last value stays, the error is shown.
        codex = f.fakeCodex(QJsonObject{{QStringLiteral("id"), 2},
                                        {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QStringLiteral("not logged in")}}}});
        snapshot = snapshotOf(f.run("2026-10-20T08:06:00Z", true, codex));
        QCOMPARE(text(item(snapshot, QStringLiteral("window_300m")), "formattedValue"), QStringLiteral("4% used"));
        QVERIFY2(text(item(snapshot, QStringLiteral("source")), "detail").contains(QStringLiteral("live update failed: Codex: not logged in")),
                 qPrintable(text(item(snapshot, QStringLiteral("source")), "detail")));
        QCOMPARE(f.codexLog().count("args: app-server"), 2);
        // A failed attempt is retried after a minute, not on every run.
        snapshotOf(f.run("2026-10-20T08:06:30Z", true, codex));
        QCOMPARE(f.codexLog().count("args: app-server"), 2);
        snapshotOf(f.run("2026-10-20T08:07:10Z", true, codex));
        QCOMPARE(f.codexLog().count("args: app-server"), 3);
    }

    void liveErrorsWithoutAnyDataAndTimeouts()
    {
        Fixture f;
        Outcome outcome = f.run("2026-10-20T08:00:00Z", true, QString());
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("not found in PATH")));

        Fixture slow;
        const QString helperPid = slow.temporary.filePath(QStringLiteral("helper.pid"));
        const QByteArray stuck = QStringLiteral("sleep 30 & echo $! > '%1'; wait").arg(helperPid).toUtf8();
        const QString codex = slow.fakeCodex(QJsonObject{}, stuck.constData());
        QElapsedTimer clock;
        clock.start();
        const SourceResult live = fetchLive(codex, slow.home, true, utc("2026-10-20T08:00:00Z"), 1500);
        QVERIFY(!live.report);
        QVERIFY(live.error.contains(QStringLiteral("did not answer")));
        QVERIFY(clock.elapsed() < 10000);
        // Nothing of the stuck Codex (here: a helper it started) is left running.
        QFile pidFile(helperPid);
        QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const QByteArray pid = pidFile.readAll().trimmed();
        QVERIFY(!pid.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(QStringLiteral("/proc/") + QString::fromLatin1(pid)), 3000);
    }

    void windowLabels()
    {
        QCOMPARE(windowTitle(300), QStringLiteral("5h limit"));
        QCOMPARE(windowTitle(10080), QStringLiteral("Weekly limit"));
        QCOMPARE(windowTitle(1440), QStringLiteral("Daily limit"));
        QCOMPARE(windowTitle(2880), QStringLiteral("2-day limit"));
        QCOMPARE(windowTitle(90), QStringLiteral("90-minute limit"));
        QCOMPARE(windowBarLabel(300), QStringLiteral("cx5h"));
        QCOMPARE(windowBarLabel(10080), QStringLiteral("cx7d"));
        QCOMPARE(windowBarLabel(90), QStringLiteral("cx90m"));
        QCOMPARE(windowId(300), QStringLiteral("window_300m"));
        qputenv("CODEX_HOME", "/some where/codex");
        QCOMPARE(defaultCodexHome(), QStringLiteral("/some where/codex"));
        qunsetenv("CODEX_HOME");
        QCOMPARE(defaultCodexHome(), QDir::home().filePath(QStringLiteral(".codex")));
    }
};

QTEST_GUILESS_MAIN(TestCodexUsage)
#include "tst_codexusage.moc"
