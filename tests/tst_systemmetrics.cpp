#include "cli/commandline.h"
#include "cli/metricscommand.h"
#include "core/cpustat.h"
#include "metrics/systemmetrics.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

// A fake machine: proc/ and sys/ trees in a temporary directory, and fixed
// filesystem statistics instead of statvfs.
struct FakeMachine {
    QTemporaryDir tmp;
    QString proc = tmp.filePath(QStringLiteral("proc"));
    QString sys = tmp.filePath(QStringLiteral("sys"));

    FakeMachine()
    {
        QDir().mkpath(proc + QStringLiteral("/net"));
        touch(sys + QStringLiteral("/class/net/wlp2s0/device"));
        QDir().mkpath(sys + QStringLiteral("/class/net/docker0"));
        touch(sys + QStringLiteral("/block/nvme0n1/device"));
        QDir().mkpath(sys + QStringLiteral("/block/zram0"));
    }

    static void touch(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot create test fixture %s", qPrintable(f.fileName()));
    }

    void write(const QString &relative, const std::string &content) const
    {
        QFile f(proc + u'/' + relative);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qFatal("cannot create test fixture %s", qPrintable(f.fileName()));
        f.write(content.data(), qint64(content.size()));
    }

    // Writes one state of the machine. CPU: busy/idle jiffies; net: wlp2s0
    // rx/tx and docker0 rx; disk: sectors read/written on nvme0n1 and zram0 writes.
    void state(std::uint64_t busy, std::uint64_t idle, std::uint64_t rx, std::uint64_t tx, std::uint64_t dockerRx,
               std::uint64_t sectorsRead, std::uint64_t sectorsWritten, std::uint64_t zramWritten) const
    {
        write(QStringLiteral("stat"),
              "cpu  " + std::to_string(busy) + " 0 0 " + std::to_string(idle) + " 0 0 0 0 0 0\n"
              "cpu0 1 0 0 1 0 0 0 0 0 0\ncpu1 1 0 0 1 0 0 0 0 0 0\ncpu2 1 0 0 1 0 0 0 0 0 0\ncpu3 1 0 0 1 0 0 0 0 0 0\n"
              "intr 1\nctxt 2\n");
        write(QStringLiteral("meminfo"),
              "MemTotal: 16384000 kB\nMemFree: 1000000 kB\nMemAvailable: 9830400 kB\nSwapTotal: 4194304 kB\nSwapFree: 3145728 kB\n");
        write(QStringLiteral("net/dev"),
              "Inter-| Receive | Transmit\n face |bytes ...|bytes ...\n"
              "    lo: 999999 1 0 0 0 0 0 0 999999 1 0 0 0 0 0 0\n"
              "wlp2s0: " + std::to_string(rx) + " 1 0 0 0 0 0 0 " + std::to_string(tx) + " 1 0 0 0 0 0 0\n"
              "docker0: " + std::to_string(dockerRx) + " 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
        write(QStringLiteral("diskstats"),
              " 259 0 nvme0n1 1 0 " + std::to_string(sectorsRead) + " 0 1 0 " + std::to_string(sectorsWritten) + " 0 0 0 0 0 0 0 0 0 0\n"
              " 259 7 nvme0n1p7 1 0 " + std::to_string(sectorsRead) + " 0 1 0 " + std::to_string(sectorsWritten) + " 0 0 0 0 0 0 0 0 0 0\n"
              " 251 0 zram0 1 0 0 0 1 0 " + std::to_string(zramWritten) + " 0 0 0 0 0 0 0 0 0 0\n");
    }

    MetricsSources sources() const
    {
        MetricsSources s;
        s.procRoot = proc.toStdString();
        s.sysRoot = sys.toStdString();
        s.storagePaths = {"/"};
        s.statFilesystem = [](const std::string &, std::string *) -> std::optional<FilesystemStats> {
            return FilesystemStats{4096, 51328506, 21307900, 18682364};
        };
        // F5 sources: nothing of the host (no /etc, no GPU tools, no MangoHud).
        s.osRoot = tmp.filePath(QStringLiteral("root")).toStdString();
        s.kernel = [](std::string *) -> std::optional<KernelInfo> {
            return KernelInfo{"Linux", "6.6.0-fixture", "#1 SMP", "x86_64"};
        };
        s.pciIdsPaths = {};
        s.nvml = nullptr;
        s.fpsSource = FpsSourceOff;
        return s;
    }
};

} // namespace

