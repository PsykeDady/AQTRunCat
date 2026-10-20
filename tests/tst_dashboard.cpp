#include "app/dashboardwindow.h"
#include "app/metricsampler.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QProgressBar>
#include <QSignalSpy>
#include <QScrollArea>
#include <QRegularExpression>
#include <QTest>
#include <QTemporaryDir>

using namespace aqtruncat::metrics;

namespace {

template<typename T>
T *child(DashboardWindow &window, const char *name)
{
    T *result = window.findChild<T *>(QLatin1String(name));
    if (!result)
        qFatal("missing dashboard child: %s", name);
    return result;
}

SystemMetricsSnapshot populatedSnapshot()
{
    SystemMetricsSnapshot snapshot;
    snapshot.cpu.status = MetricStatus::okStatus();
    snapshot.cpu.usagePercent = 23.4;
    snapshot.cpu.logicalCpus = 8;

    MemoryInfo memory;
    memory.totalBytes = 32ull << 30;
    memory.usedBytes = 14ull << 30;
    memory.availableBytes = 18ull << 30;
    memory.usagePercent = 43.75;
    memory.swapPresent = true;
    memory.swapTotalBytes = 4ull << 30;
    memory.swapUsedBytes = 256ull << 20;
    memory.swapUsagePercent = 6.25;
    snapshot.memory.status = MetricStatus::okStatus();
    snapshot.memory.info = memory;

    snapshot.network.status = MetricStatus::okStatus();
    snapshot.network.downloadBytesPerSecond = 0.0;
    snapshot.network.uploadBytesPerSecond = 84.0 * 1024.0;

    StorageInfo root{"/", 200ull << 30, 120ull << 30, 70ull << 30, 63.2};
    StorageInfo home{"/home/alice", 900ull << 30, 360ull << 30, 500ull << 30, 41.9};
    snapshot.storage.push_back({MetricStatus::okStatus(), "/", root});
    snapshot.storage.push_back({MetricStatus::okStatus(), "/home/alice", home});

    snapshot.diskIo.status = MetricStatus::okStatus();
    snapshot.diskIo.readBytesPerSecond = 3.2 * 1024 * 1024;
    snapshot.diskIo.writeBytesPerSecond = 620.0 * 1024;
    return snapshot;
}

} // namespace

