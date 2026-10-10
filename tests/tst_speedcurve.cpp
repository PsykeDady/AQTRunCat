#include "core/speedcurve.h"

#include <QTest>

#include <limits>

using aqtruncat::FpsStabilizer;
using aqtruncat::MaxFps;
using aqtruncat::MinFps;
using aqtruncat::cpuToFps;
using aqtruncat::cpuToSlowFps;

class TestSpeedCurve : public QObject
{
    Q_OBJECT

private slots:
    void approvedValues_data()
    {
        QTest::addColumn<double>("cpu");
        QTest::addColumn<int>("fps");
        QTest::newRow("0%") << 0.0 << 10;
        QTest::newRow("10%") << 10.0 << 10;
        QTest::newRow("20%") << 20.0 << 11;
        QTest::newRow("30%") << 30.0 << 13;
        QTest::newRow("40%") << 40.0 << 15;
        QTest::newRow("50%") << 50.0 << 18;
        QTest::newRow("60%") << 60.0 << 21;
        QTest::newRow("70%") << 70.0 << 25;
        QTest::newRow("80%") << 80.0 << 29;
        QTest::newRow("90%") << 90.0 << 34;
        QTest::newRow("100%") << 100.0 << 40;
    }

    void approvedValues()
    {
        QFETCH(double, cpu);
        QFETCH(int, fps);
        QCOMPARE(cpuToFps(cpu), fps);
    }

    void slowDownValues_data()
    {
        QTest::addColumn<double>("cpu");
        QTest::addColumn<int>("fps");
        QTest::newRow("0%") << 0.0 << 40;
        QTest::newRow("10%") << 10.0 << 34;
        QTest::newRow("20%") << 20.0 << 29;
        QTest::newRow("30%") << 30.0 << 25;
        QTest::newRow("40%") << 40.0 << 21;
        QTest::newRow("50%") << 50.0 << 18;
        QTest::newRow("60%") << 60.0 << 15;
        QTest::newRow("70%") << 70.0 << 13;
        QTest::newRow("80%") << 80.0 << 11;
        QTest::newRow("90%") << 90.0 << 10;
        QTest::newRow("100%") << 100.0 << 10;
    }

    void slowDownValues()
    {
        QFETCH(double, cpu);
        QFETCH(int, fps);
        QCOMPARE(cpuToSlowFps(cpu), fps);
        QCOMPARE(cpuToFps(cpu, aqtruncat::SpeedMode::SlowDown), fps);
    }

    void boundsAndClamping()
    {
        QCOMPARE(MinFps, 10);
        QCOMPARE(MaxFps, 40);
        QCOMPARE(cpuToFps(-1.0), MinFps);
        QCOMPARE(cpuToFps(-1e9), MinFps);
        QCOMPARE(cpuToFps(100.1), MaxFps);
        QCOMPARE(cpuToFps(1e9), MaxFps);
        QCOMPARE(cpuToFps(std::numeric_limits<double>::quiet_NaN()), MinFps);
        QCOMPARE(cpuToFps(std::numeric_limits<double>::infinity()), MinFps);
        QCOMPARE(cpuToSlowFps(-1.0), MaxFps);
        QCOMPARE(cpuToSlowFps(100.1), MinFps);
        QCOMPARE(cpuToSlowFps(std::numeric_limits<double>::quiet_NaN()), MaxFps);
    }

    void monotonicWithinBounds()
    {
        int previous = cpuToFps(0.0);
        for (int i = 0; i <= 10000; ++i) {
            const int fps = cpuToFps(i / 100.0);
            QVERIFY2(fps >= previous, qPrintable(QStringLiteral("not monotonic at %1%").arg(i / 100.0)));
            QVERIFY(fps >= MinFps && fps <= MaxFps);
            previous = fps;
        }
    }

    // --- stabilizer --------------------------------------------------------

    void stabilizerKeepsSameTarget()
    {
        FpsStabilizer s(18);
        for (int i = 0; i < 5; ++i)
            QCOMPARE(s.update(18), 18);
    }

    void stabilizerAppliesLargeChangesImmediately()
    {
        FpsStabilizer s(10);
        QCOMPARE(s.update(25), 25);
        QCOMPARE(s.update(12), 12);
        QCOMPARE(s.update(40), 40);
    }

    void stabilizerIgnoresOneFpsFlicker()
    {
        FpsStabilizer s(18);
        const int pattern[] = {19, 18, 19, 18, 19, 19, 18, 19};
        for (int target : pattern)
            QCOMPARE(s.update(target), 18);
    }

    void stabilizerIgnoresTwoFpsFlicker()
    {
        // Observed under steady partial load on the steep part of the curve.
        FpsStabilizer s(26);
        const int pattern[] = {28, 26, 28, 27, 26, 28, 26};
        for (int target : pattern)
            QCOMPARE(s.update(target), 26);
    }

    void stabilizerAcceptsSustainedSmallChange()
    {
        FpsStabilizer s(11);
        QCOMPARE(s.update(10), 11);
        QCOMPARE(s.update(10), 11);
        QCOMPARE(s.update(10), 10); // third consecutive request

        FpsStabilizer t(26);
        QCOMPARE(t.update(28), 26);
        QCOMPARE(t.update(28), 26);
        QCOMPARE(t.update(28), 28);
    }

    void stabilizerRestartsConfirmationWhenTargetChanges()
    {
        FpsStabilizer s(18);
        QCOMPARE(s.update(19), 18);
        QCOMPARE(s.update(17), 18); // different 1-FPS target: count starts again
        QCOMPARE(s.update(17), 18);
        QCOMPARE(s.update(17), 17);
    }
};

QTEST_APPLESS_MAIN(TestSpeedCurve)
#include "tst_speedcurve.moc"
