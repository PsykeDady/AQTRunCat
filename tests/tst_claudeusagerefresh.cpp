// 2.3.4 automatic /usage refresh of the Claude Code adapter: when it runs
// `claude -p /usage`, how the result is judged, the stop condition, retry and
// click spacing, and the private state file. A fake "claude" script only:
// never the developer's ~/.claude.json, never a real Claude Code, never the
// network.

#include "claude/claudecode.h"
#include "claude/claudeusage.h"
#include "claude/usagerefresh.h"

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

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// .claude.json with a /usage cache as Claude Code 2.1.316 writes it.
QByteArray globalConfig(int fiveHour, int sevenDay, const char *fetchedAt)
{
    auto window = [](int percent, const char *resetsAt) {
        return QJsonObject{{QStringLiteral("utilization"), percent},
                           {QStringLiteral("resets_at"), QString::fromLatin1(resetsAt)}};
    };
    return QJsonDocument(QJsonObject{
        {QStringLiteral("oauthAccount"), QJsonObject{{QStringLiteral("accountUuid"), Account}}},
        {QStringLiteral("cachedUsageUtilization"), QJsonObject{
             {QStringLiteral("fetchedAtMs"), double(utc(fetchedAt).toMSecsSinceEpoch())},
             {QStringLiteral("accountUuid"), Account},
             {QStringLiteral("utilization"), QJsonObject{
                  {QStringLiteral("five_hour"), window(fiveHour, "2026-11-06T12:50:00.789930+00:00")},
                  {QStringLiteral("seven_day"), window(sevenDay, "2026-11-06T18:00:00.789950+00:00")}}}}},
    }).toJson(QJsonDocument::Compact);
}

// The JSON result of `claude -p /usage --output-format json`, shaped as the
// one Claude Code 2.1.316 printed on 2026-11-06 (text shortened).
QByteArray envelope(int outputTokens = 0, int turns = 0, const QString &localCommand = QStringLiteral("usage"),
                    double cost = 0)
{
    QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("result")},
        {QStringLiteral("subtype"), QStringLiteral("success")},
        {QStringLiteral("is_error"), false},
        {QStringLiteral("duration_api_ms"), 0},
        {QStringLiteral("num_turns"), turns},
        {QStringLiteral("total_cost_usd"), cost},
        {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 0},
                                              {QStringLiteral("cache_creation_input_tokens"), 0},
                                              {QStringLiteral("cache_read_input_tokens"), 0},
                                              {QStringLiteral("output_tokens"), outputTokens}}},
        {QStringLiteral("modelUsage"), QJsonObject{}},
        {QStringLiteral("result"), QStringLiteral("Current session: 6% used · resets Nov 6, 1:50pm (Europe/Rome)")},
        {QStringLiteral("session_id"), QStringLiteral("990fd6d2-99c5-4919-9765-44f6a044e977")},
    };
    if (!localCommand.isEmpty())
        object.insert(QStringLiteral("local_command"), localCommand);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

struct Fixture {
    QTemporaryDir temporary;
    QString config = temporary.filePath(QStringLiteral("claude config"));
    QString global = temporary.filePath(QStringLiteral("home/.claude.json"));
    QString state = temporary.filePath(QStringLiteral("runtime/aqtruncat/claude-usage-refresh.json"));
    QString claude = temporary.filePath(QStringLiteral("bin/claude"));
    QString log = temporary.filePath(QStringLiteral("claude.log"));

