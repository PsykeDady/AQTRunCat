// F2 Claude Code adapter: synthetic Claude Code state (registry, transcripts,
// a fake /proc), never the developer's real ~/.claude and never the network.

#include "claude/claudecode.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace aqtruncat::claude;

namespace {

// Private-looking content that must never reach the adapter output.
const QByteArray SecretPrompt = "PRIVATE-PROMPT-do-not-leak";
const QByteArray SecretResponse = "PRIVATE-RESPONSE-do-not-leak";
const QByteArray SecretToken = "sk-ant-FAKE-TOKEN-do-not-leak";

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

QJsonObject usageObject(qint64 input, qint64 creation, qint64 read, qint64 output)
{
    return QJsonObject{{QStringLiteral("input_tokens"), input},
                       {QStringLiteral("cache_creation_input_tokens"), creation},
                       {QStringLiteral("cache_read_input_tokens"), read},
                       {QStringLiteral("output_tokens"), output},
                       {QStringLiteral("service_tier"), QStringLiteral("standard")}};
}

QByteArray assistantLine(const QJsonObject &usage, const QString &requestedModel = QStringLiteral("claude-opus-5-5"),
                         bool sidechain = false, const QString &model = QStringLiteral("claude-opus-5-5"),
                         const QString &timestamp = QStringLiteral("2026-10-16T10:00:00.000Z"))
{
    QJsonObject entry{
        {QStringLiteral("type"), QStringLiteral("assistant")},
        {QStringLiteral("isSidechain"), sidechain},
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("sessionId"), QStringLiteral("s")},
        {QStringLiteral("message"), QJsonObject{
             {QStringLiteral("model"), model},
             {QStringLiteral("role"), QStringLiteral("assistant")},
             {QStringLiteral("content"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                {QStringLiteral("text"), QString::fromLatin1(SecretResponse)}}}},
             {QStringLiteral("usage"), usage}}},
    };
    if (!requestedModel.isEmpty())
        entry.insert(QStringLiteral("requestedModel"), requestedModel);
    return json(entry);
}

QByteArray userLine()
{
    return json(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("user")},
        {QStringLiteral("isSidechain"), false},
        {QStringLiteral("message"), QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                {QStringLiteral("content"), QString::fromLatin1(SecretPrompt)}}}});
}

QByteArray compactLine()
{
    return json(QJsonObject{{QStringLiteral("type"), QStringLiteral("system")},
                            {QStringLiteral("subtype"), QStringLiteral("compact_boundary")}});
}

// A Claude Code installation under a directory whose path contains spaces,
// plus a fake /proc in which the registry's processes can be alive.
struct Fixture {
    QTemporaryDir temporary;
    QString config = temporary.filePath(QStringLiteral("claude config"));
    QString proc = temporary.filePath(QStringLiteral("fake proc"));

    Fixture()
    {
        QDir().mkpath(config);
        writeFile(config + QStringLiteral("/.credentials.json"), "{\"accessToken\":\"" + SecretToken + "\"}");
    }

    void process(qint64 pid, const QByteArray &startTime, const QByteArray &comm = "claude")
    {
        QByteArray stat = QByteArray::number(pid) + " (" + comm + ") S";
        for (int field = 4; field <= 21; ++field)
            stat += " 0";
        stat += ' ' + startTime + " 0 0\n";
        writeFile(proc + u'/' + QString::number(pid) + QStringLiteral("/stat"), stat);
    }

