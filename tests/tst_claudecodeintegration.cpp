// F2 end to end: the real aqtruncat-claude-metric executable run by F1's
// CustomMetricsService against synthetic Claude Code state, then presented
// by the Dashboard, the Metrics Bar and the Settings window's Integrations
// section; plus the adapter's management commands and its status-line
// bridge. Every XDG and Claude Code directory is a temporary one.

#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "app/dashboardwindow.h"
#include "app/metricsbarmodel.h"
#include "app/providerintegration.h"
#include "app/settingswindow.h"
#include "custommetrics/commandmetric.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>

using namespace aqtruncat::custommetrics;

namespace {

const QString Account = QStringLiteral("00000000-0000-4000-8000-00000000a11c");

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

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A Claude Code directory whose usage cache (as /usage leaves it) reports
// the given percentages, fetched a minute ago; windows reset in the future.
QString fakeClaude(const QString &root, double fiveHour, double sevenDay)
{
    const QString config = root + QStringLiteral("/claude config");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto window = [](double percent, const QDateTime &reset) {
        return QJsonObject{{QStringLiteral("utilization"), percent},
                           {QStringLiteral("resets_at"), reset.toString(Qt::ISODateWithMs)}};
    };
    const QJsonObject global{
        {QStringLiteral("oauthAccount"), QJsonObject{{QStringLiteral("accountUuid"), Account},
                                                     {QStringLiteral("emailAddress"), QStringLiteral("alice@example.com")}}},
        {QStringLiteral("cachedUsageUtilization"), QJsonObject{
             {QStringLiteral("fetchedAtMs"), double(now.addSecs(-60).toMSecsSinceEpoch())},
             {QStringLiteral("accountUuid"), Account},
             {QStringLiteral("utilization"), QJsonObject{
                  {QStringLiteral("five_hour"), window(fiveHour, now.addSecs(2 * 3600))},
                  {QStringLiteral("seven_day"), window(sevenDay, now.addDays(3))}}}}},
    };
    QDir().mkpath(config);
    // Written atomically, like Claude Code, so a refresh never reads half a file.
    const QString path = config + QStringLiteral("/.claude.json");
    writeFile(path + QStringLiteral(".tmp"), QJsonDocument(global).toJson());
    QFile::remove(path);
    QFile::rename(path + QStringLiteral(".tmp"), path);
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
        {QStringLiteral("integration"), QStringLiteral("claude-code")},
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

const Item *itemWithId(const Source &source, const QString &id)
{
    if (!source.snapshot)
        return nullptr;
    for (const Item &item : source.snapshot->metrics) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
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

// The adapter with this test's private environment (see initTestCase), plus
// optional overrides and standard input.
AdapterRun runAdapter(const QStringList &arguments, const QString &dataHome = {},
                      const QByteArray &input = {}, const QMap<QString, QString> &environment = {})
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!dataHome.isEmpty())
        env.insert(QStringLiteral("XDG_DATA_HOME"), dataHome);
    for (auto it = environment.cbegin(); it != environment.cend(); ++it)
        env.insert(it.key(), it.value());
    process.setProcessEnvironment(env);
    process.start(adapterPath(), arguments);
    process.write(input);
    process.closeWriteChannel();
    process.waitForFinished(10000);
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

// Accepts the next confirmation dialog the Settings window opens.
void acceptNextQuestion()
{
    auto *timer = new QTimer;
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer, [timer] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            box->button(QMessageBox::Yes)->click();
            timer->deleteLater();
        }
    });
    timer->start();
}

QStringList treeIds(const QTreeWidget *tree)
{
    QStringList ids;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        ids << tree->topLevelItem(i)->data(0, Qt::UserRole).toString();
    return ids;
}

} // namespace

class TestClaudeCodeIntegration : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_private;

