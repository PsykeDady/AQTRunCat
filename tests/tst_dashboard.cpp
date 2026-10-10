#include "app/dashboardwindow.h"
#include "app/metricsampler.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QSignalSpy>
#include <QScrollArea>
#include <QTest>

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

        available.state = aqtruncat::custommetrics::State::Pending;
        window.updateCustomMetrics({available});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Waiting…"));
        available.state = aqtruncat::custommetrics::State::Unavailable;
        window.updateCustomMetrics({available});
        QCOMPARE(child<QLabel>(window, "customStatus_0")->text(), QStringLiteral("Unavailable"));
        window.updateCustomMetrics({});
        QCOMPARE(child<QLabel>(window, "customMetricsEmpty")->text(), QStringLiteral("No custom metrics"));
    }

    void manyCustomSnapshotsStayInsideScrollableDashboard()
    {
        DashboardWindow window("/home/alice");
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
};

QTEST_MAIN(TestDashboard)
#include "tst_dashboard.moc"