class TestSystemMetrics : public QObject
{
    Q_OBJECT

private slots:
    void fixturesToSnapshotToReport()
    {
        FakeMachine m;
        SystemMetricsSampler sampler(m.sources());

        m.state(1000, 3000, 1000000, 500000, 0, 0, 0, 0);
        const SystemMetricsSnapshot first = sampler.sample(100.0);
        QCOMPARE(first.cpu.status.state, MetricState::Pending); // rates need two samples
        QCOMPARE(first.network.status.state, MetricState::Pending);
        QCOMPARE(first.diskIo.status.state, MetricState::Pending);
        QVERIFY(first.memory.status.ok()); // instantaneous values are there at once
        QVERIFY(first.storage.at(0).status.ok());

        // 2 s later: CPU 25 % busy, 2 MiB down / 64 KiB up, 4 MiB read / 1 MiB written.
        m.state(1000 + 50, 3000 + 150, 1000000 + 2097152 * 2, 500000 + 65536 * 2, 7777777, 16384, 4096, 999999);
        const SystemMetricsSnapshot s = sampler.sample(102.0);

        QVERIFY(s.cpu.status.ok());
        QCOMPARE(*s.cpu.usagePercent, 25.0);
        QCOMPARE(s.cpu.logicalCpus, 4);

        QCOMPARE(s.memory.info->usedBytes, (16384000ull - 9830400ull) * 1024);
        QCOMPARE(s.memory.info->swapUsedBytes, 1048576ull * 1024);

        QVERIFY(s.network.status.ok());
        QCOMPARE(*s.network.downloadBytesPerSecond, 2097152.0); // docker0 and lo excluded
        QCOMPARE(*s.network.uploadBytesPerSecond, 65536.0);
        QCOMPARE(s.network.countedInterfaces, 1);
        QCOMPARE(s.network.interfaces.size(), 3u);

        QVERIFY(s.diskIo.status.ok());
        QCOMPARE(*s.diskIo.readBytesPerSecond, 16384.0 * 512 / 2); // 4 MiB/s; nvme0n1p7 and zram not added
        QCOMPARE(*s.diskIo.writeBytesPerSecond, 4096.0 * 512 / 2);

        const QString report = aqtruncat::cli::formatMetricsReport(s);
        for (const char *expected : {"CPU\n  Usage:      25.0 %  (4 logical CPUs)",
                                     "Used:       6.2 GiB / 15.6 GiB  (40.0 %)",
                                     "Available:  9.4 GiB",
                                     "Swap\n  Used:       1.0 GiB / 4.0 GiB  (25.0 %)",
                                     "Network (wlp2s0)\n  Download:   2.0 MiB/s\n  Upload:     64.0 KiB/s",
                                     "Storage /\n  Used:       114.5 GiB / 195.8 GiB  (61.6 %)",
                                     "Disk I/O (nvme0n1)\n  Read:       4.0 MiB/s\n  Write:      1.0 MiB/s",
                                     "Uptime\n  n/a: ",
                                     "GPU\n  n/a: no GPU found",
                                     "FPS\n  n/a: the FPS source is turned off",
                                     "System\n  Kernel:     Linux 6.6.0-fixture (x86_64)\n  n/a: no os-release"})
            QVERIFY2(report.contains(QLatin1String(expected)), qPrintable(QStringLiteral("missing \"%1\" in:\n%2").arg(QLatin1String(expected), report)));

        const QString line = aqtruncat::cli::formatMetricsLine(s, QStringLiteral("12:00:00"));
        QCOMPARE(line, QStringLiteral("12:00:00  CPU 25.0 % | Mem 6.2 GiB (40.0 %) | Net down 2.0 MiB/s up 64.0 KiB/s | "
                                      "Disk read 4.0 MiB/s write 1.0 MiB/s"));
    }

    void cpuUsesTheRunnerCalculation()
    {
        // The metrics layer and the runner share core/cpustat: same numbers.
        FakeMachine m;
        SystemMetricsSampler sampler(m.sources());
        m.state(200, 800, 0, 0, 0, 0, 0, 0);
        sampler.sample(1.0);
        m.state(270, 830, 0, 0, 0, 0, 0, 0);
        const auto s = sampler.sample(2.0);
        const auto expected = aqtruncat::cpuBusyBetween(*aqtruncat::parseProcStat("cpu 200 0 0 800"),
                                                        *aqtruncat::parseProcStat("cpu 270 0 0 830"));
        QCOMPARE(*s.cpu.usagePercent, expected->percent);
    }

    void missingSourcesAreUnavailableNotZero()
    {
        FakeMachine m;
        MetricsSources sources = m.sources();
        sources.procRoot = m.tmp.filePath(QStringLiteral("empty")).toStdString();
        sources.statFilesystem = [](const std::string &path, std::string *error) -> std::optional<FilesystemStats> {
            *error = path + ": No such file or directory";
            return std::nullopt;
        };
        SystemMetricsSampler sampler(sources);
        sampler.sample(1.0);
        const auto s = sampler.sample(2.0);
        QCOMPARE(s.cpu.status.state, MetricState::Unavailable);
        QCOMPARE(s.memory.status.state, MetricState::Unavailable);
        QCOMPARE(s.network.status.state, MetricState::Unavailable);
        QCOMPARE(s.diskIo.status.state, MetricState::Unavailable);
        QCOMPARE(s.storage.at(0).status.state, MetricState::Unavailable);
        QVERIFY(!s.cpu.usagePercent && !s.network.downloadBytesPerSecond && !s.memory.info);

        const QString report = aqtruncat::cli::formatMetricsReport(s);
        QVERIFY(report.contains(QStringLiteral("n/a:")));
        QVERIFY(!report.contains(QStringLiteral("0 B/s")));
        QVERIFY(!report.contains(QStringLiteral("0.0 %")));
    }

