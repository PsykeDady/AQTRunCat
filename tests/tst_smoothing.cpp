#include "core/smoothing.h"
#include "core/speedcurve.h"

#include <QTest>

#include <cmath>

using aqtruncat::CpuSmoothingAlpha;
using aqtruncat::Ema;

class TestSmoothing : public QObject
{
    Q_OBJECT

private slots:
    void firstSampleInitialisesAverage()
    {
        Ema ema(CpuSmoothingAlpha);
        QVERIFY(!ema.value().has_value());
        QCOMPARE(ema.add(42.0), 42.0);
    }

    void stableInputStaysStable()
    {
        Ema ema(CpuSmoothingAlpha);
        for (int i = 0; i < 20; ++i)
            QCOMPARE(ema.add(30.0), 30.0);
    }

    void suddenIncreaseIsFollowedQuickly()
    {
        Ema ema(CpuSmoothingAlpha);
        ema.add(0.0);
        // s_n = 100 * (1 - 0.6^n) with alpha = 0.4
        QVERIFY(qFuzzyCompare(ema.add(100.0), 40.0));
        QVERIFY(qFuzzyCompare(ema.add(100.0), 64.0));
        ema.add(100.0);
        ema.add(100.0);
        QVERIFY(ema.add(100.0) > 92.0); // within 5 samples (≈5 s)
        // The runner visibly speeds up: from 10 FPS to more than 30 FPS.
        QVERIFY(aqtruncat::cpuToFps(*ema.value()) > 30);
    }

    void suddenDecreaseIsFollowedQuickly()
    {
        Ema ema(CpuSmoothingAlpha);
        ema.add(100.0);
        QVERIFY(qFuzzyCompare(ema.add(0.0), 60.0));
        for (int i = 0; i < 4; ++i)
            ema.add(0.0);
        QVERIFY(*ema.value() < 8.0);
        QCOMPARE(aqtruncat::cpuToFps(*ema.value()), aqtruncat::MinFps);
    }

    void smallOscillationsAreDamped()
    {
        Ema ema(CpuSmoothingAlpha);
        ema.add(50.0);
        double low = 100.0, high = 0.0;
        for (int i = 0; i < 40; ++i) {
            const double v = ema.add(i % 2 ? 54.0 : 46.0); // ±4 % jitter
            if (i >= 20) {
                low = std::min(low, v);
                high = std::max(high, v);
            }
        }
        // Steady-state amplitude is 8 * alpha / (2 - alpha) = 2 % peak-to-peak.
        QVERIFY(high - low < 2.1);
        QVERIFY(low > 48.5 && high < 51.5);
    }

    void dampedJitterDoesNotFlickerTheRunner()
    {
        Ema ema(CpuSmoothingAlpha);
        aqtruncat::FpsStabilizer stabilizer(aqtruncat::cpuToFps(50.0));
        ema.add(50.0);
        const int initial = stabilizer.current();
        int changes = 0;
        for (int i = 0; i < 60; ++i) {
            const int before = stabilizer.current();
            stabilizer.update(aqtruncat::cpuToFps(ema.add(i % 2 ? 54.0 : 46.0)));
            changes += stabilizer.current() != before;
        }
        QCOMPARE(changes, 0);
        QCOMPARE(stabilizer.current(), initial);
    }

    void convergesToNewLevel()
    {
        Ema ema(CpuSmoothingAlpha);
        ema.add(5.0);
        for (int i = 0; i < 25; ++i)
            ema.add(75.0);
        QVERIFY(std::abs(*ema.value() - 75.0) < 0.01);
    }
};

QTEST_APPLESS_MAIN(TestSmoothing)
#include "tst_smoothing.moc"
