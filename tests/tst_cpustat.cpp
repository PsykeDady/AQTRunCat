#include "core/cpustat.h"

#include <QTest>

using aqtruncat::CpuSampler;
using aqtruncat::CpuTimes;
using aqtruncat::cpuBusyBetween;
using aqtruncat::parseProcStat;

namespace {

// Builds a sample from the eight fields that matter for utilisation.
CpuTimes times(std::uint64_t user, std::uint64_t nice, std::uint64_t system, std::uint64_t idle,
               std::uint64_t iowait = 0, std::uint64_t irq = 0, std::uint64_t softirq = 0,
               std::uint64_t steal = 0)
{
    return CpuTimes{user, nice, system, idle, iowait, irq, softirq, steal, 0, 0};
}

double busy(const CpuTimes &before, const CpuTimes &after)
{
    const auto delta = cpuBusyBetween(before, after);
    return delta ? delta->percent : -1.0;
}

} // namespace

class TestCpuStat : public QObject
{
    Q_OBJECT

private slots:
    // --- parsing -----------------------------------------------------------

    void parsesRealProcStat()
    {
        const char *text =
            "cpu  2350865 48 1271510 154132720 73802 20 68979 7 11 3\n"
            "cpu0 290000 6 160000 19260000 9000 2 8000 1 1 0\n"
            "intr 123456 0 0\n"
            "ctxt 987654\n";
        std::string error;
        const auto t = parseProcStat(text, &error);
        QVERIFY2(t.has_value(), error.c_str());
        QCOMPARE(t->user, 2350865u);
        QCOMPARE(t->nice, 48u);
        QCOMPARE(t->system, 1271510u);
        QCOMPARE(t->idle, 154132720u);
        QCOMPARE(t->iowait, 73802u);
        QCOMPARE(t->irq, 20u);
        QCOMPARE(t->softirq, 68979u);
        QCOMPARE(t->steal, 7u);
        QCOMPARE(t->guest, 11u);
        QCOMPARE(t->guestNice, 3u);
    }

    void picksAggregateLineNotPerCpuLines()
    {
        const auto t = parseProcStat("cpu0 1 2 3 4\ncpu 10 20 30 40\n");
        QVERIFY(t.has_value());
        QCOMPARE(t->user, 10u);
    }

    void acceptsOldKernelWithFourFields()
    {
        const auto t = parseProcStat("cpu 1 2 3 4\n");
        QVERIFY(t.has_value());
        QCOMPARE(t->idle, 4u);
        QCOMPARE(t->iowait, 0u);
        QCOMPARE(t->steal, 0u);
    }

    void ignoresUnknownTrailingFields()
    {
        const auto t = parseProcStat("cpu 1 2 3 4 5 6 7 8 9 10 11 12\n");
        QVERIFY(t.has_value());
        QCOMPARE(t->guestNice, 10u);
    }

    void acceptsLineWithoutNewlineAndWithTabs()
    {
        const auto t = parseProcStat("cpu\t1\t2 3 4\r");
        QVERIFY(t.has_value());
        QCOMPARE(t->nice, 2u);
    }

