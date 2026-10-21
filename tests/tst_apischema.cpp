// F4.1 API schema: built-in metrics, Custom Metrics and official
// integrations as JSON (what every client reads), and back to the Qt
// frontend's own types. No D-Bus here.

#include "api/apischema.h"
#include "app/appsettings.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat;
using metrics::MetricStatus;

namespace {

metrics::SystemMetricsSnapshot sample()
{
    metrics::SystemMetricsSnapshot s;
    s.cpu.status = MetricStatus::okStatus();
    s.cpu.usagePercent = 23.4;
    s.cpu.logicalCpus = 8;
    s.memory.status = MetricStatus::okStatus();
    metrics::MemoryInfo m;
    m.totalBytes = 16ull << 30;
    m.usedBytes = 4ull << 30;
    m.availableBytes = 12ull << 30;
    m.usagePercent = 25.0;
    m.swapPresent = false;
    s.memory.info = m;
    s.network.status = MetricStatus::okStatus();
    s.network.downloadBytesPerSecond = 2048.0; // upload: needs another sample
    s.network.countedInterfaces = 1;
    metrics::InterfaceMetrics wlan;
    wlan.name = "wlan0";
    wlan.kind = metrics::InterfaceKind::Physical;
    wlan.counted = true;
    wlan.downloadBytesPerSecond = 2048.0;
    s.network.interfaces.push_back(wlan);
    s.diskIo.status = MetricStatus::unavailable("no physical disk");
    metrics::StorageMetrics root;
    root.path = "/";
    root.status = MetricStatus::okStatus();
    metrics::StorageInfo info;
    info.path = "/";
    info.totalBytes = 100ull << 30;
    info.usedBytes = 60ull << 30;
    info.availableBytes = 40ull << 30;
    info.usagePercent = 60.0;
    root.info = info;
    s.storage.push_back(root);
    metrics::StorageMetrics home;
    home.path = "/home/alice";
    home.status = MetricStatus::error("statvfs failed");
    s.storage.push_back(home);
    return s;
}

QJsonObject byId(const QJsonArray &array, const QString &id)
{
    for (const QJsonValue &value : array) {
        if (value.toObject().value(QStringLiteral("id")) == id)
            return value.toObject();
    }
    return {};
}

custommetrics::Source quotaSource()
{
    custommetrics::Source source;
    source.id = QStringLiteral("/data/AQTRunCat/metrics/claude-code/metric.json");
    source.path = source.id;
    source.discovered = true;
    source.type = custommetrics::SourceType::Command;
    source.configuredId = QStringLiteral("claude-code");
    source.integration = QStringLiteral("claude-code");
    source.state = custommetrics::State::Available;
    custommetrics::Snapshot snapshot;
    snapshot.title = QStringLiteral("Claude Code");
    snapshot.icon = custommetrics::IconReference{custommetrics::IconType::File, QStringLiteral("./claude-code.svg"),
                                                 QStringLiteral("/data/AQTRunCat/metrics/claude-code/claude-code.svg")};
    snapshot.metricsBarValue = QStringLiteral("3%");
    custommetrics::Item session{QStringLiteral("Current session (5h)"), QStringLiteral("3% used"), 0.03};
    session.id = QStringLiteral("five_hour");
    session.detail = QStringLiteral("Resets at 13:30");
    session.metricsBarValue = QStringLiteral("3%");
    session.metricsBarLabel = QStringLiteral("5h");
    custommetrics::Item week{QStringLiteral("Current week (all models)"), QStringLiteral("Out of date"), std::nullopt};
    week.id = QStringLiteral("seven_day");
    week.warning = true;
    snapshot.metrics = {session, week};
    snapshot.lastUpdatedDate = QDateTime(QDate(2026, 10, 20), QTime(8, 28, 39, 571), QTimeZone::UTC);
    source.snapshot = snapshot;
    return source;
}

} // namespace

class TestApiSchema : public QObject
{
    Q_OBJECT

private slots:
    void builtInMetricsBeforeTheFirstSampleArePending()
    {
        const QJsonArray metrics = api::builtInMetrics(std::nullopt, "/home/alice");
        QVERIFY(!metrics.isEmpty());
        for (const QJsonValue &value : metrics) {
            const QJsonObject metric = value.toObject();
            QCOMPARE(metric.value(QStringLiteral("category")).toString(), QStringLiteral("builtin"));
            QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
            QVERIFY(metric.value(QStringLiteral("value")).isNull()); // never 0
        }
        QVERIFY(byId(metrics, QStringLiteral("storage.home")).isEmpty()); // not known yet
    }