    void oneBrokenSourceDoesNotHideTheOthers()
    {
        FakeMachine m;
        SystemMetricsSampler sampler(m.sources());
        m.state(100, 100, 0, 0, 0, 0, 0, 0);
        m.write(QStringLiteral("meminfo"), "MemTotal: garbage\n");
        sampler.sample(1.0);
        m.state(150, 150, 1024, 0, 0, 0, 0, 0);
        m.write(QStringLiteral("meminfo"), "MemTotal: garbage\n");
        const auto s = sampler.sample(2.0);
        QCOMPARE(s.memory.status.state, MetricState::Error);
        QVERIFY(s.cpu.status.ok());
        QVERIFY(s.network.status.ok());
        QVERIFY(s.diskIo.status.ok());
        QVERIFY(aqtruncat::cli::formatMetricsReport(s).contains(QStringLiteral("Memory\n  n/a:")));
    }

    void metricsCommandLine()
    {
        using namespace aqtruncat::cli;
        auto parsed = parseCommandLine({QStringLiteral("metrics")});
        QVERIFY(parsed.invocation);
        QCOMPARE(parsed.invocation->command, Command::Metrics);
        QVERIFY(!parsed.invocation->watch);
        parsed = parseCommandLine({QStringLiteral("metrics"), QStringLiteral("--watch")});
        QVERIFY(parsed.invocation && parsed.invocation->watch);
        parsed = parseCommandLine({QStringLiteral("metrics"), QStringLiteral("--help")});
        QCOMPARE(parsed.invocation->command, Command::Help);
        QVERIFY(parsed.invocation->helpText.contains(QStringLiteral("Usage: aqtruncat metrics")));
        QVERIFY(mainHelp().contains(QStringLiteral("aqtruncat metrics")));
        QVERIFY(!parseCommandLine({QStringLiteral("metrics"), QStringLiteral("--json")}).invocation);
        QVERIFY(!parseCommandLine({QStringLiteral("metrics"), QStringLiteral("cpu")}).invocation);
    }

    void metricsCommandAcceptsStorageOnly()
    {
        using namespace aqtruncat::cli;

        FakeMachine m;
        MetricsSources sources = m.sources();
        sources.procRoot = m.tmp.filePath(QStringLiteral("missing-proc")).toStdString();
        sources.sysRoot = m.tmp.filePath(QStringLiteral("missing-sys")).toStdString();

        Invocation invocation;
        invocation.command = Command::Metrics;
        QString output;
        QString errors;
        QTextStream out(&output);
        QTextStream err(&errors);

        QCOMPARE(runMetrics(invocation, out, err, sources), ExitOk);
        QVERIFY(output.contains(QStringLiteral("Storage /")));
        QVERIFY(output.contains(QStringLiteral("61.6 %")));
        QVERIFY(errors.isEmpty());
    }

    void networkPolicyChangesInsideSamplerAndRebaselines()
    {
        FakeMachine m;
        SystemMetricsSampler sampler(m.sources());
        m.state(100, 100, 1000, 1000, 5000000, 0, 0, 0);
        sampler.sample(1.0);
        m.state(150, 150, 1200, 1100, 6000000, 0, 0, 0);
        const auto automatic = sampler.sample(2.0);
        QVERIFY(automatic.network.status.ok());
        QCOMPARE(*automatic.network.downloadBytesPerSecond, 200.0); // wlan only

        sampler.setNetworkSelection({NetworkPolicyMode::Manual, {"docker0"}});
        m.state(200, 200, 1300, 1200, 7000000, 0, 0, 0);
        QCOMPARE(sampler.sample(3.0).network.status.state, MetricState::Pending);
        m.state(250, 250, 1400, 1300, 7000250, 0, 0, 0);
        const auto manual = sampler.sample(4.0);
        QVERIFY(manual.network.status.ok());
        QCOMPARE(*manual.network.downloadBytesPerSecond, 250.0);
        QCOMPARE(manual.network.countedInterfaces, 1);

        sampler.setNetworkSelection({NetworkPolicyMode::Manual, {"tun0"}});
        m.state(300, 300, 1500, 1400, 7000300, 0, 0, 0);
        QCOMPARE(sampler.sample(5.0).network.status.state, MetricState::Pending);
        m.state(350, 350, 1600, 1500, 7000350, 0, 0, 0);
        const auto absent = sampler.sample(6.0);
        QCOMPARE(absent.network.status.state, MetricState::Unavailable);
        QVERIFY(absent.network.status.message.find("selected") != std::string::npos);
    }
};

QTEST_GUILESS_MAIN(TestSystemMetrics)
#include "tst_systemmetrics.moc"
