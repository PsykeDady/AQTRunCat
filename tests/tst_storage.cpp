#include "metrics/storage.h"

#include <QTest>

using namespace aqtruncat::metrics;

class TestStorage : public QObject
{
    Q_OBJECT

private slots:
    void dfSemantics()
    {
        // Real ext4 root of the development machine (statvfs).
        const FilesystemStats stats{4096, 51328506, 21307900, 18682364};
        const auto s = computeStorage("/", stats);
        QVERIFY(s);
        QCOMPARE(s->totalBytes, 51328506ull * 4096);                      // 210241560576, df "1B-blocks"
        QCOMPARE(s->usedBytes, (51328506ull - 21307900ull) * 4096);       // 122964402176, df "Used"
        QCOMPARE(s->availableBytes, 18682364ull * 4096);                  // 76522962944, df "Available"
        // df Use% = used / (used + available) = 61.6 % (df rounds up to 62 %).
        QVERIFY(qAbs(s->usagePercent - 100.0 * 122964402176.0 / (122964402176.0 + 76522962944.0)) < 1e-9);
    }

    void reservedBlocksAreNeitherUsedNorAvailable()
    {
        // 100 blocks, 30 free of which 5 reserved for root.
        const auto s = computeStorage("/data", FilesystemStats{1000, 100, 30, 25});
        QVERIFY(s);
        QCOMPARE(s->usedBytes, 70000ull);
        QCOMPARE(s->availableBytes, 25000ull);
        QVERIFY(s->usedBytes + s->availableBytes < s->totalBytes);
        QVERIFY(qAbs(s->usagePercent - 100.0 * 70.0 / 95.0) < 1e-9);
    }

    void fullAndEmpty()
    {
        QCOMPARE(computeStorage("/", FilesystemStats{4096, 100, 0, 0})->usagePercent, 100.0);
        QCOMPARE(computeStorage("/", FilesystemStats{4096, 100, 100, 100})->usagePercent, 0.0);
        // Only root-reserved space left: the user cannot write, 100 %.
        QCOMPARE(computeStorage("/", FilesystemStats{4096, 100, 5, 0})->usagePercent, 100.0);
    }

    void invalidStatistics_data()
    {
        QTest::addColumn<quint64>("fragment");
        QTest::addColumn<quint64>("blocks");
        QTest::addColumn<quint64>("free");
        QTest::addColumn<quint64>("available");
        QTest::newRow("no fragment size") << quint64(0) << quint64(100) << quint64(10) << quint64(10);
        QTest::newRow("no blocks (pseudo fs)") << quint64(4096) << quint64(0) << quint64(0) << quint64(0);
        QTest::newRow("free > total") << quint64(4096) << quint64(100) << quint64(200) << quint64(10);
        QTest::newRow("available > free") << quint64(4096) << quint64(100) << quint64(10) << quint64(20);
        QTest::newRow("overflow") << quint64(1) << quint64(0) << quint64(0) << quint64(0);
    }

    void invalidStatistics()
    {
        QFETCH(quint64, fragment);
        QFETCH(quint64, blocks);
        QFETCH(quint64, free);
        QFETCH(quint64, available);
        if (QByteArray(QTest::currentDataTag()) == "overflow") {
            fragment = 1ull << 20;
            blocks = ~0ull;
            free = available = 0;
        }
        std::string error;
        QVERIFY(!computeStorage("/", FilesystemStats{fragment, blocks, free, available}, &error));
        QVERIFY(!error.empty());
    }

    void queryReportsMissingPaths()
    {
        std::string error;
        QVERIFY(!queryFilesystem("/nonexistent/aqtruncat/path", &error));
        QVERIFY(error.find("/nonexistent") != std::string::npos);
    }
};

QTEST_APPLESS_MAIN(TestStorage)
#include "tst_storage.moc"