    void builtInMetricsCarryValuesStatesAndRawData()
    {
        const QJsonArray metrics = api::builtInMetrics(sample(), "/home/alice");
        const QJsonObject cpu = byId(metrics, QStringLiteral("cpu"));
        QCOMPARE(cpu.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QCOMPARE(cpu.value(QStringLiteral("value")).toDouble(), 23.4);
        QCOMPARE(cpu.value(QStringLiteral("unit")).toString(), QStringLiteral("percent"));
        QCOMPARE(cpu.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("23.4 %"));
        QCOMPARE(cpu.value(QStringLiteral("compactValue")).toString(), QStringLiteral("23%"));
        QCOMPARE(cpu.value(QStringLiteral("normalized")).toDouble(), 0.234);
        QCOMPARE(cpu.value(QStringLiteral("data")).toObject().value(QStringLiteral("logicalCpus")).toInt(), 8);

        // No swap: unavailable with a reason, not 0 %.
        const QJsonObject swap = byId(metrics, QStringLiteral("swap"));
        QCOMPARE(swap.value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
        QCOMPARE(swap.value(QStringLiteral("message")).toString(), QStringLiteral("no swap is configured"));
        QVERIFY(swap.value(QStringLiteral("value")).isNull());

        const QJsonObject down = byId(metrics, QStringLiteral("network.download"));
        QCOMPARE(down.value(QStringLiteral("value")).toDouble(), 2048.0);
        QCOMPARE(down.value(QStringLiteral("unit")).toString(), QStringLiteral("bytes-per-second"));
        // The section works, the rate needs another sample.
        const QJsonObject up = byId(metrics, QStringLiteral("network.upload"));
        QCOMPARE(up.value(QStringLiteral("state")).toString(), QStringLiteral("pending"));
        QVERIFY(up.value(QStringLiteral("value")).isNull());

        QCOMPARE(byId(metrics, QStringLiteral("disk.read")).value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
        QCOMPARE(byId(metrics, QStringLiteral("storage.root")).value(QStringLiteral("value")).toDouble(), 60.0);
        const QJsonObject home = byId(metrics, QStringLiteral("storage.home"));
        QCOMPARE(home.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
        QCOMPARE(home.value(QStringLiteral("message")).toString(), QStringLiteral("statvfs failed"));
        // Valid JSON for any client.
        QVERIFY(QJsonDocument::fromJson(QJsonDocument(metrics).toJson()).isArray());
    }

    void systemSnapshotRoundTrips()
    {
        const metrics::SystemMetricsSnapshot original = sample();
        const metrics::SystemMetricsSnapshot s = api::systemSnapshotFromMetrics(api::builtInMetrics(original, "/home/alice"));
        QVERIFY(s.cpu.status.ok());
        QCOMPARE(*s.cpu.usagePercent, 23.4);
        QCOMPARE(s.cpu.logicalCpus, 8);
        QVERIFY(s.memory.info);
        QCOMPARE(s.memory.info->usedBytes, original.memory.info->usedBytes);
        QCOMPARE(s.memory.info->totalBytes, original.memory.info->totalBytes);
        QCOMPARE(s.memory.info->usagePercent, 25.0);
        QVERIFY(!s.memory.info->swapPresent);
        QVERIFY(s.network.status.ok());
        QCOMPARE(*s.network.downloadBytesPerSecond, 2048.0);
        QVERIFY(!s.network.uploadBytesPerSecond);
        QCOMPARE(s.network.interfaces.size(), std::size_t(1));
        QCOMPARE(s.network.interfaces.front().name, std::string("wlan0"));
        QCOMPARE(s.network.interfaces.front().kind, metrics::InterfaceKind::Physical);
        QCOMPARE(s.diskIo.status.state, metrics::MetricState::Unavailable);
        QCOMPARE(s.diskIo.status.message, std::string("no physical disk"));
        QCOMPARE(s.storage.size(), std::size_t(2));
        QCOMPARE(s.storage.at(0).path, std::string("/"));
        QCOMPARE(s.storage.at(0).info->availableBytes, original.storage.at(0).info->availableBytes);
        QCOMPARE(s.storage.at(1).path, std::string("/home/alice"));
        QCOMPARE(s.storage.at(1).status.state, metrics::MetricState::Error);
    }

    void integrationAndCustomClassificationAndMultiValueItems()
    {
        const QJsonObject quotas = api::customMetric(quotaSource());
        QCOMPARE(quotas.value(QStringLiteral("id")).toString(), QStringLiteral("custom:/data/AQTRunCat/metrics/claude-code/metric.json"));
        QCOMPARE(quotas.value(QStringLiteral("category")).toString(), QStringLiteral("integration"));
        QCOMPARE(quotas.value(QStringLiteral("integration")).toString(), QStringLiteral("claude-code"));
        QCOMPARE(quotas.value(QStringLiteral("kind")).toString(), QStringLiteral("command"));
        QCOMPARE(quotas.value(QStringLiteral("compactValue")).toString(), QStringLiteral("3%"));
        QCOMPARE(quotas.value(QStringLiteral("updatedAt")).toString(), QStringLiteral("2026-10-20T08:28:39.571Z"));
        QCOMPARE(quotas.value(QStringLiteral("icon")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("file"));
        const QJsonArray items = quotas.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("compactLabel")).toString(), QStringLiteral("5h"));
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("normalized")).toDouble(), 0.03);
        QVERIFY(items.at(1).toObject().value(QStringLiteral("normalized")).isNull());
        QVERIFY(items.at(1).toObject().value(QStringLiteral("warning")).toBool());

        // The same title without provenance is a user metric.
        custommetrics::Source lookalike = quotaSource();
        lookalike.id = lookalike.path = QStringLiteral("/data/claude.json");
        lookalike.integration.clear();
        lookalike.type = custommetrics::SourceType::Snapshot;
        lookalike.snapshot->icon.reset();
        lookalike.snapshot->symbol = QStringLiteral("staroflife");
        const QJsonObject mine = api::customMetric(lookalike);
        QCOMPARE(mine.value(QStringLiteral("category")).toString(), QStringLiteral("custom"));
        QCOMPARE(mine.value(QStringLiteral("kind")).toString(), QStringLiteral("snapshot"));
        QVERIFY(mine.value(QStringLiteral("integration")).isNull());
        QCOMPARE(mine.value(QStringLiteral("icon")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("symbol"));

        // Back to sources: everything the Dashboard and the Metrics Bar use.
        const QList<custommetrics::Source> back = api::sourcesFromMetrics(QJsonArray{quotas, mine});
        QCOMPARE(back.size(), 2);
        const custommetrics::Source &q = back.at(0);
        QCOMPARE(q.id, quotaSource().id);
        QCOMPARE(q.integration, QStringLiteral("claude-code"));
        QCOMPARE(q.type, custommetrics::SourceType::Command);
        QCOMPARE(q.state, custommetrics::State::Available);
        QCOMPARE(q.snapshot->title, QStringLiteral("Claude Code"));
        QCOMPARE(q.snapshot->icon->resolvedValue, quotaSource().snapshot->icon->resolvedValue);
        QCOMPARE(*q.snapshot->metricsBarValue, QStringLiteral("3%"));
        QCOMPARE(q.snapshot->lastUpdatedDate, quotaSource().snapshot->lastUpdatedDate);
        QCOMPARE(q.snapshot->metrics.size(), 2);
        QCOMPARE(q.snapshot->metrics.at(0).id, QStringLiteral("five_hour"));
        QCOMPARE(*q.snapshot->metrics.at(0).normalizedValue, 0.03);
        QCOMPARE(q.snapshot->metrics.at(0).detail, QStringLiteral("Resets at 13:30"));
        QVERIFY(q.snapshot->metrics.at(1).warning);
        QVERIFY(!q.snapshot->metrics.at(1).normalizedValue);
        QCOMPARE(*back.at(1).snapshot->symbol, QStringLiteral("staroflife"));
        QVERIFY(back.at(1).integration.isEmpty());
    }

    void skeletonAndBrokenSourcesKeepTheirState()
    {
        custommetrics::Source broken;
        broken.id = broken.path = QStringLiteral("/data/broken.json");
        broken.state = custommetrics::State::Error;
        broken.message = QStringLiteral("invalid JSON");
        const QJsonObject metric = api::customMetric(broken);
        QCOMPARE(metric.value(QStringLiteral("title")).toString(), QStringLiteral("broken"));
        QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
        const custommetrics::Source back = api::sourcesFromMetrics(QJsonArray{metric}).first();
        QVERIFY(!back.snapshot);
        QCOMPARE(back.state, custommetrics::State::Error);
        QCOMPARE(back.message, QStringLiteral("invalid JSON"));
    }

    void settingsObjectHasEveryExposedKey()
    {
        QTemporaryDir temporary;
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("s.ini")));
        settings.setSpeedMode(SpeedMode::SlowDown);
        const QJsonObject object = api::settingsObject(settings);
        for (const char *key : {"runnerId", "speedMode", "networkMode", "networkInterfaces", "dashboardRefreshSeconds",
                                "autostart", "metricsBarEnabled", "metricsBarItems", "customMetricFiles"})
            QVERIFY2(object.contains(QLatin1String(key)), key);
        QCOMPARE(object.value(QStringLiteral("speedMode")).toString(), QStringLiteral("slow-down"));
        QCOMPARE(object.value(QStringLiteral("runnerId")).toString(), QStringLiteral("cat"));
    }

    void reloadPicksUpAnotherWriter()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("s.ini"));
        AppSettings mine(QStringLiteral("cat"), file);
        AppSettings other(QStringLiteral("cat"), file);
        int speedSignals = 0;
        int barSignals = 0;
        connect(&mine, &AppSettings::speedModeChanged, this, [&] { ++speedSignals; });
        connect(&mine, &AppSettings::metricsBarChanged, this, [&] { ++barSignals; });
        other.setSpeedMode(SpeedMode::SlowDown);
        QVERIFY(mine.reload());
        QCOMPARE(mine.speedMode(), SpeedMode::SlowDown);
        QCOMPARE(speedSignals, 1);
        QCOMPARE(barSignals, 0); // only what changed
        QVERIFY(!mine.reload()); // nothing new: no signal
        QCOMPARE(speedSignals, 1);
    }
};

QTEST_GUILESS_MAIN(TestApiSchema)
#include "tst_apischema.moc"