    void session(qint64 pid, const QString &sessionId, const QString &cwd, const QByteArray &startTime = "4242",
                 const QString &status = QStringLiteral("idle"))
    {
        writeFile(config + QStringLiteral("/sessions/%1.json").arg(pid), json(QJsonObject{
            {QStringLiteral("pid"), pid},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("cwd"), cwd},
            {QStringLiteral("startedAt"), 1792168966066.0},
            {QStringLiteral("procStart"), QString::fromLatin1(startTime)},
            {QStringLiteral("version"), QStringLiteral("2.1.301")},
            {QStringLiteral("kind"), QStringLiteral("interactive")},
            {QStringLiteral("status"), status},
            {QStringLiteral("updatedAt"), 1792170569032.0}}));
    }

    QString transcript(const QString &sessionId, const QList<QByteArray> &lines,
                       const QString &project = QStringLiteral("-tmp-my-project"))
    {
        const QString path = config + QStringLiteral("/projects/%1/%2.jsonl").arg(project, sessionId);
        QByteArray contents;
        for (const QByteArray &line : lines)
            contents += line + '\n';
        writeFile(path, contents);
        return path;
    }

    Outcome run(const QString &cwdFilter = {}) const
    {
        Options options;
        options.configDirectory = config;
        options.procRoot = proc;
        options.cwdFilter = cwdFilter;
        options.now = QDateTime::fromString(QStringLiteral("2026-10-16T12:00:00Z"), Qt::ISODate);
        return collect(options);
    }
};

QJsonObject snapshotOf(const Outcome &outcome)
{
    if (outcome.exitCode != 0)
        qFatal("adapter exited %d: %s", outcome.exitCode, qPrintable(outcome.diagnostic));
    return QJsonDocument::fromJson(outcome.standardOutput).object();
}

QString itemValue(const QJsonObject &snapshot, const QString &title)
{
    for (const QJsonValue &value : snapshot.value(QStringLiteral("metrics")).toArray()) {
        if (value.toObject().value(QStringLiteral("title")) == title)
            return value.toObject().value(QStringLiteral("formattedValue")).toString();
    }
    return {};
}

QJsonValue itemNormalized(const QJsonObject &snapshot, const QString &title)
{
    for (const QJsonValue &value : snapshot.value(QStringLiteral("metrics")).toArray()) {
        if (value.toObject().value(QStringLiteral("title")) == title)
            return value.toObject().value(QStringLiteral("normalizedValue"));
    }
    return {};
}

void verifyNoPrivateData(const Outcome &outcome)
{
    const QByteArray all = outcome.standardOutput + outcome.diagnostic.toUtf8();
    QVERIFY(!all.contains(SecretPrompt));
    QVERIFY(!all.contains(SecretResponse));
    QVERIFY(!all.contains(SecretToken));
}

} // namespace

