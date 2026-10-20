// F2 end to end: the real aqtruncat-claude-metric executable run by F1's
// CustomMetricsService against synthetic Claude Code state, then presented
// by the Dashboard and the Metrics Bar; plus enable/disable of the package.

#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "app/dashboardwindow.h"
#include "app/metricsbarmodel.h"
#include "custommetrics/commandmetric.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace aqtruncat::custommetrics;

namespace {

const QByteArray SecretResponse = "PRIVATE-RESPONSE-do-not-leak";

QString adapterPath()
{
    return QStringLiteral(AQTRUNCAT_CLAUDE_ADAPTER);
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray selfStartTime()
{
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly))
        return {};
    const QByteArray text = stat.readAll();
    return text.mid(text.lastIndexOf(')') + 1).simplified().split(' ').value(19);
}

// Claude Code state whose only live session is this test process, so the
// real /proc check succeeds without a real Claude Code.
QString fakeClaude(const QString &root, qint64 cacheRead)
{
    const QString config = root + QStringLiteral("/claude config");
    writeFile(config + QStringLiteral("/sessions/%1.json").arg(::getpid()), QJsonDocument(QJsonObject{
        {QStringLiteral("pid"), qint64(::getpid())},
        {QStringLiteral("sessionId"), QStringLiteral("f2-session")},
        {QStringLiteral("cwd"), QStringLiteral("/home/alice/my project")},
        {QStringLiteral("procStart"), QString::fromLatin1(selfStartTime())},
        {QStringLiteral("status"), QStringLiteral("idle")},
        {QStringLiteral("updatedAt"), 1792170569032.0}}).toJson(QJsonDocument::Compact));
    const QJsonObject assistant{
        {QStringLiteral("type"), QStringLiteral("assistant")},
        {QStringLiteral("isSidechain"), false},
        {QStringLiteral("requestedModel"), QStringLiteral("claude-opus-5-5[1m]")},
        {QStringLiteral("timestamp"), QStringLiteral("2026-10-16T10:00:00.000Z")},
        {QStringLiteral("message"), QJsonObject{
             {QStringLiteral("model"), QStringLiteral("claude-opus-5-5")},
             {QStringLiteral("content"), QString::fromLatin1(SecretResponse)},
             {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 2},
                                                   {QStringLiteral("cache_creation_input_tokens"), 0},
                                                   {QStringLiteral("cache_read_input_tokens"), cacheRead},
                                                   {QStringLiteral("output_tokens"), 10}}}}}};
    writeFile(config + QStringLiteral("/projects/-home-alice-my-project/f2-session.jsonl"),
              QJsonDocument(assistant).toJson(QJsonDocument::Compact) + '\n');
    return config;
}

// The package "enable" would write, pointing at the build's adapter.
QString writePackage(const QString &discovery, const QString &config)
{
    const QString directory = discovery + QStringLiteral("/claude-code");
    writeFile(directory + QStringLiteral("/claude-code.svg"),
              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\" fill=\"#3d9be9\"/></svg>");
    writeFile(directory + QStringLiteral("/metric.json"), QJsonDocument(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("claude-code")},
        {QStringLiteral("title"), QStringLiteral("Claude Code")},
        {QStringLiteral("symbol"), QJsonObject{{QStringLiteral("type"), QStringLiteral("file")},
                                                {QStringLiteral("value"), QStringLiteral("./claude-code.svg")}}},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("command")},
             {QStringLiteral("executable"), adapterPath()},
             {QStringLiteral("arguments"), QJsonArray{QStringLiteral("snapshot"), QStringLiteral("--config-dir"), config}},
             {QStringLiteral("output"), QStringLiteral("snapshot")},
             {QStringLiteral("intervalSeconds"), 1},
             {QStringLiteral("timeoutSeconds"), 5}}}}).toJson());
    return directory + QStringLiteral("/metric.json");
}

const Source &onlySource(const CustomMetricsService &service)
{
    if (service.sources().size() != 1)
        qFatal("expected exactly one source, got %lld", qlonglong(service.sources().size()));
    return service.sources().first();
}

