#include "metrics/diskio.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

// /proc/diskstats line with the given cumulative sectors (20 fields, kernel ≥ 5.5).
std::string line(const std::string &name, std::uint64_t sectorsRead, std::uint64_t sectorsWritten)
{
    return " 259       0 " + name + " 100 0 " + std::to_string(sectorsRead) + " 50 200 0 " + std::to_string(sectorsWritten)
        + " 80 0 120 130 0 0 0 0 0 0\n";
}

// /sys/block look-alike: whole disks appear here, physical ones have "device".
struct SysBlock {
    QTemporaryDir tmp;
    std::filesystem::path root{tmp.path().toStdString()};

    void add(const std::string &name, bool device, bool hidden = false)
    {
        const QDir dir(QString::fromStdString((root / name).string()));
        dir.mkpath(QStringLiteral("."));
        if (device) {
            QFile f(dir.filePath(QStringLiteral("device")));
            if (!f.open(QIODevice::WriteOnly))
                qFatal("cannot create test fixture %s", qPrintable(f.fileName()));
        }
        QFile h(dir.filePath(QStringLiteral("hidden")));
        if (!h.open(QIODevice::WriteOnly))
            qFatal("cannot create test fixture %s", qPrintable(h.fileName()));
        h.write(hidden ? "1\n" : "0\n");
    }
};

struct DiskTracker {
    SysBlock sys;
    CounterRateTracker tracker;

    AggregateRates update(const std::string &diskstats, double time)
    {
        return tracker.update(*parseDiskstats(diskstats), time, [this](const std::string &name) {
            return countsTowardsDiskTotal(classifyBlockDevice(name, sys.root));
        });
    }
};

// The development machine: two NVMe disks, partitions and zram swap.
std::string machine(std::uint64_t nvme0Read, std::uint64_t nvme0Written, std::uint64_t nvme1Written, std::uint64_t zramWritten)
{
    return line("nvme1n1", 1000, nvme1Written) + line("nvme1n1p1", 10, 0) + line("nvme1n1p2", 990, nvme1Written)
        + line("nvme0n1", nvme0Read, nvme0Written) + line("nvme0n1p7", nvme0Read, nvme0Written)
        + line("zram0", 0, zramWritten);
}

} // namespace

class TestDiskIo : public QObject
{
    Q_OBJECT

private slots:
    void parsesSectorsAsBytes()
    {
        const auto c = parseDiskstats(line("nvme0n1", 9633954, 93725856));
        QVERIFY(c);
        QCOMPARE(c->at("nvme0n1").first, 9633954ull * 512);
        QCOMPARE(c->at("nvme0n1").second, 93725856ull * 512);
    }

    void acceptsOldAndNewFieldCounts()
    {
        // 14 fields (kernels < 4.18) and 18/20 fields (newer) all work.
        QVERIFY(parseDiskstats("   8       0 sda 1 0 16 0 2 0 32 0 0 0 0\n"));
        QVERIFY(parseDiskstats(line("sda", 16, 32)));
    }

    void rejectsMalformedLines()
    {
        std::string error;
        QVERIFY(!parseDiskstats("   8 0 sda 1 2\n", &error));
        QVERIFY(!parseDiskstats("   8 0 sda 1 0 x 0 2 0 32 0 0 0 0\n", &error));
        QVERIFY(!error.empty());
        QVERIFY(parseDiskstats("")->empty());
    }

    void classifiesDevices()
    {
        SysBlock sys;
        sys.add("nvme0n1", true);
        sys.add("sda", true);
        sys.add("mmcblk0", true);
        sys.add("nvme0c0n1", true, true); // hidden NVMe multipath path
        sys.add("dm-0", false);
        sys.add("md0", false);
        sys.add("loop0", false);
        sys.add("zram0", false);
        for (const char *name : {"nvme0n1", "sda", "mmcblk0"})
            QCOMPARE(classifyBlockDevice(name, sys.root), BlockDeviceKind::PhysicalDisk);
        // Partitions are not directories under /sys/block.
        for (const char *name : {"nvme0c0n1", "dm-0", "md0", "loop0", "zram0", "nvme0n1p7", "sda1"})
            QCOMPARE(classifyBlockDevice(name, sys.root), BlockDeviceKind::Other);
    }

    void firstSampleIsPending()
    {
        DiskTracker t;
        t.sys.add("nvme0n1", true);
        const auto r = t.update(line("nvme0n1", 0, 0), 1.0);
        QCOMPARE(r.status.state, MetricState::Pending);
        QVERIFY(!r.firstPerSecond);
    }

    void ratesUseElapsedTime()
    {
        DiskTracker t;
        t.sys.add("nvme0n1", true);
        t.update(line("nvme0n1", 0, 0), 10.0);
        const auto r = t.update(line("nvme0n1", 2048, 4096), 12.0); // 1 MiB read, 2 MiB written in 2 s
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 512.0 * 1024);
        QCOMPARE(*r.secondPerSecond, 1024.0 * 1024);
    }

    void partitionsAndVirtualDevicesAreNotCountedTwice()
    {
        DiskTracker t;
        t.sys.add("nvme0n1", true);
        t.sys.add("nvme1n1", true);
        t.sys.add("zram0", false);
        t.update(machine(0, 0, 0, 0), 1.0);
        // 100 sectors written to nvme0n1 (through p7), 300 to nvme1n1 (through p2),
        // 5000 to zram (memory, not disk).
        const auto r = t.update(machine(0, 100, 300, 5000), 2.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.secondPerSecond, 400.0 * 512); // disks only, partitions not added again
        QCOMPARE(r.countedSources, 2);
    }

    void counterResetAndDeviceChanges()
    {
        DiskTracker t;
        t.sys.add("sda", true);
        t.sys.add("sdb", true);
        t.update(line("sda", 1000000, 1000000), 1.0);
        // sda removed and plugged in again (counters restart), sdb appears.
        auto r = t.update(line("sda", 10, 10) + line("sdb", 500, 500), 2.0);
        QCOMPARE(r.status.state, MetricState::Pending); // nothing measurable this interval
        r = t.update(line("sda", 20, 10) + line("sdb", 500, 1500), 3.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 10.0 * 512);
        QCOMPARE(*r.secondPerSecond, 1000.0 * 512);
        r = t.update(line("sdb", 500, 1600), 4.0); // sda unplugged
        QCOMPARE(*r.secondPerSecond, 100.0 * 512);
        QCOMPARE(r.countedSources, 1);
    }

    void noPhysicalDiskIsUnavailable()
    {
        DiskTracker t;
        t.sys.add("vda", false); // e.g. a container without /sys/block device links
        t.update(line("vda", 0, 0), 1.0);
        QCOMPARE(t.update(line("vda", 10, 10), 2.0).status.state, MetricState::Unavailable);
    }
};

QTEST_GUILESS_MAIN(TestDiskIo)
#include "tst_diskio.moc"