private slots:
    void initTestCase()
    {
        // Every adapter started by this test (directly, by F1 or by the
        // Settings window) sees only temporary state: no real ~/.claude.json,
        // live-updates file, Claude Code settings or AQTRunCat data.
        QVERIFY(m_private.isValid());
        qputenv("HOME", m_private.filePath(QStringLiteral("home")).toUtf8());
        qputenv("XDG_RUNTIME_DIR", m_private.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_private.filePath(QStringLiteral("data")).toUtf8());
        qputenv("CLAUDE_CONFIG_DIR", m_private.filePath(QStringLiteral("no claude")).toUtf8());
        QDir().mkpath(m_private.filePath(QStringLiteral("runtime")));
        QFile::setPermissions(m_private.filePath(QStringLiteral("runtime")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void usageQuotasRunThroughF1IntoDashboardAndMetricsBar()
    {
        QTemporaryDir temporary;
        const QString root = temporary.filePath(QStringLiteral("with spaces"));
        const QString config = fakeClaude(root, 3, 7);
        const QString discovery = root + QStringLiteral("/discovery dir");
        writePackage(discovery, config);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), root + QStringLiteral("/settings.ini"));

        {
            CustomMetricsService service(&settings, discovery);
            service.start();
            QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 5000);
            const Source source = onlySource(service);
            QCOMPARE(source.configuredId, QStringLiteral("claude-code"));
            QCOMPARE(source.integration, QStringLiteral("claude-code"));
            QCOMPARE(source.snapshot->title, QStringLiteral("Claude Code"));
            QVERIFY(source.snapshot->icon);
            const Item *session = itemWithId(source, QStringLiteral("five_hour"));
            const Item *week = itemWithId(source, QStringLiteral("seven_day"));
            QVERIFY(session && week);
            QCOMPARE(session->title, QStringLiteral("Current session (5h)"));
            QCOMPARE(session->formattedValue, QStringLiteral("3% used"));
            QCOMPARE(*session->normalizedValue, 0.03);
            QVERIFY(session->detail.startsWith(QStringLiteral("Resets at ")));
            QCOMPARE(week->title, QStringLiteral("Current week (all models)"));
            QCOMPARE(week->formattedValue, QStringLiteral("7% used"));
            QVERIFY(week->detail.startsWith(QStringLiteral("Resets ")));
            // The context window is not part of the default integration.
            QVERIFY(!itemWithId(source, QStringLiteral("context")));

            // Dashboard: two separate bars, each with its reset time.
            DashboardWindow dashboard("/home/alice");
            dashboard.show();
            dashboard.updateCustomMetrics(service.sources());
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customTitle_0"))->text(), QStringLiteral("Claude Code"));
            QVERIFY(dashboard.findChild<QLabel *>(QStringLiteral("customIcon_0")));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customValue_0_0"))->text(), QStringLiteral("3% used"));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customValue_0_1"))->text(), QStringLiteral("7% used"));
            auto *sessionBar = dashboard.findChild<QProgressBar *>(QStringLiteral("customProgress_0_0"));
            auto *weekBar = dashboard.findChild<QProgressBar *>(QStringLiteral("customProgress_0_1"));
            QVERIFY(sessionBar && weekBar && sessionBar != weekBar);
            QCOMPARE(sessionBar->value(), 30);
            QCOMPARE(weekBar->value(), 70);
            QCOMPARE(sessionBar->accessibleName(), QStringLiteral("Current session (5h)"));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customDetail_0_0"))->text(), session->detail);
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customDetail_0_1"))->text(), week->detail);
            QVERIFY(!dashboard.findChild<QProgressBar *>(QStringLiteral("customProgress_0_2"))); // "Usage data" has no bar

            // Metrics Bar: nothing unless selected; each quota selectable on its own.
            MetricsBarModel bar("/home/alice");
            QObject::connect(&service, &CustomMetricsService::sourcesChanged, &bar, &MetricsBarModel::updateCustom);
            bar.setConfiguration(true, aqtruncat::AppSettings::defaultMetricsBarItems());
            bar.updateCustom(service.sources());
            QTest::qWait(50);
            for (const auto &item : bar.items())
                QVERIFY(item.origin != aqtruncat::metricsbar::Origin::Custom);
            const QString sessionId = aqtruncat::metricsbar::customItemId(source.id, QStringLiteral("five_hour"));
            const QString weekId = aqtruncat::metricsbar::customItemId(source.id, QStringLiteral("seven_day"));
            bar.setConfiguration(true, {weekId, sessionId});
            QTRY_COMPARE(bar.items().size(), 2);
            QCOMPARE(bar.items().at(0).shortValue, QStringLiteral("7%"));
            QCOMPARE(bar.items().at(0).shortLabel, QStringLiteral("7d"));
            QVERIFY(!bar.items().at(0).icon); // the caption tells the two apart
            QCOMPARE(bar.items().at(0).label, QStringLiteral("Claude Code — Current week (all models)"));
            QVERIFY(bar.items().at(0).detail.startsWith(QStringLiteral("7% used — Resets ")));
            QCOMPARE(bar.items().at(1).shortValue, QStringLiteral("3%"));
            QCOMPARE(bar.items().at(1).shortLabel, QStringLiteral("5h"));
            QCOMPARE(bar.items().at(1).status, aqtruncat::metricsbar::Status::Available);

            // Both values follow the next /usage independently.
            fakeClaude(root, 4, 9);
            QTRY_COMPARE_WITH_TIMEOUT(itemWithId(onlySource(service), QStringLiteral("five_hour"))->formattedValue,
                                      QStringLiteral("4% used"), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(bar.items().at(0).shortValue, QStringLiteral("9%"), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(bar.items().at(1).shortValue, QStringLiteral("4%"), 2000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(childProcesses().isEmpty(), 2000); // no adapter left behind
    }

    void absentClaudeIsUnavailableAndNeverZero()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("discovery"));
        writePackage(discovery, temporary.filePath(QStringLiteral("no claude")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Unavailable, 5000);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("Claude Code not found")));
        QVERIFY(onlySource(service).snapshot->metrics.isEmpty());

        MetricsBarModel bar("/home/alice");
        bar.setConfiguration(true, {aqtruncat::metricsbar::customItemId(onlySource(service).id, QStringLiteral("five_hour"))});
        bar.updateCustom(service.sources());
        QTRY_COMPARE(bar.items().size(), 1);
        QCOMPARE(bar.items().first().status, aqtruncat::metricsbar::Status::Unavailable);
        QVERIFY(bar.items().first().shortValue.isEmpty());
    }

    void enableAndDisableManageOnlyTheirOwnPackage()
    {
        QTemporaryDir temporary;
        const QString dataHome = temporary.filePath(QStringLiteral("data home"));
        const QString directory = dataHome + QStringLiteral("/AQTRunCat/metrics/claude-code");
        const QString metric = directory + QStringLiteral("/metric.json");

        AdapterRun run = runAdapter({QStringLiteral("enable")}, dataHome);
        QCOMPARE(run.exitCode, 0);
        QVERIFY(QFileInfo(directory + QStringLiteral("/claude-code.svg")).isFile());
        const CommandParseResult parsed = parseCommandFile(metric);
        QVERIFY2(parsed.definition, qPrintable(parsed.error));
        QCOMPARE(parsed.definition->id, QStringLiteral("claude-code"));
        QCOMPARE(parsed.definition->integration, QStringLiteral("claude-code"));
        QCOMPARE(parsed.definition->output, CommandOutput::Snapshot);
        QCOMPARE(parsed.definition->executable, QFileInfo(adapterPath()).canonicalFilePath());
        QCOMPARE(parsed.definition->arguments, QStringList{QStringLiteral("snapshot")});
        QCOMPARE(parsed.definition->intervalSeconds, 10);
        QVERIFY(parsed.definition->icon);

        // The context window is an option of the same package.
        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--context"), QStringLiteral("--cwd"),
                             QStringLiteral("/work/my project")}, dataHome).exitCode, 0);
        QCOMPARE(parseCommandFile(metric).definition->arguments,
                 QStringList({QStringLiteral("snapshot"), QStringLiteral("--context"), QStringLiteral("--cwd"),
                              QStringLiteral("/work/my project")}));
        const QJsonObject status = QJsonDocument::fromJson(
            runAdapter({QStringLiteral("status"), QStringLiteral("--json")}, dataHome).standardOutput).object();
        QVERIFY(status.value(QStringLiteral("enabled")).toBool());
        QVERIFY(status.value(QStringLiteral("context")).toBool());
        QCOMPARE(status.value(QStringLiteral("cwd")).toString(), QStringLiteral("/work/my project"));
        QCOMPARE(status.value(QStringLiteral("liveUpdates")).toString(), QStringLiteral("off"));

        // A user-managed package with the same name is never overwritten or removed silently.
        writeFile(metric, "{\"id\":\"claude-code\",\"title\":\"Mine\",\"source\":{\"type\":\"command\",\"executable\":\"./my-script\"}}");
        run = runAdapter({QStringLiteral("enable")}, dataHome);
        QCOMPARE(run.exitCode, 1);
        QVERIFY(run.standardError.contains("--force"));
        QCOMPARE(parseCommandFile(metric).definition->title, QStringLiteral("Mine"));
        QVERIFY(QJsonDocument::fromJson(runAdapter({QStringLiteral("status"), QStringLiteral("--json")}, dataHome)
                                            .standardOutput).object().value(QStringLiteral("userPackage")).toBool());
        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 1);
        QVERIFY(QFileInfo::exists(metric));
        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--force")}, dataHome).exitCode, 0);
        QCOMPARE(parseCommandFile(metric).definition->title, QStringLiteral("Claude Code"));

        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 0);
        QVERIFY(!QFileInfo::exists(directory));
        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 0); // already disabled
    }

    void liveUpdatesThroughTheRealStatusLine()
    {
        QTemporaryDir temporary;
        const QString dataHome = temporary.filePath(QStringLiteral("data"));
        const QString config = temporary.filePath(QStringLiteral("claude"));
        const QString runtime = temporary.filePath(QStringLiteral("run"));
        const QString settingsFile = config + QStringLiteral("/settings.json");
        // The user's own status line: prints a marker and how much input it got.
        const QString script = temporary.filePath(QStringLiteral("my status.sh"));
        writeFile(script, "#!/bin/sh\nprintf 'mine:%s' \"$(wc -c | tr -d ' ')\"\n");
        QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QByteArray original = "{\n  \"statusLine\": {\"type\": \"command\", \"command\": \"'"
                                    + script.toUtf8() + "'\"},\n  \"model\": \"opus\"\n}\n";
        writeFile(settingsFile, original);
        const QMap<QString, QString> claudeEnvironment{{QStringLiteral("CLAUDE_CONFIG_DIR"), config},
                                                       {QStringLiteral("XDG_RUNTIME_DIR"), runtime}};

        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--config-dir"), config}, dataHome).exitCode, 0);
        AdapterRun run = runAdapter({QStringLiteral("live-updates"), QStringLiteral("on")}, dataHome);
        QCOMPARE(run.exitCode, 0);
        QVERIFY(QJsonDocument::fromJson(readAll(settingsFile)).object().value(QStringLiteral("statusLine")).toObject()
                    .value(QStringLiteral("command")).toString().endsWith(QStringLiteral(" statusline")));

        // What Claude Code does: run the status line with its JSON on stdin.
        const QDateTime reset = QDateTime::currentDateTimeUtc().addSecs(3600);
        const QByteArray input = QJsonDocument(QJsonObject{
            {QStringLiteral("cwd"), QStringLiteral("/private/project")},
            {QStringLiteral("rate_limits"), QJsonObject{
                 {QStringLiteral("five_hour"), QJsonObject{{QStringLiteral("used_percentage"), 12.5},
                                                          {QStringLiteral("resets_at"), double(reset.toSecsSinceEpoch())}}},
                 {QStringLiteral("seven_day"), QJsonObject{{QStringLiteral("used_percentage"), 30},
                                                          {QStringLiteral("resets_at"), double(reset.addDays(2).toSecsSinceEpoch())}}}}}})
                                     .toJson(QJsonDocument::Compact);
        run = runAdapter({QStringLiteral("statusline")}, dataHome, input, claudeEnvironment);
        QCOMPARE(run.exitCode, 0);
        // The user's status line ran with the very same input.
        QCOMPARE(run.standardOutput, QByteArray("mine:") + QByteArray::number(input.size()));

        // The next snapshot uses the live report (no /usage cache exists here).
        run = runAdapter({QStringLiteral("snapshot"), QStringLiteral("--config-dir"), config}, dataHome, {}, claudeEnvironment);
        QCOMPARE(run.exitCode, 0);
        const QJsonObject snapshot = QJsonDocument::fromJson(run.standardOutput).object();
        const QJsonArray metrics = snapshot.value(QStringLiteral("metrics")).toArray();
        QCOMPARE(metrics.at(0).toObject().value(QStringLiteral("formattedValue")).toString(), QStringLiteral("12.5% used"));
        QCOMPARE(metrics.at(1).toObject().value(QStringLiteral("formattedValue")).toString(), QStringLiteral("30% used"));
        QCOMPARE(metrics.at(2).toObject().value(QStringLiteral("formattedValue")).toString(),
                 QStringLiteral("live, Claude Code status line"));

        const QJsonObject status = QJsonDocument::fromJson(
            runAdapter({QStringLiteral("status"), QStringLiteral("--json")}, dataHome, {}, claudeEnvironment).standardOutput).object();
        QCOMPARE(status.value(QStringLiteral("liveUpdates")).toString(), QStringLiteral("on"));
        QVERIFY(status.value(QStringLiteral("chainedStatusLine")).toBool());
        QVERIFY(status.value(QStringLiteral("lastLiveReport")).isString());

        // Disabling the integration puts the user's status line back exactly.
        QCOMPARE(runAdapter({QStringLiteral("disable")}, dataHome).exitCode, 0);
        QCOMPARE(readAll(settingsFile), original);
    }

    void settingsIntegrationsSectionSharesStateWithTheCommandLine()
    {
        QTemporaryDir temporary;
        const QString dataHome = m_private.filePath(QStringLiteral("data"));
        const QString discovery = dataHome + QStringLiteral("/AQTRunCat/metrics");
        const QString config = fakeClaude(temporary.filePath(QStringLiteral("root")), 3, 7);
        qputenv("CLAUDE_CONFIG_DIR", config.toUtf8());
        QDir().mkpath(discovery);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();

        {
            ProviderIntegration integration(QStringLiteral("claude-code"), adapterPath());
            SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}}, &service, nullptr,
                                  &integration);
            auto *enabled = window.findChild<QCheckBox *>(QStringLiteral("claudeEnabled"));
            auto *live = window.findChild<QCheckBox *>(QStringLiteral("claudeLiveUpdates"));
            auto *context = window.findChild<QCheckBox *>(QStringLiteral("claudeContext"));
            auto *status = window.findChild<QLabel *>(QStringLiteral("claudeStatus"));
            QVERIFY(enabled && live && context && status);
            QTRY_VERIFY(enabled->isEnabled());
            QVERIFY(!enabled->isChecked());
            QVERIFY(!live->isEnabled());
            QCOMPARE(status->text(), QStringLiteral("Off."));

            // GUI on: the adapter writes the package, F1 discovers and runs it.
            enabled->click();
            QTRY_VERIFY_WITH_TIMEOUT(integration.status().value(QStringLiteral("enabled")).toBool(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("On — available."), 5000);
            QVERIFY(enabled->isChecked());
            QVERIFY(live->isEnabled());
            // Not a user Custom Metric: managed under Integrations only.
            auto *customFiles = window.findChild<QTreeWidget *>(QStringLiteral("customMetricFiles"));
            QCOMPARE(customFiles->topLevelItemCount(), 0);
            // The Metrics Bar offers each quota, not the whole source.
            auto *barItems = window.findChild<QTreeWidget *>(QStringLiteral("metricsBarItems"));
            const QString sourceId = onlySource(service).id;
            QTRY_VERIFY(treeIds(barItems).contains(aqtruncat::metricsbar::customItemId(sourceId, QStringLiteral("five_hour"))));
            QVERIFY(treeIds(barItems).contains(aqtruncat::metricsbar::customItemId(sourceId, QStringLiteral("seven_day"))));
            QVERIFY(!treeIds(barItems).contains(aqtruncat::metricsbar::customId(sourceId)));

            // Command line → GUI.
            QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--context")}).exitCode, 0);
            QTRY_VERIFY_WITH_TIMEOUT(context->isChecked(), 5000);

            // GUI live updates, after confirmation; the adapter edits settings.json.
            QTRY_VERIFY(live->isEnabled());
            acceptNextQuestion();
            live->click();
            QTRY_VERIFY_WITH_TIMEOUT(integration.status().value(QStringLiteral("liveUpdates")).toString()
                                         == QLatin1String("on"), 5000);
            QVERIFY(QJsonDocument::fromJson(readAll(config + QStringLiteral("/settings.json"))).object()
                        .value(QStringLiteral("statusLine")).isObject());
        }

        // Persistence: a new window (as after a restart) shows the same state.
        {
            ProviderIntegration integration(QStringLiteral("claude-code"), adapterPath());
            SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}}, &service, nullptr,
                                  &integration);
            auto *enabled = window.findChild<QCheckBox *>(QStringLiteral("claudeEnabled"));
            QTRY_VERIFY(enabled->isEnabled());
            QVERIFY(enabled->isChecked());
            QVERIFY(window.findChild<QCheckBox *>(QStringLiteral("claudeLiveUpdates"))->isChecked());
            QVERIFY(window.findChild<QCheckBox *>(QStringLiteral("claudeContext"))->isChecked());

            // GUI off: live updates and the package go away together.
            enabled->click();
            QTRY_VERIFY_WITH_TIMEOUT(!integration.status().value(QStringLiteral("enabled")).toBool(), 5000);
            QVERIFY(!QFileInfo::exists(config + QStringLiteral("/settings.json"))); // it did not exist before
            QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), qsizetype(0), 5000);
        }
        qputenv("CLAUDE_CONFIG_DIR", m_private.filePath(QStringLiteral("no claude")).toUtf8());
    }

    void providerAndLookalikeUserMetricsStaySeparate()
    {
        QTemporaryDir temporary;
        const QString root = temporary.filePath(QStringLiteral("root"));
        const QString config = fakeClaude(root, 3, 7);
        const QString discovery = root + QStringLiteral("/discovery");
        const QString package = writePackage(discovery, config);
        // A legacy M6 snapshot with the provider's title, icon-less, even
        // claiming the provenance key: snapshots are always user metrics.
        writeFile(discovery + QStringLiteral("/claude.json"),
                  R"({"title":"Claude Code","integration":"claude-code","metricsBarValue":"5%","metrics":[{"title":"Context","formattedValue":"5%","normalizedValue":0.05}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        // The user's own command metric with the same title.
        writeFile(discovery + QStringLiteral("/mine/metric.json"),
                  R"({"id":"mine","title":"Claude Code","source":{"type":"command","executable":"/bin/echo","arguments":["42"],"intervalSeconds":60}})");
        aqtruncat::AppSettings settings(QStringLiteral("cat"), root + QStringLiteral("/settings.ini"));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(std::all_of(service.sources().cbegin(), service.sources().cend(),
                                             [](const Source &s) { return s.state == State::Available; })
                                     && service.sources().size() == 3, 5000);
        int providers = 0;
        for (const Source &source : service.sources()) {
            const bool official = QFileInfo(source.path).canonicalFilePath() == QFileInfo(package).canonicalFilePath();
            QCOMPARE(source.integration, official ? QStringLiteral("claude-code") : QString());
            providers += official;
        }
        QCOMPARE(providers, 1);

        DashboardWindow dashboard("/home/alice");
        dashboard.show();
        dashboard.updateCustomMetrics(service.sources());
        auto *integrations = dashboard.findChild<QWidget *>(QStringLiteral("integrationsContainer"));
        auto *custom = dashboard.findChild<QWidget *>(QStringLiteral("customMetricsContainer"));
        QCOMPARE(integrations->findChildren<QLabel *>(QRegularExpression(QStringLiteral("^customTitle_"))).size(), 1);
        QCOMPARE(custom->findChildren<QLabel *>(QRegularExpression(QStringLiteral("^customTitle_"))).size(), 2);
        const auto values = integrations->findChildren<QLabel *>(QRegularExpression(QStringLiteral("^customValue_\\d+_0$")));
        QCOMPARE(values.size(), 1);
        QCOMPARE(values.first()->text(), QStringLiteral("3% used"));
    }

    void packageFromBeforeProvenanceIsMigratedBySettings()
    {
        QTemporaryDir temporary;
        const QString dataHome = m_private.filePath(QStringLiteral("data"));
        const QString metric = dataHome + QStringLiteral("/AQTRunCat/metrics/claude-code/metric.json");
        const QString config = fakeClaude(temporary.filePath(QStringLiteral("root")), 3, 7);
        qputenv("CLAUDE_CONFIG_DIR", config.toUtf8());
        QCOMPARE(runAdapter({QStringLiteral("enable"), QStringLiteral("--context")}).exitCode, 0);
        // As written by the first F2 version: no "integration" field.
        QJsonObject old = QJsonDocument::fromJson(readAll(metric)).object();
        old.remove(QStringLiteral("integration"));
        writeFile(metric, QJsonDocument(old).toJson());
        QVERIFY(!QJsonDocument::fromJson(runAdapter({QStringLiteral("status"), QStringLiteral("--json")}).standardOutput)
                     .object().value(QStringLiteral("integrationMarked")).toBool());

        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, dataHome + QStringLiteral("/AQTRunCat/metrics"));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), qsizetype(1), 5000);
        QCOMPARE(service.sources().first().integration, QString()); // still shown as a user metric
        {
            ProviderIntegration integration(QStringLiteral("claude-code"), adapterPath());
            SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}}, &service, nullptr,
                                  &integration);
            // Even before the rewrite it is not listed among Custom Metrics.
            QTRY_VERIFY_WITH_TIMEOUT(integration.statusKnown(), 5000);
            QCOMPARE(window.findChild<QTreeWidget *>(QStringLiteral("customMetricFiles"))->topLevelItemCount(), 0);
            QTRY_VERIFY_WITH_TIMEOUT(integration.status().value(QStringLiteral("integrationMarked")).toBool(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().integration, QStringLiteral("claude-code"), 5000);
            // Its options were kept.
            QVERIFY(integration.status().value(QStringLiteral("context")).toBool());
        }
        QCOMPARE(runAdapter({QStringLiteral("disable")}).exitCode, 0);
        qputenv("CLAUDE_CONFIG_DIR", m_private.filePath(QStringLiteral("no claude")).toUtf8());
    }

    void upgradeRewritesOnlyAnEnabledOwnPackage()
    {
        QTemporaryDir temporary;
        const QString dataHome = temporary.filePath(QStringLiteral("data"));
        const QString directory = dataHome + QStringLiteral("/AQTRunCat/metrics/claude-code");
        const QString metric = directory + QStringLiteral("/metric.json");

        // Off: nothing is created.
        AdapterRun run = runAdapter({QStringLiteral("upgrade")}, dataHome);
        QCOMPARE(run.exitCode, 0);
        QVERIFY(run.standardOutput.contains("nothing to upgrade"));
        QVERIFY(!QFileInfo::exists(directory));

        // As the first F2 version wrote it: no provenance, an adapter at an
        // older path, the context option on.
        writeFile(metric, R"({"id":"claude-code","title":"Claude Code","symbol":{"type":"file","value":"./claude-code.svg"},"source":{"type":"command","executable":"/old/prefix/bin/aqtruncat-claude-metric","arguments":["snapshot","--context","--cwd","/work/p"],"output":"snapshot","intervalSeconds":10,"timeoutSeconds":5}})");
        writeFile(directory + QStringLiteral("/notes.txt"), "the user's own file");
        run = runAdapter({QStringLiteral("upgrade")}, dataHome);
        QCOMPARE(run.exitCode, 0);
        QVERIFY(run.standardOutput.contains("upgraded"));
        const CommandParseResult parsed = parseCommandFile(metric);
        QVERIFY2(parsed.definition, qPrintable(parsed.error));
        QCOMPARE(parsed.definition->integration, QStringLiteral("claude-code"));
        QCOMPARE(parsed.definition->executable, QFileInfo(adapterPath()).canonicalFilePath());
        QCOMPARE(parsed.definition->arguments, QStringList({QStringLiteral("snapshot"), QStringLiteral("--context"),
                                                            QStringLiteral("--cwd"), QStringLiteral("/work/p")}));
        QVERIFY(QFileInfo::exists(directory + QStringLiteral("/notes.txt"))); // nothing deleted
        QVERIFY(QJsonDocument::fromJson(runAdapter({QStringLiteral("status"), QStringLiteral("--json")}, dataHome)
                                            .standardOutput).object().value(QStringLiteral("integrationMarked")).toBool());

        // A user-managed package with the same id is left exactly as it is.
        const QByteArray mine = "{\"id\":\"claude-code\",\"title\":\"Mine\",\"source\":{\"type\":\"command\",\"executable\":\"./my-script\"}}";
        writeFile(metric, mine);
        QCOMPARE(runAdapter({QStringLiteral("upgrade")}, dataHome).exitCode, 0);
        QCOMPARE(readAll(metric), mine);
        QCOMPARE(runAdapter({QStringLiteral("upgrade"), QStringLiteral("--force")}, dataHome).exitCode, 2);
    }

    void missingAdapterIsReportedInSettings()
    {
        aqtruncat::AppSettings settings(QStringLiteral("cat"), m_private.filePath(QStringLiteral("missing.ini")));
        ProviderIntegration integration(QStringLiteral("claude-code"), QString());
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}}, nullptr, nullptr, &integration);
        QVERIFY(!window.findChild<QCheckBox *>(QStringLiteral("claudeEnabled"))->isEnabled());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("claudeStatus"))->text().contains(QStringLiteral("not installed")));
        // Without an integration the section does not exist at all.
        SettingsWindow plain(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}});
        QVERIFY(!plain.findChild<QWidget *>(QStringLiteral("integrationsGroup")));
    }

    void commandLineErrorsAndHelp()
    {
        QCOMPARE(runAdapter({}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("bogus")}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("snapshot"), QStringLiteral("--force")}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("snapshot"), QStringLiteral("--cwd"), QStringLiteral("/x")}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("status"), QStringLiteral("--context")}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("live-updates")}).exitCode, 2);
        QCOMPARE(runAdapter({QStringLiteral("live-updates"), QStringLiteral("maybe")}).exitCode, 2);
        const AdapterRun help = runAdapter({QStringLiteral("--help")});
        QCOMPARE(help.exitCode, 0);
        QVERIFY(help.standardOutput.contains("live-updates"));
        QVERIFY(help.standardOutput.contains("/usage"));
        const AdapterRun version = runAdapter({QStringLiteral("--version")});
        QCOMPARE(version.exitCode, 0);
        QVERIFY(version.standardOutput.startsWith("aqtruncat-claude-metric "));
        // The status line never fails, whatever it is given.
        QCOMPARE(runAdapter({QStringLiteral("statusline"), QStringLiteral("--weird")}, {}, "garbage").exitCode, 0);
    }
};

QTEST_MAIN(TestClaudeCodeIntegration)
#include "tst_claudecodeintegration.moc"
