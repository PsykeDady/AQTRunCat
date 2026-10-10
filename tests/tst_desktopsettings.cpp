#include "app/runnerselection.h"
#include "app/runnerspeedcontroller.h"
#include "app/snapshotthrottle.h"
#include "runner/runnerservice.h"
#include "runnerfixture.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

struct Roots {
    QTemporaryDir temporary;
    QString bundled = temporary.filePath(QStringLiteral("bundled"));
    QString user = temporary.filePath(QStringLiteral("user"));

    void add(const QString &root, const QString &id)
    {
        fixture::writeFrames(QDir(QDir(root).filePath(id)), fixture::galleryNames(id, 3));
    }

    aqtruncat::RunnerService service() const { return aqtruncat::RunnerService({bundled, user}); }
};

} // namespace

class TestDesktopSettings : public QObject
{
    Q_OBJECT

private slots:
    void runtimeSpeedModeSwitchesWithoutAnotherSampler()
    {
        RunnerSpeedController speed(aqtruncat::SpeedMode::Normal);
        QList<int> applied;
        connect(&speed, &RunnerSpeedController::fpsChanged, this,
                [&applied](int fps, int, double, double) { applied.append(fps); });
        QCOMPARE(speed.currentFps(), 10);
        speed.consume(0.0, 0.0);
        QCOMPARE(applied.last(), 10);
        speed.setMode(aqtruncat::SpeedMode::SlowDown);
        QCOMPARE(applied.last(), 40);
        speed.consume(100.0, 100.0);
        QCOMPARE(applied.last(), 10);
        speed.setMode(aqtruncat::SpeedMode::Normal);
        QCOMPARE(applied.last(), 40);
        speed.consume(50.0, 50.0);
        QCOMPARE(applied.last(), 18);
    }

    void dashboardThrottleDoesNotPoll()
    {
        SnapshotThrottle throttle(2);
        int emitted = 0;
        connect(&throttle, &SnapshotThrottle::snapshotReady, this,
                [&emitted](const aqtruncat::metrics::SystemMetricsSnapshot &) { ++emitted; });
        aqtruncat::metrics::SystemMetricsSnapshot snapshot;
        for (double time : {1.0, 2.0, 2.9, 3.0}) {
            snapshot.timeSeconds = time;
            throttle.submit(snapshot);
        }
        QCOMPARE(emitted, 2); // t=1 and t=3
        throttle.setIntervalSeconds(5);
        snapshot.timeSeconds = 3.1;
        throttle.submit(snapshot);
        QCOMPARE(emitted, 3); // setting change makes next shared snapshot immediate
    }

    void missingPersistedRunnerFallsBackAndRequestsRepair()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        const auto selected = selectStartupRunner(roots.service(), QStringLiteral("removed"),
                                                  QStringLiteral("cat"), false);
        QVERIFY(selected.choice.runner);
        QCOMPARE(selected.choice.runner->id, QStringLiteral("cat"));
        QVERIFY(selected.repairPersistedRunner);
    }

    void invalidButPresentRunnerFallsBackWithoutDestroyingSetting()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        QDir(roots.user).mkpath(QStringLiteral("temporarily-broken"));
        const auto selected = selectStartupRunner(roots.service(), QStringLiteral("temporarily-broken"),
                                                  QStringLiteral("cat"), false);
        QVERIFY(selected.choice.runner);
        QCOMPARE(selected.choice.runner->id, QStringLiteral("cat"));
        QVERIFY(!selected.repairPersistedRunner);
    }

    void explicitMissingRunnerNeverFallsBack()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        const auto selected = selectStartupRunner(roots.service(), QStringLiteral("missing"),
                                                  QStringLiteral("cat"), true);
        QVERIFY(!selected.choice.runner);
        QVERIFY(!selected.repairPersistedRunner);
    }
};

QTEST_GUILESS_MAIN(TestDesktopSettings)
#include "tst_desktopsettings.moc"