QStringList childProcesses()
{
    QStringList children;
    const QDir tasks(QStringLiteral("/proc/self/task"));
    for (const QString &task : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(tasks.filePath(task) + QStringLiteral("/children"));
        if (file.open(QIODevice::ReadOnly))
            children += QString::fromLatin1(file.readAll()).split(u' ', Qt::SkipEmptyParts);
    }
    return children;
}

struct AdapterRun {
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

AdapterRun runAdapter(const QStringList &arguments, const QString &dataHome)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
    process.setProcessEnvironment(environment);
    process.start(adapterPath(), arguments);
    process.waitForFinished(10000);
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

} // namespace

class TestClaudeCodeIntegration : public QObject
{
    Q_OBJECT

private slots:
    void adapterRunsThroughF1IntoDashboardAndMetricsBar()
    {
        QTemporaryDir temporary;
        const QString root = temporary.filePath(QStringLiteral("with spaces"));
        const QString config = fakeClaude(root, 120000);
        const QString discovery = root + QStringLiteral("/discovery dir");
        writePackage(discovery, config);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), root + QStringLiteral("/settings.ini"));

        {
            CustomMetricsService service(&settings, discovery);
            service.start();
            QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 5000);
            const Source source = onlySource(service);
            QCOMPARE(source.type, SourceType::Command);
            QCOMPARE(source.configuredId, QStringLiteral("claude-code"));
            QCOMPARE(source.snapshot->title, QStringLiteral("Claude Code"));
            QVERIFY(source.snapshot->icon);
            QCOMPARE(source.snapshot->metrics.size(), 4);
            QCOMPARE(source.snapshot->metrics.at(0).title, QStringLiteral("Context window"));
            QCOMPARE(source.snapshot->metrics.at(0).formattedValue, QStringLiteral("12.0% of 1M (120k tokens)"));
            QCOMPARE(*source.snapshot->metricsBarValue, QStringLiteral("12%"));
            for (const Item &item : source.snapshot->metrics)
                QVERIFY(!item.formattedValue.toUtf8().contains(SecretResponse));

            DashboardWindow dashboard("/home/alice");
            dashboard.show();
            dashboard.updateCustomMetrics(service.sources());
            auto *title = dashboard.findChild<QLabel *>(QStringLiteral("customTitle_0"));
            QVERIFY(title);
            QCOMPARE(title->text(), QStringLiteral("Claude Code"));
            QVERIFY(dashboard.findChild<QLabel *>(QStringLiteral("customIcon_0")));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customValue_0_0"))->text(),
                     QStringLiteral("12.0% of 1M (120k tokens)"));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customValue_0_1"))->text(),
                     QStringLiteral("claude-opus-5-5 · 1M context"));
            QCOMPARE(dashboard.findChild<QProgressBar *>(QStringLiteral("customProgress_0_0"))->value(), 120);

            // Not in the bar unless selected; selected, it shows the compact value.
            MetricsBarModel bar("/home/alice");
            bar.setConfiguration(true, aqtruncat::AppSettings::defaultMetricsBarItems());
            bar.updateCustom(service.sources());
            QTest::qWait(50);
            for (const auto &item : bar.items())
                QVERIFY(item.origin != aqtruncat::metricsbar::Origin::Custom);
            bar.setConfiguration(true, {aqtruncat::metricsbar::customId(source.id)});
            QTRY_VERIFY(bar.items().size() == 1 && bar.items().first().origin == aqtruncat::metricsbar::Origin::Custom);
            QCOMPARE(bar.items().first().shortValue, QStringLiteral("12%"));
            QCOMPARE(bar.items().first().status, aqtruncat::metricsbar::Status::Available);
            QVERIFY(bar.items().first().icon);

            // The next refresh picks up a new response.
            fakeClaude(root, 450000);
            QTRY_COMPARE_WITH_TIMEOUT(*onlySource(service).snapshot->metricsBarValue, QStringLiteral("45%"), 5000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(childProcesses().isEmpty(), 2000); // no adapter left behind
    }

    void absentClaudeIsUnavailableAndKeepsOtherStateIntact()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("discovery"));
        writePackage(discovery, temporary.filePath(QStringLiteral("no claude")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Unavailable, 5000);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("Claude Code data directory not found")));
        // Only the configured identity, never a zero value.
        QVERIFY(onlySource(service).snapshot->metrics.isEmpty());

        MetricsBarModel bar("/home/alice");
        bar.setConfiguration(true, {aqtruncat::metricsbar::customId(onlySource(service).id)});
        bar.updateCustom(service.sources());
        QTRY_COMPARE(bar.items().size(), 1);
        QCOMPARE(bar.items().first().status, aqtruncat::metricsbar::Status::Unavailable);
    }

    void enableAndDisableManageOnlyTheirOwnPackage()
    {
        QTemporaryDir temporary;
        const QString dataHome = temporary.filePath(QStringLiteral("data home"));
        const QString directory = dataHome + QStringLiteral("/AQTRunCat/metrics/claude-code");
        const QString metric = directory + QStringLiteral("/metric.json");

        AdapterRun run = runAdapter({QStringLiteral("enable"), QStringLiteral("--cwd"), QStringLiteral("/work/my project")}, dataHome);
        QCOMPARE(run.exitCode, 0);
        QVERIFY(QFileInfo(directory + QStringLiteral("/claude-code.svg")).isFile());
        const CommandParseResult parsed = parseCommandFile(metric);
        QVERIFY2(parsed.definition, qPrintable(parsed.error));
        QCOMPARE(parsed.definition->id, QStringLiteral("claude-code"));
        QCOMPARE(parsed.definition->output, CommandOutput::Snapshot);
        QCOMPARE(parsed.definition->executable, QFileInfo(adapterPath()).canonicalFilePath());
        QCOMPARE(parsed.definition->arguments,
                 QStringList({QStringLiteral("snapshot"), QStringLiteral("--cwd"), QStringLiteral("/work/my project")}));
        QCOMPARE(parsed.definition->intervalSeconds, 10);
        QVERIFY(parsed.definition->icon);
        QVERIFY(run.standardOutput.contains("Settings"));

        QCOMPARE(runAdapter({QStringLiteral("enable")}, dataHome).exitCode, 0); // our own package: updated
        QCOMPARE(parseCommandFile(metric).definition->arguments, QStringList{QStringLiteral("snapshot")});

        // A user-managed package with the same name is never overwritten or removed silently.
        writeFile(metric, "{\"id\":\"claude-code\",\"title\":\"Mine\",\"source\":{\"type\":\"command\",\"executable\":\"./my-script\"}}");
        run = runAdapter({QStringLiteral("enable")}, dataHome);
        QCOMPARE(run.exitCode, 1);
        QVERIFY(run.standardError.contains("--force"));
        QCOMPARE(parseCommandFile(metric).definition->title, QStringLiteral("Mine"));
        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 1);
        QVERIFY(QFileInfo::exists(metric));
        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--force")}, dataHome).exitCode, 0);
        QCOMPARE(parseCommandFile(metric).definition->title, QStringLiteral("Claude Code"));

        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 0);
        QVERIFY(!QFileInfo::exists(directory));
        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 0); // already disabled
    }

    void commandLineErrorsAndHelp()
    {
        QTemporaryDir temporary;
        QCOMPARE(runAdapter({}, temporary.path()).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("bogus")}, temporary.path()).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("snapshot"), QStringLiteral("--force")}, temporary.path()).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--config-dir"), QStringLiteral("/x")}, temporary.path()).exitCode, 2);
        const AdapterRun help = runAdapter({QStringLiteral("--help")}, temporary.path());
        QCOMPARE(help.exitCode, 0);
        QVERIFY(help.standardOutput.contains("snapshot"));
        const AdapterRun version = runAdapter({QStringLiteral("--version")}, temporary.path());
        QCOMPARE(version.exitCode, 0);
        QVERIFY(version.standardOutput.startsWith("aqtruncat-claude-metric "));
    }
};

QTEST_MAIN(TestClaudeCodeIntegration)
#include "tst_claudecodeintegration.moc"
