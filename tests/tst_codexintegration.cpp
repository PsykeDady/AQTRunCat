// F3 end to end: the real aqtruncat-codex-metric run by F1's
// CustomMetricsService against a synthetic Codex home (and a fake "codex"
// app-server for live updates), presented by the Dashboard under
// Integrations, the Metrics Bar and Settings › Integrations, side by side
// with the Claude Code integration. Every XDG, Codex and Claude directory is
// temporary; the real codex executable is never run.

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

const QByteArray SecretPrompt = "PRIVATE-PROMPT-do-not-leak";

QString codexAdapter() { return QStringLiteral(AQTRUNCAT_CODEX_ADAPTER); }
QString claudeAdapter() { return QStringLiteral(AQTRUNCAT_CLAUDE_ADAPTER); }

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A Codex session that recorded the given limits a minute ago.
void codexSession(const QString &codexHome, double fiveHour, double weekly)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto window = [](double used, int minutes, const QDateTime &reset) {
        return QJsonObject{{QStringLiteral("used_percent"), used}, {QStringLiteral("window_minutes"), minutes},
                           {QStringLiteral("resets_at"), double(reset.toSecsSinceEpoch())}};
    };
    const QByteArray prompt = json(QJsonObject{{QStringLiteral("type"), QStringLiteral("response_item")},
                                               {QStringLiteral("payload"), QJsonObject{{QStringLiteral("content"), QString::fromLatin1(SecretPrompt)}}}});
    const QByteArray event = json(QJsonObject{
        {QStringLiteral("timestamp"), now.addSecs(-60).toString(Qt::ISODateWithMs)},
        {QStringLiteral("type"), QStringLiteral("event_msg")},
        {QStringLiteral("payload"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("token_count")},
             {QStringLiteral("rate_limits"), QJsonObject{{QStringLiteral("limit_id"), QStringLiteral("codex")},
                                                         {QStringLiteral("primary"), window(fiveHour, 300, now.addSecs(7200))},
                                                         {QStringLiteral("secondary"), window(weekly, 10080, now.addDays(3))},
                                                         {QStringLiteral("plan_type"), QStringLiteral("plus")}}}}}});
    const QString path = codexHome + QStringLiteral("/sessions/2026/10/20/rollout-test.jsonl");
    writeFile(path + QStringLiteral(".tmp"), prompt + '\n' + event + '\n');
    QFile::remove(path);
    QFile::rename(path + QStringLiteral(".tmp"), path);
}

// A Claude Code directory whose /usage cache reports 3% / 7%.
void claudeCache(const QString &config)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto window = [](double percent, const QDateTime &reset) {
        return QJsonObject{{QStringLiteral("utilization"), percent},
                           {QStringLiteral("resets_at"), reset.toString(Qt::ISODateWithMs)}};
    };
    writeFile(config + QStringLiteral("/.claude.json"), json(QJsonObject{
        {QStringLiteral("oauthAccount"), QJsonObject{{QStringLiteral("accountUuid"), QStringLiteral("a")}}},
        {QStringLiteral("cachedUsageUtilization"), QJsonObject{
             {QStringLiteral("fetchedAtMs"), double(now.addSecs(-60).toMSecsSinceEpoch())},
             {QStringLiteral("accountUuid"), QStringLiteral("a")},
             {QStringLiteral("utilization"), QJsonObject{{QStringLiteral("five_hour"), window(3, now.addSecs(7200))},
                                                         {QStringLiteral("seven_day"), window(7, now.addDays(3))}}}}}}));
}

