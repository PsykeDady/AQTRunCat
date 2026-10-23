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

// F5 sections on top of sample().
metrics::SystemMetricsSnapshot sampleF5()
{
    metrics::SystemMetricsSnapshot s = sample();
    s.uptime.status = MetricStatus::okStatus();
    s.uptime.seconds = 90061.6;
    s.gpu.status = MetricStatus::okStatus();
    metrics::GpuDevice intel;
    intel.id = "0000:00:02.0";
    intel.card = "card1";
    intel.driver = "i915";
    intel.vendor = "Intel";
    intel.vendorId = 0x8086;
    intel.deviceId = 0x9a49;
    intel.model = "Intel Iris Xe";
    intel.integrated = true;
    intel.utilizationStatus = MetricStatus::okStatus();
    intel.utilizationPercent = 12.5;
    intel.utilizationMethod = "rc6-residency";
    intel.memoryStatus = MetricStatus::unavailable("integrated GPU: it uses shared system memory");
    intel.temperatureStatus = MetricStatus::unavailable("integrated GPU: no sensor of its own");
    intel.frequencyMHz = 350.0;
    metrics::GpuDevice amd;
    amd.id = "0000:03:00.0";
    amd.driver = "amdgpu";
    amd.vendor = "AMD";
    amd.vendorId = 0x1002;
    amd.deviceId = 0x73bf;
    amd.model = "AMD Radeon RX 6800 XT";
    amd.selected = true;
    amd.suspended = true;
    for (MetricStatus *status : {&amd.utilizationStatus, &amd.memoryStatus, &amd.temperatureStatus})
        *status = MetricStatus::unavailable("the GPU is suspended (runtime power management); not woken up to read it");
    s.gpu.devices = {intel, amd};
    s.fps.status = MetricStatus::okStatus();
    s.fps.condition = metrics::FpsCondition::Measured;
    s.fps.source = "mangohud";
    s.fps.fps = 143.6;
    s.fps.lastFps = 143.6;
    s.fps.frametimeMs = 6.96;
    s.fps.ageSeconds = 0.4;
    s.fps.application = "Game";
    s.packageUpdates.status = MetricStatus::okStatus();
    s.packageUpdates.manager = "pacman";
    s.packageUpdates.count = 3;
    s.packageUpdates.ignored = 1;
    s.packageUpdates.lastSuccess = 1'800'000'000;
    s.packageUpdates.metadataTime = 1'799'000'000;
    s.packageUpdates.metadataStale = true;
    s.flatpakUpdates.status = MetricStatus::error("system installation: No cached summary");
    s.flatpakUpdates.manager = "flatpak";
    s.flatpakUpdates.lastSuccess = 1'799'999'000;
    s.kernel.status = MetricStatus::okStatus();
    s.kernel.info = metrics::KernelInfo{"Linux", "7.2.10-zen1-1-zen", "#1 ZEN SMP", "x86_64"};
    s.distribution.status = MetricStatus::okStatus();
    metrics::DistributionInfo d;
    d.name = d.prettyName = "Arch Linux";
    d.id = "arch";
    d.buildId = "rolling";
    d.source = "/etc/os-release";
    s.distribution.info = d;
    return s;
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

    void f5MetricsHaveStableIdsTypesAndStates()
    {
        const QJsonArray metrics = api::builtInMetrics(sampleF5(), "/home/alice");
        QStringList ids;
        for (const QJsonValue &value : metrics)
            ids << value.toObject().value(QStringLiteral("id")).toString();
        QCOMPARE(ids.mid(ids.indexOf(QStringLiteral("disk.write"))),
                 (QStringList{"disk.write", "uptime", "gpu:0000:00:02.0", "gpu:0000:03:00.0", "fps", "updates.packages",
                              "updates.flatpak", "kernel", "distribution"}));

        const QJsonObject uptime = byId(metrics, QStringLiteral("uptime"));
        QCOMPARE(uptime.value(QStringLiteral("value")).toDouble(), 90061.0);
        QCOMPARE(uptime.value(QStringLiteral("unit")).toString(), QStringLiteral("seconds"));
        QCOMPARE(uptime.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("1d 1h 1m"));
        QCOMPARE(uptime.value(QStringLiteral("compactValue")).toString(), QStringLiteral("1d"));
        QVERIFY(uptime.value(QStringLiteral("normalized")).isNull()); // no meaningless bar

        const QJsonObject intel = byId(metrics, QStringLiteral("gpu:0000:00:02.0"));
        QCOMPARE(intel.value(QStringLiteral("title")).toString(), QStringLiteral("Intel Iris Xe"));
        QCOMPARE(intel.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QCOMPARE(intel.value(QStringLiteral("value")).toDouble(), 12.5);
        QVERIFY(intel.value(QStringLiteral("normalized")).isNull()); // the items carry the bars
        const QJsonArray items = intel.value(QStringLiteral("items")).toArray();
        QCOMPARE(items.size(), 3);
        QCOMPARE(items.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Activity"));
        QCOMPARE(items.at(1).toObject().value(QStringLiteral("formattedValue")).toString(), QStringLiteral("N/A"));
        QVERIFY(items.at(1).toObject().value(QStringLiteral("compactValue")).isNull());
        QVERIFY(items.at(1).toObject().value(QStringLiteral("detail")).toString().contains(QLatin1String("shared system memory")));
        const QJsonObject intelData = intel.value(QStringLiteral("data")).toObject();
        QCOMPARE(intelData.value(QStringLiteral("vendorId")).toString(), QStringLiteral("8086"));
        QCOMPARE(intelData.value(QStringLiteral("integrated")).toBool(), true);
        QCOMPARE(intelData.value(QStringLiteral("utilization")).toObject().value(QStringLiteral("method")).toString(),
                 QStringLiteral("rc6-residency"));
        QVERIFY(intelData.value(QStringLiteral("memory")).toObject().value(QStringLiteral("usedBytes")).isNull());
        QCOMPARE(intelData.value(QStringLiteral("selected")).toBool(), false);

        const QJsonObject amd = byId(metrics, QStringLiteral("gpu:0000:03:00.0"));
        QCOMPARE(amd.value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
        QVERIFY(amd.value(QStringLiteral("message")).toString().contains(QLatin1String("suspended")));
        QVERIFY(amd.value(QStringLiteral("value")).isNull());
        QVERIFY(amd.value(QStringLiteral("data")).toObject().value(QStringLiteral("integrated")).isNull());
        QCOMPARE(amd.value(QStringLiteral("data")).toObject().value(QStringLiteral("selected")).toBool(), true);

        const QJsonObject fps = byId(metrics, QStringLiteral("fps"));
        QCOMPARE(fps.value(QStringLiteral("value")).toDouble(), 143.6);
        QCOMPARE(fps.value(QStringLiteral("unit")).toString(), QStringLiteral("fps"));
        QCOMPARE(fps.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("144 FPS"));
        QCOMPARE(fps.value(QStringLiteral("compactValue")).toString(), QStringLiteral("144"));
        QCOMPARE(fps.value(QStringLiteral("data")).toObject().value(QStringLiteral("condition")).toString(), QStringLiteral("measured"));
        QVERIFY(fps.value(QStringLiteral("normalized")).isNull());

        const QJsonObject packages = byId(metrics, QStringLiteral("updates.packages"));
        QCOMPARE(packages.value(QStringLiteral("value")).toInt(), 3);
        QVERIFY(packages.value(QStringLiteral("value")).isDouble()); // a JSON number
        QCOMPARE(packages.value(QStringLiteral("unit")).toString(), QStringLiteral("count"));
        QCOMPARE(packages.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("3 updates"));
        QCOMPARE(packages.value(QStringLiteral("compactValue")).toString(), QStringLiteral("3"));
        QVERIFY(packages.value(QStringLiteral("normalized")).isNull()); // a count is not a percentage
        const QJsonObject packagesData = packages.value(QStringLiteral("data")).toObject();
        QCOMPARE(packagesData.value(QStringLiteral("manager")).toString(), QStringLiteral("pacman"));
        QCOMPARE(packagesData.value(QStringLiteral("lastSuccess")).toString(), QStringLiteral("2027-01-15T08:00:00Z"));
        QCOMPARE(packagesData.value(QStringLiteral("metadataStale")).toBool(), true);
        QVERIFY(packages.value(QStringLiteral("detail")).toString().contains(QLatin1String("out of date")));

        const QJsonObject flatpak = byId(metrics, QStringLiteral("updates.flatpak"));
        QCOMPARE(flatpak.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
        QVERIFY(flatpak.value(QStringLiteral("value")).isNull());

        const QJsonObject kernel = byId(metrics, QStringLiteral("kernel"));
        QCOMPARE(kernel.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("Linux 7.2.10-zen1-1-zen"));
        QVERIFY(kernel.value(QStringLiteral("value")).isNull()); // text, not a number
        QVERIFY(kernel.value(QStringLiteral("unit")).isNull());
        QVERIFY(kernel.value(QStringLiteral("compactValue")).isNull()); // not offered to the Metrics Bar
        QCOMPARE(kernel.value(QStringLiteral("data")).toObject().value(QStringLiteral("machine")).toString(), QStringLiteral("x86_64"));
        const QJsonObject distribution = byId(metrics, QStringLiteral("distribution"));
        QCOMPARE(distribution.value(QStringLiteral("formattedValue")).toString(), QStringLiteral("Arch Linux"));
        QCOMPARE(distribution.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("arch"));
        QVERIFY(distribution.value(QStringLiteral("value")).isNull());
        QVERIFY(api::SchemaVersion >= 2); // F5 fields since schema 2
    }

    void f5SnapshotRoundTrips()
    {
        const metrics::SystemMetricsSnapshot original = sampleF5();
        const metrics::SystemMetricsSnapshot s = api::systemSnapshotFromMetrics(api::builtInMetrics(original, "/home/alice"));
        QCOMPARE(*s.uptime.seconds, 90061.6);
        QCOMPARE(s.gpu.devices.size(), std::size_t(2));
        const metrics::GpuDevice &intel = s.gpu.devices[0];
        QCOMPARE(intel.id, std::string("0000:00:02.0"));
        QCOMPARE(intel.vendorId, 0x8086u);
        QCOMPARE(*intel.utilizationPercent, 12.5);
        QCOMPARE(intel.memoryStatus.state, metrics::MetricState::Unavailable);
        QCOMPARE(*intel.frequencyMHz, 350.0);
        QVERIFY(*intel.integrated);
        QVERIFY(s.gpu.devices[1].selected && s.gpu.devices[1].suspended);
        QCOMPARE(*s.fps.fps, 143.6);
        QCOMPARE(s.fps.condition, metrics::FpsCondition::Measured);
        QCOMPARE(s.fps.application, std::string("Game"));
        QCOMPARE(*s.packageUpdates.count, 3);
        QCOMPARE(s.packageUpdates.ignored, 1);
        QCOMPARE(*s.packageUpdates.lastSuccess, std::int64_t(1'800'000'000));
        QVERIFY(s.packageUpdates.metadataStale);
        QCOMPARE(s.flatpakUpdates.status.state, metrics::MetricState::Error);
        QVERIFY(!s.flatpakUpdates.count);
        QCOMPARE(s.kernel.info->release, std::string("7.2.10-zen1-1-zen"));
        QCOMPARE(s.distribution.info->buildId, std::string("rolling"));
    }

    void schemaOneBackendLeavesF5Pending()
    {
        // A frontend of this version talking to an F4 backend (schema 1).
        QJsonArray old;
        for (const QJsonValue &value : api::builtInMetrics(sample(), "/home/alice")) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString();
            if (QStringList{"cpu", "memory", "swap", "network.download", "network.upload", "storage.root",
                            "storage.home", "disk.read", "disk.write"}.contains(id))
                old.append(value);
        }
        const metrics::SystemMetricsSnapshot s = api::systemSnapshotFromMetrics(old);
        QVERIFY(s.cpu.status.ok());
        for (const metrics::MetricStatus *status : {&s.uptime.status, &s.gpu.status, &s.fps.status, &s.kernel.status,
                                                     &s.distribution.status, &s.packageUpdates.status})
            QCOMPARE(status->state, metrics::MetricState::Pending);
        QVERIFY(s.gpu.devices.empty());
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

    // Schema 3 (2.1.2): "enabled" on Custom Metrics, both ways; a payload
    // without it (an older backend) means enabled.
    void customMetricEnabledFlag()
    {
        QVERIFY(api::SchemaVersion >= 3); // "enabled" since schema 3
        custommetrics::Source off;
        off.id = off.path = QStringLiteral("/data/off.json");
        off.enabled = false;
        off.state = custommetrics::State::Unavailable;
        off.message = QStringLiteral("Disabled.");
        QJsonObject metric = api::customMetric(off);
        QCOMPARE(metric.value(QStringLiteral("enabled")), QJsonValue(false));
        QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
        QVERIFY(!api::sourcesFromMetrics(QJsonArray{metric}).first().enabled);
        off.enabled = true;
        QCOMPARE(api::customMetric(off).value(QStringLiteral("enabled")), QJsonValue(true));
        metric.remove(QStringLiteral("enabled"));
        QVERIFY(api::sourcesFromMetrics(QJsonArray{metric}).first().enabled);
    }

    // Schema 4 (2.1.4): the effective display.showBar, both ways; absent
    // (older backend) = bars as before.
    void customMetricShowBar()
    {
        QVERIFY(api::SchemaVersion >= 4); // "showBar" since schema 4
        custommetrics::Source source;
        source.id = source.path = QStringLiteral("/data/dice.json");
        source.state = custommetrics::State::Available;
        custommetrics::Snapshot snapshot;
        snapshot.title = QStringLiteral("Dice");
        snapshot.metrics = {{QStringLiteral("D20"), QStringLiteral("12"), 0.6}};
        snapshot.showBar = false;
        source.snapshot = snapshot;
        QJsonObject metric = api::customMetric(source);
        QCOMPARE(metric.value(QStringLiteral("showBar")), QJsonValue(false));
        QCOMPARE(metric.value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("normalized")).toDouble(), 0.6);
        QVERIFY(!api::sourcesFromMetrics(QJsonArray{metric}).first().snapshot->showBar);
        metric.remove(QStringLiteral("showBar"));
        QVERIFY(api::sourcesFromMetrics(QJsonArray{metric}).first().snapshot->showBar);
        source.snapshot->showBar = true;
        QCOMPARE(api::customMetric(source).value(QStringLiteral("showBar")), QJsonValue(true));
        source.snapshot.reset();
        QCOMPARE(api::customMetric(source).value(QStringLiteral("showBar")), QJsonValue(true));
    }

    // Schema 5 (2.1.5): groups, both ways: kind "group", data.groupId on
    // children, data.configuredEnabled next to the effective "enabled".
    void customMetricGroups()
    {
        QCOMPARE(api::SchemaVersion, 5);
        custommetrics::Source group;
        group.id = group.path = QStringLiteral("/m/dice/metric.json");
        group.type = custommetrics::SourceType::Group;
        group.state = custommetrics::State::Available;
        group.enabled = group.configuredEnabled = false;
        custommetrics::Source child;
        child.id = child.path = QStringLiteral("/m/dice/d6/metric.json");
        child.type = custommetrics::SourceType::Command;
        child.parentId = group.id;
        child.configuredId = QStringLiteral("dice-metrics/d6");
        child.enabled = false;           // under the disabled group
        child.configuredEnabled = true;  // its own file says enabled
        const QJsonObject g = api::customMetric(group);
        const QJsonObject c = api::customMetric(child);
        QCOMPARE(g.value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
        QCOMPARE(c.value(QStringLiteral("data")).toObject().value(QStringLiteral("groupId")).toString(), group.id);
        QCOMPARE(c.value(QStringLiteral("data")).toObject().value(QStringLiteral("configuredId")).toString(), QStringLiteral("dice-metrics/d6"));
        QCOMPARE(c.value(QStringLiteral("enabled")), QJsonValue(false));
        QCOMPARE(c.value(QStringLiteral("data")).toObject().value(QStringLiteral("configuredEnabled")), QJsonValue(true));
        const QList<custommetrics::Source> back = api::sourcesFromMetrics(QJsonArray{g, c});
        QCOMPARE(back.at(0).type, custommetrics::SourceType::Group);
        QCOMPARE(back.at(1).parentId, group.id);
        QVERIFY(!back.at(1).enabled);
        QVERIFY(back.at(1).configuredEnabled);
        // An older payload: no group, own state = effective state.
        QJsonObject old = c;
        QJsonObject data = old.value(QStringLiteral("data")).toObject();
        data.remove(QStringLiteral("groupId"));
        data.remove(QStringLiteral("configuredEnabled"));
        old.insert(QStringLiteral("data"), data);
        QVERIFY(api::sourcesFromMetrics(QJsonArray{old}).first().parentId.isEmpty());
        QVERIFY(!api::sourcesFromMetrics(QJsonArray{old}).first().configuredEnabled);
    }

    void settingsObjectHasEveryExposedKey()
    {
        QTemporaryDir temporary;
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("s.ini")));
        settings.setSpeedMode(SpeedMode::SlowDown);
        const QJsonObject object = api::settingsObject(settings);
        for (const char *key : {"runnerId", "speedMode", "networkMode", "networkInterfaces", "dashboardRefreshSeconds",
                                "autostart", "metricsBarEnabled", "metricsBarItems", "customMetricFiles",
                                "gpuDevice", "fpsSource", "updatesIntervalMinutes", "updatesRemoteRefresh"})
            QVERIFY2(object.contains(QLatin1String(key)), key);
        QCOMPARE(object.value(QStringLiteral("speedMode")).toString(), QStringLiteral("slow-down"));
        QCOMPARE(object.value(QStringLiteral("runnerId")).toString(), QStringLiteral("cat"));
        QCOMPARE(object.value(QStringLiteral("gpuDevice")).toString(), QStringLiteral("auto"));
        QCOMPARE(object.value(QStringLiteral("fpsSource")).toString(), QStringLiteral("mangohud"));
        QCOMPARE(object.value(QStringLiteral("updatesIntervalMinutes")).toInt(), 60);
        QCOMPARE(object.value(QStringLiteral("updatesRemoteRefresh")).toBool(), false);
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