class TestDashboard : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void initialStateIsUnavailable()
    {
        DashboardWindow window("/home/alice");
        QCOMPARE(child<QLabel>(window, "cpuValue")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "memoryValue")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "networkDownload")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "diskRead")->text(), QStringLiteral("N/A"));
    }

    void displaysLiveMetrics()
    {
        DashboardWindow window("/home/alice");
        window.updateSnapshot(populatedSnapshot());
        QCOMPARE(child<QLabel>(window, "cpuValue")->text(), QStringLiteral("23.4 %"));
        QCOMPARE(child<QLabel>(window, "cpuDetail")->text(), QStringLiteral("8 logical CPUs"));
        QCOMPARE(child<QProgressBar>(window, "cpuProgress")->value(), 234);
        QCOMPARE(child<QLabel>(window, "memoryValue")->text(), QStringLiteral("43.8 %"));
        QVERIFY(child<QLabel>(window, "memoryDetail")->text().contains(QStringLiteral("14.0 GiB / 32.0 GiB")));
        QCOMPARE(child<QLabel>(window, "swapValue")->text(), QStringLiteral("256.0 MiB / 4.0 GiB"));
        QCOMPARE(child<QLabel>(window, "networkDownload")->text(), QStringLiteral("↓ 0 B/s"));
        QCOMPARE(child<QLabel>(window, "networkUpload")->text(), QStringLiteral("↑ 84.0 KiB/s"));
        QCOMPARE(child<QLabel>(window, "rootStorageValue")->text(), QStringLiteral("63.2 %"));
        QCOMPARE(child<QLabel>(window, "homeStorageValue")->text(), QStringLiteral("41.9 %"));
        QCOMPARE(child<QLabel>(window, "diskRead")->text(), QStringLiteral("↓ 3.2 MiB/s"));
        QCOMPARE(child<QLabel>(window, "diskWrite")->text(), QStringLiteral("↑ 620.0 KiB/s"));
    }

    void receivesSharedSamplerSnapshot()
    {
        DashboardWindow window("/home/alice");
        MetricsSources sources;
        sources.procRoot = "/definitely-not-present-aqtruncat";
        sources.sysRoot = "/definitely-not-present-aqtruncat";
        sources.storagePaths = {"/"};
        sources.statFilesystem = [](const std::string &, std::string *) -> std::optional<FilesystemStats> {
            return FilesystemStats{1, 1000, 500, 500};
        };
        MetricSampler sampler(nullptr, std::move(sources));
        connect(&sampler, &MetricSampler::snapshotReady, &window, &DashboardWindow::updateSnapshot);
        sampler.sampleNow();
        QCOMPARE(child<QLabel>(window, "cpuValue")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "rootStorageValue")->text(), QStringLiteral("50.0 %"));
    }

    void swapDisabledAndUnavailableSections()
    {
        DashboardWindow window("/home/alice");
        SystemMetricsSnapshot snapshot = populatedSnapshot();
        snapshot.memory.info->swapPresent = false;
        snapshot.network.status = MetricStatus::unavailable("no physical interface");
        snapshot.network.downloadBytesPerSecond.reset();
        snapshot.network.uploadBytesPerSecond.reset();
        snapshot.diskIo.status = MetricStatus::pending("first sample");
        snapshot.diskIo.readBytesPerSecond.reset();
        snapshot.diskIo.writeBytesPerSecond.reset();
        window.updateSnapshot(snapshot);
        QCOMPARE(child<QLabel>(window, "swapValue")->text(), QStringLiteral("Disabled"));
        QCOMPARE(child<QLabel>(window, "networkDownload")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "networkUpload")->text(), QStringLiteral("N/A"));
        QCOMPARE(child<QLabel>(window, "diskRead")->text(), QStringLiteral("Waiting…"));
        QCOMPARE(child<QLabel>(window, "diskWrite")->text(), QStringLiteral("Waiting…"));
    }

    void toggleAndCloseReuseTheSameWindow()
    {
        DashboardWindow window("/home/alice");
        QSignalSpy visibility(&window, &DashboardWindow::visibilityChanged);
        window.toggleVisible();
        QTRY_VERIFY(window.isVisible());
        window.toggleVisible();
        QTRY_VERIFY(!window.isVisible());
        window.toggleVisible();
        QTRY_VERIFY(window.isVisible());
        window.close();
        QTRY_VERIFY(!window.isVisible());
        window.toggleVisible();
        QTRY_VERIFY(window.isVisible());
        QVERIFY(visibility.count() >= 5);
    }

    void displaysCustomSnapshotsStatusesAndPreservesOrder()
    {
        DashboardWindow window("/home/alice");
        window.show(); // a hidden Dashboard defers custom card rebuilds
        aqtruncat::custommetrics::Snapshot snapshot;
        snapshot.title = QStringLiteral("Sensors");
        snapshot.symbol = QStringLiteral("🌡");
        snapshot.metricsBarValue = QStringLiteral("48°");
        snapshot.lastUpdatedDate = QDateTime::fromString(QStringLiteral("2026-06-05T04:50:40Z"), Qt::ISODate);
        snapshot.metrics = {{QStringLiteral("Temperature"), QStringLiteral("48.5 °C"), 0.485},
                            {QStringLiteral("Fan"), QStringLiteral("0 rpm"), std::nullopt}};
        aqtruncat::custommetrics::Source available;
        available.path = QStringLiteral("/tmp/sensors.json");
        available.state = aqtruncat::custommetrics::State::Available;
        available.snapshot = snapshot;
        aqtruncat::custommetrics::Source error;
        error.path = QStringLiteral("/tmp/broken.json");
        error.state = aqtruncat::custommetrics::State::Error;
        error.message = QStringLiteral("invalid JSON");

        window.updateCustomMetrics({available, error});
        QCOMPARE(child<QLabel>(window, "customTitle_0")->text(), QStringLiteral("🌡  Sensors"));
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("48.5 °C"));
        QCOMPARE(child<QLabel>(window, "customValue_0_1")->text(), QStringLiteral("0 rpm"));
        QCOMPARE(child<QProgressBar>(window, "customProgress_0_0")->value(), 485);
        QCOMPARE(child<QLabel>(window, "customStatus_1")->text(), QStringLiteral("Error"));
        QVERIFY(window.findChild<QScrollArea *>(QStringLiteral("dashboardScroll")));

        // A refresh with a valid value keeps the settled presentation.
        available.state = aqtruncat::custommetrics::State::Pending;
        window.updateCustomMetrics({available});
        QVERIFY(child<QLabel>(window, "customStatus_0")->text().startsWith(QStringLiteral("Last updated: ")));
        available.state = aqtruncat::custommetrics::State::Unavailable;
        window.updateCustomMetrics({available});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Unavailable"));
        window.updateCustomMetrics({});
        // Empty sections are hidden with their heading.
        QVERIFY(child<QWidget>(window, "customMetricsHeader")->isHidden());
        QVERIFY(child<QWidget>(window, "customMetricsContainer")->isHidden());
        QVERIFY(child<QWidget>(window, "integrationsHeader")->isHidden());
    }

    void integrationsAndCustomMetricsAreSeparateSections()
    {
        DashboardWindow window("/home/alice");
        window.show();
        auto source = [](const QString &path, const QString &title, const QString &integration) {
            aqtruncat::custommetrics::Snapshot snapshot;
            snapshot.title = title;
            snapshot.lastUpdatedDate = QDateTime::currentDateTimeUtc();
            snapshot.metrics.append({QStringLiteral("Value"), QStringLiteral("1"), std::nullopt});
            aqtruncat::custommetrics::Source result;
            result.id = path;
            result.path = path;
            result.state = aqtruncat::custommetrics::State::Available;
            result.integration = integration;
            result.snapshot = snapshot;
            return result;
        };
        // Service order: a user metric, the provider, a legacy M6 snapshot
        // whose title is the same as the provider's.
        const auto mine = source(QStringLiteral("/m/temperature/metric.json"), QStringLiteral("Temperature"), {});
        const auto claude = source(QStringLiteral("/m/claude-code/metric.json"), QStringLiteral("Claude Code"),
                                   QStringLiteral("claude-code"));
        const auto lookalike = source(QStringLiteral("/m/claude.json"), QStringLiteral("Claude Code"), {});
        window.updateCustomMetrics({mine, claude, lookalike});

        auto *integrations = child<QWidget>(window, "integrationsContainer");
        auto *custom = child<QWidget>(window, "customMetricsContainer");
        QVERIFY(!child<QWidget>(window, "integrationsHeader")->isHidden());
        QVERIFY(!child<QWidget>(window, "customMetricsHeader")->isHidden());
        QCOMPARE(child<QLabel>(window, "integrationsHeader")->text(), QStringLiteral("Integrations"));
        // Integrations first; each card exactly once, in its own section.
        QCOMPARE(child<QLabel>(window, "customTitle_0")->text(), QStringLiteral("Claude Code"));
        QCOMPARE(child<QWidget>(window, "customSource_0")->parentWidget(), integrations);
        QCOMPARE(child<QLabel>(window, "customTitle_1")->text(), QStringLiteral("Temperature"));
        QCOMPARE(child<QWidget>(window, "customSource_1")->parentWidget(), custom);
        QCOMPARE(child<QLabel>(window, "customTitle_2")->text(), QStringLiteral("Claude Code"));
        QCOMPARE(child<QWidget>(window, "customSource_2")->parentWidget(), custom);
        QVERIFY(!window.findChild<QWidget *>(QStringLiteral("customSource_3")));
        QCOMPARE(integrations->findChildren<QWidget *>(QRegularExpression(QStringLiteral("^customSource_"))).size(), 1);
        QCOMPARE(custom->findChildren<QWidget *>(QRegularExpression(QStringLiteral("^customSource_"))).size(), 2);

        // Only the provider: the Custom Metrics heading disappears.
        window.updateCustomMetrics({claude});
        QVERIFY(!child<QWidget>(window, "integrationsHeader")->isHidden());
        QVERIFY(child<QWidget>(window, "customMetricsHeader")->isHidden());
        // Only user metrics: no Integrations heading.
        window.updateCustomMetrics({mine, lookalike});
        QVERIFY(child<QWidget>(window, "integrationsHeader")->isHidden());
        QVERIFY(!child<QWidget>(window, "customMetricsHeader")->isHidden());

        // A hidden Dashboard applies the classification when shown.
        window.hide();
        window.updateCustomMetrics({claude});
        window.show();
        QVERIFY(child<QWidget>(window, "customMetricsHeader")->isHidden());
        QCOMPARE(child<QWidget>(window, "customSource_0")->parentWidget(), integrations);
    }

    void manyCustomSnapshotsStayInsideScrollableDashboard()
    {
        DashboardWindow window("/home/alice");
        window.show(); // a hidden Dashboard defers custom card rebuilds
        QList<aqtruncat::custommetrics::Source> sources;
        for (int i = 0; i < 30; ++i) {
            aqtruncat::custommetrics::Snapshot snapshot;
            snapshot.title = QStringLiteral("Metric %1").arg(i);
            snapshot.lastUpdatedDate = QDateTime::currentDateTimeUtc();
            snapshot.metrics.append({QStringLiteral("Value"), QString::number(i), std::nullopt});
            aqtruncat::custommetrics::Source source;
            source.path = QStringLiteral("/tmp/%1.json").arg(i);
            source.state = aqtruncat::custommetrics::State::Available;
            source.snapshot = snapshot;
            sources.append(source);
        }
        window.updateCustomMetrics(sources);
        QVERIFY(window.findChild<QWidget *>(QStringLiteral("customSource_29")));
        QVERIFY(window.findChild<QScrollArea *>(QStringLiteral("dashboardScroll"))->widgetResizable());
    }

    void structuredIconUsesFallbackWithoutInvalidatingMetric()
    {
        QTemporaryDir temporary;
        const QString brokenIcon = temporary.filePath(QStringLiteral("broken.svg"));
        QFile broken(brokenIcon);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        QCOMPARE(broken.write("not an image"), qint64(12));
        broken.close();

        DashboardWindow window("/home/alice");
        window.show(); // a hidden Dashboard defers custom card rebuilds
        aqtruncat::custommetrics::Snapshot snapshot;
        snapshot.title = QStringLiteral("Command metric");
        snapshot.icon = aqtruncat::custommetrics::IconReference{
            aqtruncat::custommetrics::IconType::File,
            QStringLiteral("./broken.svg"), brokenIcon};
        snapshot.metrics.append({QStringLiteral("Command metric"), QStringLiteral("23.4"), std::nullopt});
        snapshot.metricsBarValue = QStringLiteral("23.4");
        snapshot.lastUpdatedDate = QDateTime::currentDateTimeUtc();
        aqtruncat::custommetrics::Source source;
        source.path = QStringLiteral("/tmp/package/metric.json");
        source.type = aqtruncat::custommetrics::SourceType::Command;
        source.state = aqtruncat::custommetrics::State::Available;
        source.snapshot = snapshot;
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customTitle_0")->text(), QStringLiteral("Command metric"));
        QVERIFY(!child<QLabel>(window, "customIcon_0")->pixmap().isNull());
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("23.4"));
    }

    void commandRefreshDoesNotFlickerToWaiting()
    {
        namespace cm = aqtruncat::custommetrics;
        DashboardWindow window("/home/alice");
        window.show(); // a hidden Dashboard defers custom card rebuilds
        cm::Snapshot skeleton;
        skeleton.title = QStringLiteral("Temperature");
        cm::Source source;
        source.id = QStringLiteral("/tmp/package/metric.json");
        source.path = source.id;
        source.type = cm::SourceType::Command;

        // First execution: no valid value yet.
        source.state = cm::State::Pending;
        source.message = QStringLiteral("Waiting for the first command result.");
        source.snapshot = skeleton;
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Waiting…"));
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("customValue_0_0")));

        cm::Snapshot first = skeleton;
        first.metrics.append({QStringLiteral("Temperature"), QStringLiteral("23.4"), std::nullopt});
        first.lastUpdatedDate = QDateTime::fromString(QStringLiteral("2026-10-16T10:00:00Z"), Qt::ISODate);
        source.state = cm::State::Available;
        source.message.clear();
        source.snapshot = first;
        window.updateCustomMetrics({source});
        const QString firstFooter = child<QLabel>(window, "customStatus_0")->text();
        QVERIFY(firstFooter.startsWith(QStringLiteral("Last updated: ")));

        // Refresh running: internally Pending, visually unchanged.
        source.state = cm::State::Pending;
        source.message = QStringLiteral("Command is running.");
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("23.4"));
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), firstFooter);
        QVERIFY(child<QLabel>(window, "customStatus_0")->toolTip().isEmpty());

        // Successful refresh replaces the value.
        cm::Snapshot second = first;
        second.metrics.first().formattedValue = QStringLiteral("24.1");
        second.lastUpdatedDate = QDateTime::fromString(QStringLiteral("2026-10-16T10:05:00Z"), Qt::ISODate);
        source.state = cm::State::Available;
        source.message.clear();
        source.snapshot = second;
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("24.1"));
        QVERIFY(child<QLabel>(window, "customStatus_0")->text() != firstFooter);

        // Failed refresh exposes Error with the last valid value retained,
        // and the next refresh keeps showing Error until it settles.
        source.state = cm::State::Error;
        source.message = QStringLiteral("Command exited with status 1.");
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Error"));
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("24.1"));
        source.state = cm::State::Pending;
        source.message = QStringLiteral("Command is running.");
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Error"));
        QCOMPARE(child<QLabel>(window, "customStatus_0")->toolTip(),
                 QStringLiteral("Command exited with status 1."));

        source.state = cm::State::Unavailable;
        source.message = QStringLiteral("Executable is not available: ./missing");
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Unavailable"));

        // A redefinition resets to a skeleton: first execution again.
        source.state = cm::State::Pending;
        source.message = QStringLiteral("Waiting for the first command result.");
        source.snapshot = skeleton;
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Waiting…"));
    }

    void hiddenDashboardTracksSettledStateForDeferredRebuild()
    {
        namespace cm = aqtruncat::custommetrics;
        DashboardWindow window("/home/alice");
        cm::Snapshot snapshot;
        snapshot.title = QStringLiteral("Hidden");
        snapshot.metrics.append({QStringLiteral("Hidden"), QStringLiteral("1"), std::nullopt});
        cm::Source source;
        source.id = QStringLiteral("/tmp/hidden/metric.json");
        source.path = source.id;
        source.type = cm::SourceType::Command;
        source.snapshot = snapshot;
        source.state = cm::State::Error;
        source.message = QStringLiteral("timeout");
        window.updateCustomMetrics({source});
        source.state = cm::State::Pending;
        source.message = QStringLiteral("Command is running.");
        window.updateCustomMetrics({source});
        window.show();
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Error"));
    }

    void hiddenDashboardDefersCustomRebuildUntilShown()
    {
        DashboardWindow window("/home/alice");
        aqtruncat::custommetrics::Snapshot snapshot;
        snapshot.title = QStringLiteral("Deferred");
        snapshot.metrics.append({QStringLiteral("Deferred"), QStringLiteral("1"), std::nullopt});
        aqtruncat::custommetrics::Source source;
        source.path = QStringLiteral("/tmp/deferred.json");
        source.state = aqtruncat::custommetrics::State::Available;
        source.snapshot = snapshot;

        window.updateCustomMetrics({source});
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("customValue_0_0")));
        source.snapshot->metrics.first().formattedValue = QStringLiteral("2");
        window.updateCustomMetrics({source});
        window.show();
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("2"));

        source.snapshot->metrics.first().formattedValue = QStringLiteral("3");
        window.updateCustomMetrics({source});
        QCOMPARE(child<QLabel>(window, "customValue_0_0")->text(), QStringLiteral("3"));
    }
};

QTEST_MAIN(TestDashboard)
#include "tst_dashboard.moc"