    Fixture()
    {
        QDir().mkpath(config);
        // The fake claude: logs how it was started, copies "next-cache.json"
        // over .claude.json (as /usage would), prints "envelope.json", writes
        // "stderr.txt" to stderr and exits with the status in "exit-code".
        writeFile(claude, QStringLiteral(
            "#!/bin/sh\n"
            "d='%1'\n"
            "{ echo \"args: $*\"; echo \"stdin: $(readlink /proc/self/fd/0)\";"
            "  echo \"config: ${CLAUDE_CONFIG_DIR:-unset}\"; } >> '%2'\n"
            "[ -f \"$d/sleep\" ] && { sleep 30 & echo $! > \"$d/child.pid\"; wait; }\n"
            "[ -f \"$d/next-cache.json\" ] && cp \"$d/next-cache.json\" '%3'\n"
            "[ -f \"$d/stderr.txt\" ] && cat \"$d/stderr.txt\" >&2\n"
            "cat \"$d/envelope.json\"\n"
            "exit $(cat \"$d/exit-code\" 2>/dev/null || echo 0)\n")
            .arg(temporary.path(), log, global).toUtf8());
        QFile::setPermissions(claude, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        writeFile(temporary.filePath(QStringLiteral("envelope.json")), envelope());
    }

    void nextCache(int fiveHour, int sevenDay, const char *fetchedAt)
    {
        writeFile(temporary.filePath(QStringLiteral("next-cache.json")), globalConfig(fiveHour, sevenDay, fetchedAt));
    }

    int runs() const { return int(readAll(log).count("args: ")); }

    Outcome run(const char *now, bool autoRefresh = true, bool manual = false, const QString &executable = {}) const
    {
        Options options;
        options.configDirectory = config;
        options.globalConfigFile = global;
        options.autoRefresh = autoRefresh;
        options.refreshSeconds = 5 * 60;
        options.manualRefresh = manual;
        options.claudeExecutable = executable.isNull() ? claude : executable;
        options.refreshStateFile = state;
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

QString valueOf(const QJsonObject &snapshot, const char *id)
{
    return itemById(snapshot, QLatin1String(id)).value(QStringLiteral("formattedValue")).toString();
}

QString sourceDetail(const QJsonObject &snapshot)
{
    return itemById(snapshot, QStringLiteral("source")).value(QStringLiteral("detail")).toString();
}

} // namespace

class TestClaudeUsageRefresh : public QObject
{
    Q_OBJECT

private slots:
    void anOldReportIsRefreshedThroughUsageAndReadAgain()
    {
        Fixture f;
        writeFile(f.global, globalConfig(3, 20, "2026-11-06T08:00:00Z"));
        f.nextCache(6, 27, "2026-11-06T08:10:00Z");
        QJsonObject snapshot = snapshotOf(f.run("2026-11-06T08:10:00Z"));
        QCOMPARE(f.runs(), 1);
        QCOMPARE(valueOf(snapshot, "five_hour"), QStringLiteral("6% used"));
        QCOMPARE(valueOf(snapshot, "seven_day"), QStringLiteral("27% used"));
        QVERIFY(!itemById(snapshot, QStringLiteral("source")).value(QStringLiteral("warning")).toBool());
        // Exactly the documented command, with no input and the default directory.
        const QByteArray log = readAll(f.log);
        QVERIFY2(log.contains("args: -p /usage --output-format json --no-session-persistence --strict-mcp-config\n"), log);
        QVERIFY2(log.contains("stdin: /dev/null\n"), log);
        QVERIFY2(log.contains("config: unset\n"), log);
        QCOMPARE(usageCommandText(),
                 QStringLiteral("claude -p /usage --output-format json --no-session-persistence --strict-mcp-config"));

        // Fresh now: nothing runs.
        snapshotOf(f.run("2026-11-06T08:14:00Z"));
        QCOMPARE(f.runs(), 1);
        // Old again: runs once more.
        snapshotOf(f.run("2026-11-06T08:15:30Z"));
        QCOMPARE(f.runs(), 2);
        // That run did not move the cache (still 08:10): the next one waits a
        // whole interval after the last attempt, not one snapshot.
        snapshotOf(f.run("2026-11-06T08:17:00Z"));
        QCOMPARE(f.runs(), 2);
        snapshotOf(f.run("2026-11-06T08:20:30Z"));
        QCOMPARE(f.runs(), 3);
    }

    void offNothingRunsAndAFreshLiveReportIsEnough()
    {
        Fixture f;
        writeFile(f.global, globalConfig(3, 20, "2026-11-06T08:00:00Z"));
        f.nextCache(6, 27, "2026-11-06T08:30:00Z");
        QJsonObject snapshot = snapshotOf(f.run("2026-11-06T08:30:00Z", false));
        QCOMPARE(f.runs(), 0);
        QCOMPARE(valueOf(snapshot, "five_hour"), QStringLiteral("3% used"));
        QVERIFY(!QFileInfo::exists(f.state));

        // A status-line report newer than the interval: no /usage run.
        const QString bridge = f.temporary.filePath(QStringLiteral("runtime/aqtruncat/claude-rate-limits.json"));
        const QByteArray input = QJsonDocument(QJsonObject{{QStringLiteral("rate_limits"), QJsonObject{
            {QStringLiteral("five_hour"), QJsonObject{{QStringLiteral("used_percentage"), 9},
                                                      {QStringLiteral("resets_at"), double(utc("2026-11-06T12:50:00Z").toSecsSinceEpoch())}}}}}}).toJson();
        QVERIFY(captureStatusLine(input, bridge, f.config, utc("2026-11-06T08:28:00Z")));
        Options options;
        options.configDirectory = f.config;
        options.globalConfigFile = f.global;
        options.bridgeStateFile = bridge;
        options.liveUpdatesOn = true;
        options.autoRefresh = true;
        options.claudeExecutable = f.claude;
        options.refreshStateFile = f.state;
        options.now = utc("2026-11-06T08:30:00Z");
        snapshot = snapshotOf(collect(options));
        QCOMPARE(f.runs(), 0);
        QCOMPARE(valueOf(snapshot, "five_hour"), QStringLiteral("9% used"));
    }

    void aClickRefreshesAtOnceButNotTwiceInHalfAMinute()
    {
        Fixture f;
        writeFile(f.global, globalConfig(3, 20, "2026-11-06T08:29:00Z"));
        f.nextCache(4, 21, "2026-11-06T08:30:00Z");
        // Fresh data and a click: runs anyway.
        QCOMPARE(valueOf(snapshotOf(f.run("2026-11-06T08:30:00Z", true, true)), "five_hour"), QStringLiteral("4% used"));
        QCOMPARE(f.runs(), 1);
        snapshotOf(f.run("2026-11-06T08:30:20Z", true, true));
        QCOMPARE(f.runs(), 1);
        snapshotOf(f.run("2026-11-06T08:30:31Z", true, true));
        QCOMPARE(f.runs(), 2);
        // A click while automatic refresh is off runs nothing.
        snapshotOf(f.run("2026-11-06T08:40:00Z", false, true));
        QCOMPARE(f.runs(), 2);
    }

    void aFailureIsShownKeepsTheValuesAndIsRetriedAfterAMinute()
    {
        Fixture f;
        writeFile(f.global, globalConfig(3, 20, "2026-11-06T08:00:00Z"));
        writeFile(f.temporary.filePath(QStringLiteral("exit-code")), "1");
        writeFile(f.temporary.filePath(QStringLiteral("stderr.txt")), "\nNot logged in · Please run /login\nmore\n");
        writeFile(f.temporary.filePath(QStringLiteral("envelope.json")), "");
        QJsonObject snapshot = snapshotOf(f.run("2026-11-06T08:10:00Z"));
        QCOMPARE(f.runs(), 1);
        QCOMPARE(valueOf(snapshot, "five_hour"), QStringLiteral("3% used"));
        QVERIFY(itemById(snapshot, QStringLiteral("source")).value(QStringLiteral("warning")).toBool());
        QCOMPARE(sourceDetail(snapshot),
                 QStringLiteral("Automatic refresh failed: claude exited with status 1: Not logged in · Please run /login"));
        snapshotOf(f.run("2026-11-06T08:10:40Z"));
        QCOMPARE(f.runs(), 1);
        snapshotOf(f.run("2026-11-06T08:11:00Z"));
        QCOMPARE(f.runs(), 2);
        // Recovery clears the message.
        QFile::remove(f.temporary.filePath(QStringLiteral("exit-code")));
        QFile::remove(f.temporary.filePath(QStringLiteral("stderr.txt")));
        writeFile(f.temporary.filePath(QStringLiteral("envelope.json")), envelope());
        f.nextCache(5, 22, "2026-11-06T08:12:00Z");
        snapshot = snapshotOf(f.run("2026-11-06T08:12:00Z"));
        QCOMPARE(f.runs(), 3);
        QCOMPARE(valueOf(snapshot, "five_hour"), QStringLiteral("5% used"));
        QVERIFY(sourceDetail(snapshot).isEmpty());
        QVERIFY(readRefreshState(f.state, f.config).error.isEmpty());
    }

    void sendingUsageToTheModelStopsAutomaticRefreshForGood()
    {
        Fixture f;
        writeFile(f.global, globalConfig(3, 20, "2026-11-06T08:00:00Z"));
        writeFile(f.temporary.filePath(QStringLiteral("envelope.json")), envelope(42, 1, QString(), 0.01));
        QJsonObject snapshot = snapshotOf(f.run("2026-11-06T08:10:00Z"));
        QCOMPARE(f.runs(), 1);
        QVERIFY(itemById(snapshot, QStringLiteral("source")).value(QStringLiteral("warning")).toBool());
        QVERIFY2(sourceDetail(snapshot).startsWith(QStringLiteral("Automatic refresh stopped: this Claude Code version "
                                                                  "did not run /usage locally")),
                 qPrintable(sourceDetail(snapshot)));
        // Neither the schedule nor a click runs it again, however late.
        snapshotOf(f.run("2026-11-06T09:10:00Z"));
        snapshotOf(f.run("2026-11-06T09:11:00Z", true, true));
        QCOMPARE(f.runs(), 1);
        QVERIFY(!readRefreshState(f.state, f.config).stopped.isEmpty());
        // "auto-refresh on" writes an empty state: it is tried again.
        QVERIFY(writeRefreshState(f.state, f.config, {}));
        writeFile(f.temporary.filePath(QStringLiteral("envelope.json")), envelope());
        snapshotOf(f.run("2026-11-06T09:12:00Z"));
        QCOMPARE(f.runs(), 2);
    }

    void resultJudgement()
    {
        QVERIFY(judgeUsageCommandOutput(envelope()).ok);
        QVERIFY(judgeUsageCommandOutput("\n" + envelope() + "\n").ok);
        // An older Claude Code without local_command but with no use: fine.
        QVERIFY(judgeUsageCommandOutput(envelope(0, 0, QString())).ok);
        for (const QByteArray &used : {envelope(1), envelope(0, 1), envelope(0, 0, QStringLiteral("cost")),
                                       envelope(0, 0, QStringLiteral("usage"), 0.0001)}) {
            const UsageCommandResult result = judgeUsageCommandOutput(used);
            QVERIFY(!result.ok);
            QVERIFY(result.modelUsed);
        }
        const UsageCommandResult notJson = judgeUsageCommandOutput("Current session: 6% used");
        QVERIFY(!notJson.ok && !notJson.modelUsed);
        QCOMPARE(notJson.error, QStringLiteral("claude printed no JSON result"));
        QJsonObject failed = QJsonDocument::fromJson(envelope()).object();
        failed.insert(QStringLiteral("is_error"), true);
        const UsageCommandResult error = judgeUsageCommandOutput(QJsonDocument(failed).toJson());
        QVERIFY(!error.ok && !error.modelUsed);
        // The output (with the user's usage text) never ends up in an error.
        QVERIFY(!error.error.contains(QStringLiteral("6%")));
    }

    void missingExecutableAndNoDataYet()
    {
        Fixture f;
        // Nothing cached yet, claude missing: the reason, not "open /usage".
        writeFile(f.global, "{}");
        Outcome outcome = f.run("2026-11-06T08:10:00Z", true, false, QStringLiteral(""));
        QCOMPARE(outcome.exitCode, 1);
        QCOMPARE(outcome.diagnostic,
                 QStringLiteral("Automatic refresh failed: the claude executable was not found in PATH"));
        // Nothing cached and the run did not produce a cache either: 69.
        Fixture g;
        writeFile(g.global, "{}");
        outcome = g.run("2026-11-06T08:10:00Z");
        QCOMPARE(g.runs(), 1);
        QCOMPARE(outcome.exitCode, 69);
        QCOMPARE(outcome.diagnostic, QStringLiteral("No Claude Code usage report yet."));
    }

    void anExplicitDirectoryIsPassedToClaude()
    {
        Fixture f;
        QCOMPARE(runUsageCommand(f.claude, f.config, true).error, QString());
        QVERIFY2(readAll(f.log).contains(("config: " + f.config + u'\n').toUtf8()), readAll(f.log));
    }

    void aRunThatHangsIsStoppedWithItsChildren()
    {
        Fixture f;
        writeFile(f.temporary.filePath(QStringLiteral("sleep")), "");
        const UsageCommandResult result = runUsageCommand(f.claude, f.config, false, 700);
        QCOMPARE(result.error, QStringLiteral("claude did not finish within 0.7 s"));
        const QByteArray child = readAll(f.temporary.filePath(QStringLiteral("child.pid"))).trimmed();
        QVERIFY(!child.isEmpty());
        // The "sleep 30" it started is gone too (or only a zombie being reaped).
        QTRY_VERIFY(!QFileInfo::exists(QStringLiteral("/proc/") + QString::fromLatin1(child))
                    || readAll(QStringLiteral("/proc/") + QString::fromLatin1(child) + QStringLiteral("/stat"))
                           .contains(") Z "));
    }

    void stateIsPrivateAndBoundToTheClaudeDirectory()
    {
        Fixture f;
        RefreshState state;
        state.lastAttemptAt = utc("2026-11-06T08:00:00Z");
        state.lastSuccessAt = utc("2026-11-06T08:00:01Z");
        state.error = QStringLiteral("boom");
        QVERIFY(writeRefreshState(f.state, f.config, state));
        QCOMPARE(QFile::permissions(f.state) & ~(QFileDevice::ReadUser | QFileDevice::WriteUser),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const RefreshState back = readRefreshState(f.state, f.config);
        QCOMPARE(back.lastAttemptAt, state.lastAttemptAt);
        QCOMPARE(back.lastSuccessAt, state.lastSuccessAt);
        QCOMPARE(back.error, state.error);
        QVERIFY(!readRefreshState(f.state, f.temporary.filePath(QStringLiteral("other"))).lastAttemptAt.isValid());
    }

    void dueRules()
    {
        RefreshState none;
        const QDateTime now = utc("2026-11-06T08:10:00Z");
        QVERIFY(refreshDue(none, std::nullopt, RefreshTrigger::Schedule, 300, now));
        QVERIFY(!refreshDue(none, utc("2026-11-06T08:06:00Z"), RefreshTrigger::Schedule, 300, now));
        QVERIFY(refreshDue(none, utc("2026-11-06T08:05:00Z"), RefreshTrigger::Schedule, 300, now));
        QVERIFY(refreshDue(none, utc("2026-11-06T08:09:59Z"), RefreshTrigger::Manual, 300, now));
        // An attempt in the future (clock moved back) does not block.
        RefreshState future;
        future.lastAttemptAt = utc("2026-11-06T09:00:00Z");
        QVERIFY(refreshDue(future, std::nullopt, RefreshTrigger::Schedule, 300, now));
        // A one-minute interval retries a failure after a minute, not later.
        RefreshState failed;
        failed.lastAttemptAt = utc("2026-11-06T08:09:00Z");
        failed.error = QStringLiteral("x");
        QVERIFY(refreshDue(failed, std::nullopt, RefreshTrigger::Schedule, 60, now));
    }
};

QTEST_GUILESS_MAIN(TestClaudeUsageRefresh)
#include "tst_claudeusagerefresh.moc"