struct Run {
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

Run run(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    process.waitForFinished(30000);
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

QJsonObject codexStatus()
{
    return QJsonDocument::fromJson(run(codexAdapter(), {QStringLiteral("status"), QStringLiteral("--json")}).out).object();
}

const Source *sourceWith(const CustomMetricsService &service, const QString &integration)
{
    for (const Source &source : service.sources()) {
        if (source.integration == integration)
            return &source;
    }
    return nullptr;
}

const Item *itemWithId(const Source *source, const QString &id)
{
    if (!source || !source->snapshot)
        return nullptr;
    for (const Item &item : source->snapshot->metrics) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

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

} // namespace

class TestCodexIntegration : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QString m_codexHome;
    QString m_claudeConfig;
    QString m_dataHome;
    QString m_metrics;
    QString m_codexLog;

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_codexHome = m_root.filePath(QStringLiteral("codex home"));
        m_claudeConfig = m_root.filePath(QStringLiteral("claude config"));
        m_dataHome = m_root.filePath(QStringLiteral("data"));
        m_metrics = m_dataHome + QStringLiteral("/AQTRunCat/metrics");
        m_codexLog = m_root.filePath(QStringLiteral("codex.log"));
        QDir().mkpath(m_codexHome);
        QDir().mkpath(m_metrics);
        QDir().mkpath(m_root.filePath(QStringLiteral("runtime")));
        QFile::setPermissions(m_root.filePath(QStringLiteral("runtime")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        // A fake "codex": its app-server answers the usage read (32% / 4%).
        const QString fakeBin = m_root.filePath(QStringLiteral("fakebin"));
        writeFile(fakeBin + QStringLiteral("/codex"), QStringLiteral(
            "#!/bin/sh\n"
            "echo \"args: $*\" >> '%1'\n"
            "while IFS= read -r line; do\n"
            "  case \"$line\" in\n"
            "    *'\"initialize\"'*) echo '{\"id\":1,\"result\":{}}' ;;\n"
            "    *'account/rateLimits/read'*) now=$(date +%s); echo \"{\\\"id\\\":2,\\\"result\\\":{\\\"rateLimits\\\":{\\\"limitId\\\":\\\"codex\\\",\\\"primary\\\":{\\\"usedPercent\\\":32,\\\"windowDurationMins\\\":300,\\\"resetsAt\\\":$((now+3600))},\\\"secondary\\\":{\\\"usedPercent\\\":4,\\\"windowDurationMins\\\":10080,\\\"resetsAt\\\":$((now+86400))},\\\"planType\\\":\\\"plus\\\"},\\\"accountId\\\":\\\"acct-PRIVATE\\\"}}\" ;;\n"
            "  esac\n"
            "done\n").arg(m_codexLog).toUtf8());
        QFile::setPermissions(fakeBin + QStringLiteral("/codex"),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("PATH", (fakeBin + u':' + qEnvironmentVariable("PATH")).toUtf8());
        qputenv("HOME", m_root.filePath(QStringLiteral("home")).toUtf8());
        qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
        qputenv("XDG_RUNTIME_DIR", m_root.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("CODEX_HOME", m_codexHome.toUtf8());
        qputenv("CLAUDE_CONFIG_DIR", m_claudeConfig.toUtf8());
        codexSession(m_codexHome, 12, 40);
        claudeCache(m_claudeConfig);
    }

    void codexUnderIntegrationsNextToClaude()
    {
        QCOMPARE(run(codexAdapter(), {QStringLiteral("enable")}).exitCode, 0);
        QCOMPARE(run(claudeAdapter(), {QStringLiteral("enable")}).exitCode, 0);
        // A user metric with the provider's title stays a Custom Metric.
        writeFile(m_metrics + QStringLiteral("/codex-lookalike.json"),
                  "{\"title\":\"Codex\",\"metrics\":[{\"title\":\"x\",\"formattedValue\":\"1\"}],\"lastUpdatedDate\":\"2026-06-05T04:50:40Z\"}");
        aqtruncat::AppSettings settings(QStringLiteral("cat"), m_root.filePath(QStringLiteral("a.ini")));
        {
            CustomMetricsService service(&settings, m_metrics);
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(sourceWith(service, QStringLiteral("codex"))
                                         && sourceWith(service, QStringLiteral("codex"))->state == State::Available
                                         && sourceWith(service, QStringLiteral("claude-code"))
                                         && sourceWith(service, QStringLiteral("claude-code"))->state == State::Available, 8000);
            const Source *codex = sourceWith(service, QStringLiteral("codex"));
            QCOMPARE(codex->snapshot->title, QStringLiteral("Codex"));
            QVERIFY(codex->snapshot->icon);
            const Item *five = itemWithId(codex, QStringLiteral("window_300m"));
            const Item *week = itemWithId(codex, QStringLiteral("window_10080m"));
            QVERIFY(five && week);
            QCOMPARE(five->title, QStringLiteral("5h limit"));
            QCOMPARE(five->formattedValue, QStringLiteral("12% used"));
            QVERIFY(five->detail.startsWith(QStringLiteral("88% left · Resets ")));
            QCOMPARE(week->title, QStringLiteral("Weekly limit"));
            QCOMPARE(week->formattedValue, QStringLiteral("40% used"));
            for (const Item &item : codex->snapshot->metrics)
                QVERIFY(!(item.formattedValue + item.detail).toUtf8().contains(SecretPrompt));

            DashboardWindow dashboard("/home/alice");
            dashboard.show();
            dashboard.updateCustomMetrics(service.sources());
            auto *integrations = dashboard.findChild<QWidget *>(QStringLiteral("integrationsContainer"));
            auto *custom = dashboard.findChild<QWidget *>(QStringLiteral("customMetricsContainer"));
            QStringList integrationTitles;
            for (QLabel *title : integrations->findChildren<QLabel *>(QRegularExpression(QStringLiteral("^customTitle_\\d+$"))))
                integrationTitles << title->text();
            integrationTitles.sort();
            QCOMPARE(integrationTitles, (QStringList{QStringLiteral("Claude Code"), QStringLiteral("Codex")}));
            QCOMPARE(custom->findChildren<QLabel *>(QRegularExpression(QStringLiteral("^customTitle_\\d+$"))).size(), 1);
            // Codex's two windows: two bars of their own.
            const auto bars = integrations->findChildren<QProgressBar *>();
            QStringList barNames;
            for (QProgressBar *bar : bars)
                barNames << bar->accessibleName();
            QVERIFY(barNames.contains(QStringLiteral("5h limit")));
            QVERIFY(barNames.contains(QStringLiteral("Weekly limit")));
            QVERIFY(barNames.contains(QStringLiteral("Current session (5h)")));

            // Metrics Bar: both providers' short windows, told apart.
            MetricsBarModel bar("/home/alice");
            QObject::connect(&service, &CustomMetricsService::sourcesChanged, &bar, &MetricsBarModel::updateCustom);
            bar.setConfiguration(true, aqtruncat::AppSettings::defaultMetricsBarItems());
            bar.updateCustom(service.sources());
            QTest::qWait(50);
            for (const auto &item : bar.items())
                QVERIFY(item.origin != aqtruncat::metricsbar::Origin::Custom); // nothing added by itself
            bar.setConfiguration(true, {aqtruncat::metricsbar::customItemId(codex->id, QStringLiteral("window_300m")),
                                        aqtruncat::metricsbar::customItemId(sourceWith(service, QStringLiteral("claude-code"))->id,
                                                                            QStringLiteral("five_hour"))});
            QTRY_COMPARE(bar.items().size(), 2);
            QCOMPARE(bar.items().at(0).shortLabel, QStringLiteral("cx5h"));
            QCOMPARE(bar.items().at(0).shortValue, QStringLiteral("12%"));
            QCOMPARE(bar.items().at(0).label, QStringLiteral("Codex — 5h limit"));
            QCOMPARE(bar.items().at(1).shortLabel, QStringLiteral("5h"));
            QCOMPARE(bar.items().at(1).label, QStringLiteral("Claude Code — Current session (5h)"));
            QVERIFY(bar.items().at(0).shortLabel != bar.items().at(1).shortLabel);

            // A new Codex report is picked up.
            codexSession(m_codexHome, 13, 41);
            QTRY_COMPARE_WITH_TIMEOUT(bar.items().at(0).shortValue, QStringLiteral("13%"), 20000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(childProcesses().isEmpty(), 3000);
        QFile::remove(m_metrics + QStringLiteral("/codex-lookalike.json"));
        QCOMPARE(run(claudeAdapter(), {QStringLiteral("disable")}).exitCode, 0);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("disable")}).exitCode, 0);
    }

    void settingsManagesCodexIndependentlyOfClaude()
    {
        aqtruncat::AppSettings settings(QStringLiteral("cat"), m_root.filePath(QStringLiteral("b.ini")));
        CustomMetricsService service(&settings, m_metrics);
        service.start();
        QFile::remove(m_codexLog);
        {
            ProviderIntegration claude(QStringLiteral("claude-code"), claudeAdapter());
            ProviderIntegration codex(QStringLiteral("codex"), codexAdapter());
            SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat")}}, &service, nullptr,
                                  &claude, &codex);
            auto *codexEnabled = window.findChild<QCheckBox *>(QStringLiteral("codexEnabled"));
            auto *codexLive = window.findChild<QCheckBox *>(QStringLiteral("codexLiveUpdates"));
            auto *codexStatusLabel = window.findChild<QLabel *>(QStringLiteral("codexStatus"));
            auto *claudeEnabled = window.findChild<QCheckBox *>(QStringLiteral("claudeEnabled"));
            QVERIFY(codexEnabled && codexLive && codexStatusLabel && claudeEnabled);
            QTRY_VERIFY(codexEnabled->isEnabled() && claudeEnabled->isEnabled());
            QVERIFY(!codexEnabled->isChecked());
            QVERIFY(!codexLive->isEnabled());
            QCOMPARE(codexStatusLabel->text(), QStringLiteral("Off."));

            // GUI on: Codex only.
            codexEnabled->click();
            QTRY_VERIFY_WITH_TIMEOUT(codex.status().value(QStringLiteral("enabled")).toBool(), 15000);
            QTRY_COMPARE_WITH_TIMEOUT(codexStatusLabel->text(), QStringLiteral("On — available."), 8000);
            QVERIFY(!claude.status().value(QStringLiteral("enabled")).toBool());
            QVERIFY(!claudeEnabled->isChecked());
            // Under Integrations, not Custom Metrics; its windows offered to the Metrics Bar.
            QCOMPARE(window.findChild<QTreeWidget *>(QStringLiteral("customMetricFiles"))->topLevelItemCount(), 0);
            auto *barItems = window.findChild<QTreeWidget *>(QStringLiteral("metricsBarItems"));
            const QString codexId = sourceWith(service, QStringLiteral("codex"))->id;
            QTRY_VERIFY(treeIds(barItems).contains(aqtruncat::metricsbar::customItemId(codexId, QStringLiteral("window_300m"))));
            QVERIFY(!treeIds(barItems).contains(aqtruncat::metricsbar::customId(codexId)));
            // No live read without consent.
            QVERIFY(!QFileInfo::exists(m_codexLog));

            // GUI live updates after confirmation: the fake app-server answers.
            QTRY_VERIFY(codexLive->isEnabled());
            acceptNextQuestion();
            codexLive->click();
            QTRY_VERIFY_WITH_TIMEOUT(codex.status().value(QStringLiteral("liveUpdates")).toString() == QLatin1String("on"), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(itemWithId(sourceWith(service, QStringLiteral("codex")), QStringLiteral("window_300m"))
                                         && itemWithId(sourceWith(service, QStringLiteral("codex")), QStringLiteral("window_300m"))
                                                    ->formattedValue == QLatin1String("32% used"), 20000);
            QVERIFY(QFileInfo::exists(m_codexLog));

            // Command line → GUI.
            QCOMPARE(run(codexAdapter(), {QStringLiteral("live-updates"), QStringLiteral("off")}).exitCode, 0);
            QTRY_VERIFY_WITH_TIMEOUT(!codexLive->isChecked(), 15000);
            QCOMPARE(run(codexAdapter(), {QStringLiteral("disable")}).exitCode, 0);
            QTRY_VERIFY_WITH_TIMEOUT(!codexEnabled->isChecked(), 15000);
            QTRY_VERIFY_WITH_TIMEOUT(!sourceWith(service, QStringLiteral("codex")), 15000);
        }
        // The live state never stores the account id.
        QFile state(m_root.filePath(QStringLiteral("runtime/aqtruncat/codex-rate-limits.json")));
        QVERIFY(state.open(QIODevice::ReadOnly));
        QVERIFY(!state.readAll().contains("acct-PRIVATE"));
    }

    void packagePolicyUpgradeAndStatus()
    {
        const QString metric = m_metrics + QStringLiteral("/codex/metric.json");
        Run r = run(codexAdapter(), {QStringLiteral("upgrade")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.out.contains("nothing to upgrade"));
        QVERIFY(!QFileInfo::exists(metric));

        QCOMPARE(run(codexAdapter(), {QStringLiteral("live-updates"), QStringLiteral("on")}).exitCode, 1); // not enabled
        QCOMPARE(run(codexAdapter(), {QStringLiteral("enable"), QStringLiteral("--live")}).exitCode, 0);
        CommandParseResult parsed = parseCommandFile(metric);
        QVERIFY2(parsed.definition, qPrintable(parsed.error));
        QCOMPARE(parsed.definition->integration, QStringLiteral("codex"));
        QCOMPARE(parsed.definition->output, CommandOutput::Snapshot);
        QCOMPARE(parsed.definition->arguments, (QStringList{QStringLiteral("snapshot"), QStringLiteral("--live")}));
        QCOMPARE(parsed.definition->executable, QFileInfo(codexAdapter()).canonicalFilePath());
        QJsonObject status = codexStatus();
        QVERIFY(status.value(QStringLiteral("enabled")).toBool());
        QVERIFY(status.value(QStringLiteral("integrationMarked")).toBool());
        QCOMPARE(status.value(QStringLiteral("liveUpdates")).toString(), QStringLiteral("on"));
        QVERIFY(status.value(QStringLiteral("codexFound")).toBool());

        // An older package at another path: upgrade keeps the options.
        QJsonObject old = QJsonDocument::fromJson(readAll(metric)).object();
        QJsonObject source = old.value(QStringLiteral("source")).toObject();
        source.insert(QStringLiteral("executable"), QStringLiteral("/old/bin/aqtruncat-codex-metric"));
        old.insert(QStringLiteral("source"), source);
        old.remove(QStringLiteral("integration"));
        writeFile(metric, QJsonDocument(old).toJson());
        QCOMPARE(run(codexAdapter(), {QStringLiteral("upgrade")}).exitCode, 0);
        parsed = parseCommandFile(metric);
        QCOMPARE(parsed.definition->integration, QStringLiteral("codex"));
        QCOMPARE(parsed.definition->arguments, (QStringList{QStringLiteral("snapshot"), QStringLiteral("--live")}));
        QCOMPARE(parsed.definition->executable, QFileInfo(codexAdapter()).canonicalFilePath());

        // A user-managed package named codex is never replaced or removed.
        const QByteArray mine = "{\"id\":\"codex\",\"title\":\"Mine\",\"source\":{\"type\":\"command\",\"executable\":\"./mine\"}}";
        writeFile(metric, mine);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("enable")}).exitCode, 1);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("disable")}).exitCode, 1);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("upgrade")}).exitCode, 0);
        QVERIFY(codexStatus().value(QStringLiteral("userPackage")).toBool());
        QCOMPARE(readAll(metric), mine);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("enable"), QStringLiteral("--force")}).exitCode, 0);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("disable")}).exitCode, 0);
        QVERIFY(!QFileInfo::exists(m_metrics + QStringLiteral("/codex")));
    }

    void commandLine()
    {
        QCOMPARE(run(codexAdapter(), {}).exitCode, 2);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("bogus")}).exitCode, 2);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("snapshot"), QStringLiteral("--force")}).exitCode, 2);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("live-updates")}).exitCode, 2);
        QCOMPARE(run(codexAdapter(), {QStringLiteral("disable"), QStringLiteral("--live")}).exitCode, 2);
        const Run help = run(codexAdapter(), {QStringLiteral("--help")});
        QCOMPARE(help.exitCode, 0);
        QVERIFY(help.out.contains("account/rateLimits/read"));
        QVERIFY(run(codexAdapter(), {QStringLiteral("--version")}).out.startsWith("aqtruncat-codex-metric "));
        // Without any Codex: Unavailable (69), never an empty or zero snapshot.
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("CODEX_HOME"), m_root.filePath(QStringLiteral("no codex")));
        environment.insert(QStringLiteral("PATH"), QStringLiteral("/nonexistent"));
        process.setProcessEnvironment(environment);
        process.start(codexAdapter(), {QStringLiteral("snapshot")});
        process.waitForFinished(10000);
        QCOMPARE(process.exitCode(), 69);
        QVERIFY(process.readAllStandardOutput().isEmpty());
    }
};

QTEST_MAIN(TestCodexIntegration)
#include "tst_codexintegration.moc"