class TestClaudeCodeMetric : public QObject
{
    Q_OBJECT

private slots:
    void missingInstallationIsUnavailable()
    {
        QTemporaryDir temporary;
        Options options;
        options.configDirectory = temporary.filePath(QStringLiteral("no claude here"));
        const Outcome outcome = collect(options);
        QCOMPARE(outcome.exitCode, 69);
        QVERIFY(outcome.standardOutput.isEmpty());
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("not found")));
    }

    void installedButNeverUsedIsUnavailable()
    {
        Fixture f;
        const Outcome outcome = f.run();
        QCOMPARE(outcome.exitCode, 69);
        QCOMPARE(outcome.diagnostic, QStringLiteral("No running Claude Code session."));
    }

    void staleRegistryEntriesAreIgnored()
    {
        Fixture f;
        f.session(100, QStringLiteral("old-session"), QStringLiteral("/tmp/my project"));      // process gone
        f.process(200, "9999");
        f.session(200, QStringLiteral("reused-pid"), QStringLiteral("/tmp/my project"), "4242"); // PID reused
        f.transcript(QStringLiteral("old-session"), {assistantLine(usageObject(1, 2, 3, 4))});
        const Outcome outcome = f.run();
        QCOMPARE(outcome.exitCode, 69);
    }

    void unsupportedRegistryIsError()
    {
        Fixture f;
        writeFile(f.config + QStringLiteral("/sessions/123.json"), "{\"process\":123}");
        writeFile(f.config + QStringLiteral("/sessions/124.json"), "{truncated");
        writeFile(f.config + QStringLiteral("/sessions/notes.json"), "{}"); // not a <pid>.json entry
        const Outcome outcome = f.run();
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("registry")));
    }

    void validSessionProducesContextModelAndSession()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc-123"), QStringLiteral("/tmp/my project"), "4242", QStringLiteral("busy"));
        f.transcript(QStringLiteral("abc-123"), {userLine(), assistantLine(usageObject(2, 1587, 23000, 592)), userLine()});
        const Outcome outcome = f.run();
        const QJsonObject snapshot = snapshotOf(outcome);
        // (2 + 1587 + 23000) / 200000 = 12.2945 %; output tokens excluded.
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")),
                 QStringLiteral("12.3% of 200k (24.6k tokens)"));
        QCOMPARE(itemNormalized(snapshot, QStringLiteral("Context window")).toDouble(), 24589.0 / 200000.0);
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("12%"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Model")), QStringLiteral("claude-opus-5-5"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Session")), QStringLiteral("my project · busy"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Running sessions")), QStringLiteral("1"));
        QCOMPARE(snapshot.value(QStringLiteral("lastUpdatedDate")).toString(), QStringLiteral("2026-10-16T10:00:00.000Z"));
        verifyNoPrivateData(outcome);
        QVERIFY(!outcome.standardOutput.contains("/tmp/my project")); // only the directory name
    }

    void extendedContextUsesOneMillionDenominator()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(10, 0, 217390, 50),
                                                           QStringLiteral("claude-opus-5-5[1m]"))});
        const QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("21.7% of 1M (217.4k tokens)"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("22%"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Model")), QStringLiteral("claude-opus-5-5 · 1M context"));
    }

    void unknownWindowShowsTokensWithoutPercentage()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(1500, 0, 0, 10), QString())});
        const QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("1.5k tokens (window size unknown)"));
        QVERIFY(itemNormalized(snapshot, QStringLiteral("Context window")).isUndefined());
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("1.5k"));
    }

    void percentageBoundaries_data()
    {
        QTest::addColumn<qint64>("tokens");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("bar");
        QTest::newRow("zero") << qint64(0) << QStringLiteral("0.0% of 200k (0 tokens)") << QStringLiteral("0%");
        QTest::newRow("tiny") << qint64(100) << QStringLiteral("<0.1% of 200k (100 tokens)") << QStringLiteral("<1%");
        QTest::newRow("half") << qint64(1000) << QStringLiteral("0.5% of 200k (1k tokens)") << QStringLiteral("<1%");
        QTest::newRow("one") << qint64(2000) << QStringLiteral("1.0% of 200k (2k tokens)") << QStringLiteral("1%");
        QTest::newRow("full") << qint64(200000) << QStringLiteral("100.0% of 200k (200k tokens)") << QStringLiteral("100%");
        QTest::newRow("over") << qint64(210000) << QStringLiteral("105.0% of 200k (210k tokens)") << QStringLiteral("105%");
    }

    void percentageBoundaries()
    {
        QFETCH(qint64, tokens);
        QFETCH(QString, text);
        QFETCH(QString, bar);
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(tokens, 0, 0, 1))});
        const QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), text);
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), bar);
    }

    void sidechainSyntheticAndTruncatedEntriesAreSkipped()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {
            assistantLine(usageObject(10, 0, 40000, 1)),
            assistantLine(usageObject(10, 0, 90000, 1), QStringLiteral("claude-opus-5-5"), true),
            assistantLine(usageObject(0, 0, 0, 0), QString(), false, QStringLiteral("<synthetic>")),
            "{\"type\":\"assistant\",\"message\":{\"usage\":{\"input_tokens\":1", // being written
        });
        const QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("20.0% of 200k (40k tokens)"));
    }

    void compactionMakesContextUnknownUntilNextResponse()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(10, 0, 150000, 1)), compactLine(), userLine()});
        QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")),
                 QStringLiteral("Unknown until the next response after /compact"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("—"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Model")), QStringLiteral("claude-opus-5-5"));

        f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(10, 0, 150000, 1)), compactLine(),
                                             assistantLine(usageObject(10, 0, 30000, 1))});
        snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("15.0% of 200k (30k tokens)"));
    }

    void sessionWithoutResponseIsAvailableButNotZero()
    {
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("no-transcript"), QStringLiteral("/tmp/p"));
        QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("No response yet"));
        QCOMPARE(snapshot.value(QStringLiteral("metricsBarValue")).toString(), QStringLiteral("—"));
        QVERIFY(itemValue(snapshot, QStringLiteral("Model")).isEmpty());

        f.transcript(QStringLiteral("no-transcript"), {userLine(), "garbage that is not JSON", ""});
        snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("No response yet"));
    }

    void missingOrInvalidUsageFieldsAreUnsupported_data()
    {
        QTest::addColumn<QByteArray>("line");
        QJsonObject noInput = usageObject(1, 2, 3, 4);
        noInput.remove(QStringLiteral("input_tokens"));
        QJsonObject textCount = usageObject(1, 2, 3, 4);
        textCount.insert(QStringLiteral("cache_read_input_tokens"), QStringLiteral("many"));
        QJsonObject negative = usageObject(1, 2, 3, 4);
        negative.insert(QStringLiteral("output_tokens"), -5);
        QTest::newRow("missing input") << assistantLine(noInput);
        QTest::newRow("string count") << assistantLine(textCount);
        QTest::newRow("negative count") << assistantLine(negative);
    }

    void missingOrInvalidUsageFieldsAreUnsupported()
    {
        QFETCH(QByteArray, line);
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {line});
        const Outcome outcome = f.run();
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY(outcome.standardOutput.isEmpty());
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("unsupported transcript format")));
        verifyNoPrivateData(outcome);
    }

    void absentCacheCountersMeanNoCacheUse()
    {
        QJsonObject usage{{QStringLiteral("input_tokens"), 20000}, {QStringLiteral("output_tokens"), 5}};
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        f.transcript(QStringLiteral("abc"), {assistantLine(usage)});
        QCOMPARE(itemValue(snapshotOf(f.run()), QStringLiteral("Context window")),
                 QStringLiteral("10.0% of 200k (20k tokens)"));
    }

    void tailWindowGrowsAndStaysBounded()
    {
        Fixture f;
        const QByteArray filler = userLine() + QByteArray(1024, 'x');
        QList<QByteArray> lines{assistantLine(usageObject(10, 0, 50000, 1))};
        for (int i = 0; i < 600; ++i) // ~600 KiB after the response: beyond the first 256 KiB window
            lines.append(filler);
        const QString path = f.transcript(QStringLiteral("abc"), lines);
        const UsageResult grown = readLatestUsage(path);
        QCOMPARE(grown.status, UsageStatus::Found);
        QCOMPARE(grown.usage->contextTokens(), qint64(50010));

        const UsageResult bounded = readLatestUsage(path, 300 * 1024);
        QCOMPARE(bounded.status, UsageStatus::Unsupported);
        QVERIFY(!bounded.error.toUtf8().contains(SecretPrompt));
    }

    void multipleSessionsSelectMostRecentlyActive()
    {
        Fixture f;
        f.process(1001, "11");
        f.process(1002, "22");
        f.session(1001, QStringLiteral("older"), QStringLiteral("/work/alpha"), "11");
        f.session(1002, QStringLiteral("newer"), QStringLiteral("/work/beta"), "22", QStringLiteral("busy"));
        const QString older = f.transcript(QStringLiteral("older"), {assistantLine(usageObject(0, 0, 20000, 1))});
        const QString newer = f.transcript(QStringLiteral("newer"), {assistantLine(usageObject(0, 0, 60000, 1))},
                                           QStringLiteral("-work-beta"));
        QFile olderFile(older);
        QVERIFY(olderFile.open(QIODevice::ReadWrite));
        QVERIFY(olderFile.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-600), QFileDevice::FileModificationTime));
        olderFile.close();

        QJsonObject snapshot = snapshotOf(f.run());
        QCOMPARE(itemValue(snapshot, QStringLiteral("Session")), QStringLiteral("beta · busy"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Context window")), QStringLiteral("30.0% of 200k (60k tokens)"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Running sessions")),
                 QStringLiteral("2 (showing the most recently active)"));

        // --cwd pins a project; a sibling with a common prefix does not match.
        snapshot = snapshotOf(f.run(QStringLiteral("/work/alpha")));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Session")), QStringLiteral("alpha · idle"));
        QCOMPARE(itemValue(snapshot, QStringLiteral("Running sessions")), QStringLiteral("1"));
        QCOMPARE(f.run(QStringLiteral("/work/alp")).exitCode, 69);
        QCOMPARE(f.run(QStringLiteral("/work")).exitCode, 0);
        Q_UNUSED(newer);
    }

    void processMatchingHandlesOddCommandNamesAndOldEntries()
    {
        Fixture f;
        f.process(77, "123456", "we ird) (name");
        QVERIFY(processMatches(f.proc, 77, QStringLiteral("123456")));
        QVERIFY(!processMatches(f.proc, 77, QStringLiteral("123457")));
        QVERIFY(!processMatches(f.proc, 78, QStringLiteral("123456")));
        QVERIFY(!processMatches(f.proc, 0, QStringLiteral("1")));

        // Entries without procStart: fall back to the command line.
        writeFile(f.proc + QStringLiteral("/90/cmdline"), QByteArray("/usr/bin/node\0/opt/claude/cli.js\0", 33));
        writeFile(f.proc + QStringLiteral("/91/cmdline"), QByteArray("/usr/bin/vim\0", 13));
        QVERIFY(processMatches(f.proc, 90, QString()));
        QVERIFY(!processMatches(f.proc, 91, QString()));

        // The real /proc: this test process matches its own start time.
        QFile stat(QStringLiteral("/proc/self/stat"));
        QVERIFY(stat.open(QIODevice::ReadOnly));
        const QByteArray self = stat.readAll();
        const QByteArray startTime = self.mid(self.lastIndexOf(')') + 1).simplified().split(' ').at(19);
        QVERIFY(processMatches(QStringLiteral("/proc"), ::getpid(), QString::fromLatin1(startTime)));
    }

    void unreadableTranscriptIsError()
    {
        if (::geteuid() == 0)
            QSKIP("root ignores file permissions");
        Fixture f;
        f.process(321, "4242");
        f.session(321, QStringLiteral("abc"), QStringLiteral("/tmp/p"));
        const QString path = f.transcript(QStringLiteral("abc"), {assistantLine(usageObject(1, 0, 0, 1))});
        QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));
        const Outcome outcome = f.run();
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(outcome.exitCode, 1);
        QVERIFY(outcome.diagnostic.contains(QStringLiteral("cannot read the session transcript")));
    }

    void contextWindowAndTokenFormatting()
    {
        QCOMPARE(contextWindowFor(QStringLiteral("claude-sonnet-5-5")), std::optional<qint64>(200000));
        QCOMPARE(contextWindowFor(QStringLiteral("claude-opus-5-5[1m]")), std::optional<qint64>(1000000));
        QCOMPARE(contextWindowFor(QStringLiteral("claude-opus-5-5[1M]")), std::optional<qint64>(1000000));
        QCOMPARE(contextWindowFor(QString()), std::nullopt);
        QCOMPARE(formatTokens(0), QStringLiteral("0"));
        QCOMPARE(formatTokens(950), QStringLiteral("950"));
        QCOMPARE(formatTokens(1000), QStringLiteral("1k"));
        QCOMPARE(formatTokens(152345), QStringLiteral("152.3k"));
        QCOMPARE(formatTokens(200000), QStringLiteral("200k"));
        QCOMPARE(formatTokens(1000000), QStringLiteral("1M"));
        QCOMPARE(formatTokens(1250000), QStringLiteral("1.25M"));
    }

    void configDirectoryHonoursEnvironment()
    {
        qputenv("CLAUDE_CONFIG_DIR", "/some where/claude");
        QCOMPARE(defaultConfigDirectory(), QStringLiteral("/some where/claude"));
        qunsetenv("CLAUDE_CONFIG_DIR");
        QCOMPARE(defaultConfigDirectory(), QDir::home().filePath(QStringLiteral(".claude")));
    }
};

QTEST_GUILESS_MAIN(TestClaudeCodeMetric)
#include "tst_claudecodemetric.moc"
