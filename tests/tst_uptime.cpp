#include "metrics/systemmetrics.h"
#include "metrics/uptime.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

// F5.1: /proc/uptime parsing and the duration formats.
class TestUptime : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheFirstField()
    {
        QCOMPARE(parseProcUptime("12345.67 98765.43\n"), std::optional<double>(12345.67));
        QCOMPARE(parseProcUptime("0.00 0.00"), std::optional<double>(0.0));
        QCOMPARE(parseProcUptime("  42.5\n"), std::optional<double>(42.5));
    }

    void rejectsMalformedValues()
    {
        std::string error;
        QVERIFY(!parseProcUptime("", &error));
        QVERIFY(!error.empty());
        QVERIFY(!parseProcUptime("abc 1.0"));
        QVERIFY(!parseProcUptime("-5.0 1.0"));
        QVERIFY(!parseProcUptime("12x 1.0"));
        QVERIFY(!parseProcUptime("nan 1.0"));
        QVERIFY(!parseProcUptime("inf 1.0"));
    }

    void formatsDaysHoursMinutes_data()
    {
        QTest::addColumn<double>("seconds");
        QTest::addColumn<QString>("full");
        QTest::addColumn<QString>("compact");
        QTest::newRow("zero") << 0.0 << "0m" << "0m";
        QTest::newRow("59 s, truncated") << 59.9 << "0m" << "0m";
        QTest::newRow("one minute") << 60.0 << "1m" << "1m";
        QTest::newRow("59 min") << 3599.0 << "59m" << "59m";
        QTest::newRow("one hour") << 3600.0 << "1h 0m" << "1h";
        QTest::newRow("23h59m") << 86399.0 << "23h 59m" << "23h";
        QTest::newRow("one day") << 86400.0 << "1d 0h 0m" << "1d";
        QTest::newRow("1d 1h 1m") << 90061.0 << "1d 1h 1m" << "1d";
        QTest::newRow("400 days") << 400.0 * 86400 + 5 * 3600 << "400d 5h 0m" << "400d";
        QTest::newRow("saturates the compact form") << 5000.0 * 86400 << "5000d 0h 0m" << "999d";
    }
    void formatsDaysHoursMinutes()
    {
        QFETCH(double, seconds);
        QFETCH(QString, full);
        QFETCH(QString, compact);
        QCOMPARE(QString::fromStdString(formatDuration(seconds)), full);
        QCOMPARE(QString::fromStdString(compactDuration(seconds)), compact);
        QVERIFY(compactDuration(seconds).size() <= 4);
    }

    void invalidDurationsHaveNoText()
    {
        QVERIFY(formatDuration(-1.0).empty());
        QVERIFY(compactDuration(std::numeric_limits<double>::quiet_NaN()).empty());
        QVERIFY(formatDuration(std::numeric_limits<double>::infinity()).empty());
    }

    void samplerReadsUptimeFromProc()
    {
        // System uptime, never the process's: the value comes from <proc>/uptime.
        QTemporaryDir proc;
        QFile file(proc.filePath(QStringLiteral("uptime")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("7384.12 1234.00\n");
        file.close();
        MetricsSources sources;
        sources.procRoot = proc.path().toStdString();
        sources.sysRoot = proc.path().toStdString() + "/nosys";
        sources.osRoot = proc.path().toStdString();
        sources.fpsSource = FpsSourceOff;
        SystemMetricsSampler sampler(sources);
        const SystemMetricsSnapshot snapshot = sampler.sample(1.0);
        QVERIFY(snapshot.uptime.status.ok());
        QCOMPARE(*snapshot.uptime.seconds, 7384.12);

        QVERIFY(QFile::remove(proc.filePath(QStringLiteral("uptime"))));
        const SystemMetricsSnapshot missing = sampler.sample(2.0);
        QCOMPARE(missing.uptime.status.state, MetricState::Unavailable);
        QVERIFY(!missing.uptime.seconds);
    }
};

QTEST_GUILESS_MAIN(TestUptime)
#include "tst_uptime.moc"
