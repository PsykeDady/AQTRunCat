#include "metrics/memory.h"

#include <QTest>

using aqtruncat::metrics::parseMeminfo;

namespace {

// Trimmed real /proc/meminfo (kernel 7.x), values in kB = KiB.
const char *RealMeminfo =
    "MemTotal:       32603624 kB\n"
    "MemFree:         2011712 kB\n"
    "MemAvailable:   17512800 kB\n"
    "Buffers:          612004 kB\n"
    "Cached:         14215004 kB\n"
    "SwapCached:        12464 kB\n"
    "Active:         15421644 kB\n"
    "Shmem:            900120 kB\n"
    "SReclaimable:    1104332 kB\n"
    "SwapTotal:      16649212 kB\n"
    "SwapFree:       16473980 kB\n"
    "HugePages_Total:       0\n"
    "Hugepagesize:       2048 kB\n";

} // namespace

class TestMemory : public QObject
{
    Q_OBJECT

private slots:
    void usesMemAvailable()
    {
        const auto m = parseMeminfo(RealMeminfo);
        QVERIFY(m);
        QCOMPARE(m->totalBytes, 32603624ull * 1024);
        QCOMPARE(m->availableBytes, 17512800ull * 1024);
        QCOMPARE(m->usedBytes, (32603624ull - 17512800ull) * 1024); // total - available, cache not "used"
        QVERIFY(qAbs(m->usagePercent - 100.0 * (32603624.0 - 17512800.0) / 32603624.0) < 1e-9);
        QVERIFY(!m->availableIsEstimate);
    }

    void swapIsSeparate()
    {
        const auto m = parseMeminfo(RealMeminfo);
        QVERIFY(m->swapPresent);
        QCOMPARE(m->swapTotalBytes, 16649212ull * 1024);
        QCOMPARE(m->swapUsedBytes, (16649212ull - 16473980ull) * 1024);
        QVERIFY(qAbs(m->swapUsagePercent - 100.0 * 175232.0 / 16649212.0) < 1e-9);
        // RAM figures do not include swap.
        QCOMPARE(m->totalBytes, 32603624ull * 1024);
    }

    void noSwap()
    {
        auto m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 400 kB\nMemAvailable: 600 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
        QVERIFY(m);
        QVERIFY(!m->swapPresent);
        QCOMPARE(m->swapUsedBytes, 0ull);
        m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 400 kB\nMemAvailable: 600 kB\n"); // no swap lines at all
        QVERIFY(m && !m->swapPresent);
    }

    void fallbackWithoutMemAvailable()
    {
        // Kernel < 3.14: free + buffers + cached + sreclaimable - shmem.
        const auto m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 100 kB\nBuffers: 50 kB\nCached: 300 kB\n"
                                    "SReclaimable: 40 kB\nShmem: 90 kB\n");
        QVERIFY(m);
        QVERIFY(m->availableIsEstimate);
        QCOMPARE(m->availableBytes, (100ull + 50 + 300 + 40 - 90) * 1024);
        QCOMPARE(m->usedBytes, (1000ull - 400) * 1024);
        QVERIFY(qAbs(m->usagePercent - 60.0) < 1e-9);
    }

    void fallbackIsClampedToTotal()
    {
        const auto m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 900 kB\nCached: 500 kB\n");
        QVERIFY(m);
        QCOMPARE(m->availableBytes, 1000ull * 1024);
        QCOMPARE(m->usedBytes, 0ull);
    }

    void availableAboveTotalIsClamped()
    {
        const auto m = parseMeminfo("MemTotal: 1000 kB\nMemFree: 10 kB\nMemAvailable: 1200 kB\n");
        QVERIFY(m);
        QCOMPARE(m->usedBytes, 0ull);
        QCOMPARE(m->usagePercent, 0.0);
    }

    void invalidInput_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("empty") << QByteArray("");
        QTest::newRow("no MemTotal") << QByteArray("MemFree: 10 kB\nMemAvailable: 10 kB\n");
        QTest::newRow("zero total") << QByteArray("MemTotal: 0 kB\nMemFree: 0 kB\n");
        QTest::newRow("no MemFree") << QByteArray("MemTotal: 1000 kB\nMemAvailable: 10 kB\n");
        QTest::newRow("malformed value") << QByteArray("MemTotal: lots kB\nMemFree: 1 kB\n");
        QTest::newRow("negative") << QByteArray("MemTotal: 1000 kB\nMemFree: -1 kB\n");
        QTest::newRow("malformed swap") << QByteArray("MemTotal: 1000 kB\nMemFree: 1 kB\nSwapTotal: 1x kB\n");
    }

    void invalidInput()
    {
        QFETCH(QByteArray, text);
        std::string error;
        QVERIFY(!parseMeminfo(std::string_view(text.constData(), text.size()), &error));
        QVERIFY(!error.empty());
    }

    void unknownMalformedFieldsAreIgnored()
    {
        QVERIFY(parseMeminfo("MemTotal: 1000 kB\nMemFree: 10 kB\nMemAvailable: 500 kB\nWeird: ??? kB\nno colon here\n"));
    }
};

QTEST_APPLESS_MAIN(TestMemory)
#include "tst_memory.moc"
