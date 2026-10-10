#include "app/cpumonitor.h"
#include "app/metricsampler.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

struct CpuFixture {
    QTemporaryDir temporary;
    QString proc = temporary.filePath(QStringLiteral("proc"));
    QString sys = temporary.filePath(QStringLiteral("sys"));

    CpuFixture()
    {
        QDir().mkpath(proc + QStringLiteral("/net"));
        QDir().mkpath(sys + QStringLiteral("/class/net"));
        QDir().mkpath(sys + QStringLiteral("/block"));
        write(QStringLiteral("meminfo"), "MemTotal: 1024 kB\nMemFree: 512 kB\nMemAvailable: 512 kB\n");
        write(QStringLiteral("net/dev"), "Inter-| Receive | Transmit\n face |bytes|bytes\n");
        write(QStringLiteral("diskstats"), "");
    }

    void cpu(int active, int idle)
    {
        write(QStringLiteral("stat"), "cpu " + QByteArray::number(active) + " 0 0 " + QByteArray::number(idle)
                                           + " 0 0 0 0 0 0\ncpu0 1 0 0 1 0 0 0 0 0 0\n");
    }

    void write(const QString &name, const QByteArray &contents)
    {
        QFile file(proc + u'/' + name);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(contents), contents.size());
    }

    aqtruncat::metrics::MetricsSources sources() const
    {
        aqtruncat::metrics::MetricsSources result;
        result.procRoot = proc.toStdString();
        result.sysRoot = sys.toStdString();
        result.storagePaths.clear();
        return result;
    }
};

} // namespace

class TestDesktopMetrics : public QObject
{
    Q_OBJECT

private slots:
    void sharedSnapshotFeedsRunnerCpu()
    {
        CpuFixture fixture;
        fixture.cpu(100, 100);
        MetricSampler sampler(nullptr, fixture.sources());
        CpuMonitor runnerCpu;
        int snapshots = 0;
        int runnerSamples = 0;
        double raw = -1.0;
        connect(&sampler, &MetricSampler::snapshotReady, &runnerCpu,
                [&runnerCpu, &snapshots](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                    ++snapshots;
                    runnerCpu.consume(snapshot.cpu);
                });
        connect(&runnerCpu, &CpuMonitor::sampled, this, [&](double value, double smoothed) {
            ++runnerSamples;
            raw = value;
            QCOMPARE(smoothed, value); // first M1 EMA sample initialises directly
        });

        sampler.sampleNow();
        QCOMPARE(snapshots, 1);
        QCOMPARE(runnerSamples, 0); // first CPU sample is Pending
        fixture.cpu(150, 150);
        sampler.sampleNow();
        QCOMPARE(snapshots, 2);
        QCOMPARE(runnerSamples, 1);
        QCOMPARE(raw, 50.0);
    }

    void unavailableCpuDoesNotChangeRunner()
    {
        CpuMonitor runnerCpu;
        int samples = 0;
        connect(&runnerCpu, &CpuMonitor::sampled, this, [&samples] { ++samples; });
        aqtruncat::metrics::CpuMetrics cpu;
        cpu.status = aqtruncat::metrics::MetricStatus::unavailable("missing");
        runnerCpu.consume(cpu);
        QCOMPARE(samples, 0);
    }

    void samplerLifecycleIsApplicationOwned()
    {
        CpuFixture fixture;
        fixture.cpu(100, 100);
        MetricSampler sampler(nullptr, fixture.sources(), std::chrono::milliseconds(10));
        int snapshots = 0;
        connect(&sampler, &MetricSampler::snapshotReady, this,
                [&snapshots](const aqtruncat::metrics::SystemMetricsSnapshot &) { ++snapshots; });
        sampler.start();
        QVERIFY(sampler.isActive());
        QCOMPARE(snapshots, 1); // immediate baseline
        QTRY_VERIFY_WITH_TIMEOUT(snapshots >= 2, 100);
        sampler.start(); // idempotent: no second immediate sample/timer
        sampler.stop();
        QVERIFY(!sampler.isActive());
        const int stoppedAt = snapshots;
        QTest::qWait(30);
        QCOMPARE(snapshots, stoppedAt);
    }
};

QTEST_GUILESS_MAIN(TestDesktopMetrics)
#include "tst_desktopmetrics.moc"