    void rejectsMalformedInput_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("empty") << QByteArray("");
        QTest::newRow("no cpu line") << QByteArray("intr 1 2 3\nctxt 4\n");
        QTest::newRow("only per-cpu lines") << QByteArray("cpu0 1 2 3 4\n");
        QTest::newRow("too few fields") << QByteArray("cpu 1 2 3\n");
        QTest::newRow("no fields") << QByteArray("cpu \n");
        QTest::newRow("non numeric") << QByteArray("cpu 1 x 3 4\n");
        QTest::newRow("negative") << QByteArray("cpu 1 -2 3 4\n");
        QTest::newRow("trailing garbage") << QByteArray("cpu 1 2 3 4abc\n");
        QTest::newRow("overflow") << QByteArray("cpu 1 2 3 99999999999999999999999\n");
    }

    void rejectsMalformedInput()
    {
        QFETCH(QByteArray, text);
        std::string error;
        QVERIFY(!parseProcStat(std::string_view(text.constData(), text.size()), &error).has_value());
        QVERIFY(!error.empty());
    }

    // --- delta semantics ---------------------------------------------------

    void idleSystemIsZero()
    {
        QCOMPARE(busy(times(100, 0, 50, 1000), times(100, 0, 50, 1400)), 0.0);
    }

    void fullLoadIsHundred()
    {
        QCOMPARE(busy(times(100, 0, 50, 1000), times(300, 0, 250, 1000)), 100.0);
    }

    void partialLoad()
    {
        // 25 user + 75 idle
        QCOMPARE(busy(times(0, 0, 0, 0), times(25, 0, 0, 75)), 25.0);
    }

    void niceCountsAsActive()
    {
        QCOMPARE(busy(times(0, 0, 0, 0), times(0, 100, 0, 0)), 100.0);
        QCOMPARE(busy(times(0, 0, 0, 0), times(0, 30, 0, 70)), 30.0);
    }

    void iowaitCountsAsIdle()
    {
        QCOMPARE(busy(times(0, 0, 0, 0), times(0, 0, 0, 0, 100)), 0.0);
        QCOMPARE(busy(times(0, 0, 0, 0), times(20, 0, 0, 40, 40)), 20.0);
    }

    void irqSoftirqAndStealCountAsActive()
    {
        // 10+10+10 user/nice/system, 5+5 irq/softirq, 10 steal = 50 active; 40 idle + 10 iowait
        QCOMPARE(busy(times(0, 0, 0, 0), times(10, 10, 10, 40, 10, 5, 5, 10)), 50.0);
    }

    void guestIsNotCountedTwice()
    {
        // The kernel adds guest time to both user and guest.
        CpuTimes before{};
        CpuTimes after{};
        after.user = 50;
        after.guest = 50;
        after.idle = 50;
        QCOMPARE(busy(before, after), 50.0);
    }

    void zeroDeltaHasNoValue()
    {
        const CpuTimes t = times(10, 1, 5, 100, 3);
        QVERIFY(!cpuBusyBetween(t, t).has_value());
    }

    void regressedCounterIsIgnoredAndFlagged()
    {
        // Known kernel behaviour: aggregate iowait can go backwards.
        const auto delta = cpuBusyBetween(times(0, 0, 0, 0, 500), times(50, 0, 0, 50, 400));
        QVERIFY(delta.has_value());
        QVERIFY(delta->counterRegressed);
        QCOMPARE(delta->percent, 50.0);
    }

    void allCountersBackwardsHasNoValue()
    {
        QVERIFY(!cpuBusyBetween(times(100, 100, 100, 100), times(1, 1, 1, 1)).has_value());
    }

    void resultIsAlwaysWithinRange()
    {
        const auto delta = cpuBusyBetween(times(0, 0, 0, 0), times(UINT64_MAX / 8, 0, 0, 0));
        QVERIFY(delta.has_value());
        QVERIFY(delta->percent >= 0.0 && delta->percent <= 100.0);
    }

    // --- sampler -------------------------------------------------------------

    void firstSampleHasNoValue()
    {
        CpuSampler sampler;
        QVERIFY(!sampler.add(times(10, 0, 10, 100)).has_value());
        const auto second = sampler.add(times(20, 0, 20, 120));
        QVERIFY(second.has_value());
        QCOMPARE(second->percent, 50.0);
    }

    void samplerRecoversAfterCounterReset()
    {
        CpuSampler sampler;
        sampler.add(times(1000, 0, 1000, 1000));
        QVERIFY(!sampler.add(times(1, 0, 1, 1)).has_value()); // everything went backwards
        const auto next = sampler.add(times(11, 0, 1, 11));   // measured from the new baseline
        QVERIFY(next.has_value());
        QCOMPARE(next->percent, 50.0);
    }

    void samplerKeepsBaselineOnZeroDelta()
    {
        CpuSampler sampler;
        sampler.add(times(0, 0, 0, 0));
        QVERIFY(!sampler.add(times(0, 0, 0, 0)).has_value());
        const auto next = sampler.add(times(30, 0, 0, 70));
        QVERIFY(next.has_value());
        QCOMPARE(next->percent, 30.0);
    }
};

QTEST_APPLESS_MAIN(TestCpuStat)
#include "tst_cpustat.moc"
