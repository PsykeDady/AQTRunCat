#include "runner/runneranimator.h"

#include <QSignalSpy>
#include <QTest>

using aqtruncat::RunnerAnimator;

class TestRunnerAnimator : public QObject
{
    Q_OBJECT

private slots:
    void announcesFirstFrameOfANewRunner()
    {
        RunnerAnimator animator;
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        animator.setSequence({0, 1, 2, 3, 4});
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 0);
    }

    void advancesAndWrapsAtTheFrameRate()
    {
        RunnerAnimator animator;
        animator.setSequence({0, 1, 2});
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        animator.setFps(40);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 4, 2000);
        QList<int> frames;
        for (const auto &args : spy)
            frames.append(args.at(0).toInt());
        QCOMPARE(frames.mid(0, 4), (QList<int>{1, 2, 0, 1}));
    }

    void changingFpsKeepsThePosition()
    {
        RunnerAnimator animator;
        animator.setSequence({0, 1, 2, 3, 4});
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        animator.setFps(40);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 3, 2000);
        const int before = animator.currentFrame();
        animator.setFps(10); // no frame reset, no extra signal
        QCOMPARE(animator.currentFrame(), before);
        const int signalsBefore = spy.size();
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() > signalsBefore, 2000);
        QCOMPARE(spy.last().at(0).toInt(), (before + 1) % 5);
        QCOMPARE(animator.fps(), 10);
    }

    void switchingRunnerKeepsTimerAndUsesOnlyNewFrames()
    {
        RunnerAnimator animator;
        animator.setSequence({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
        animator.setFps(40);
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 6, 2000); // somewhere past frame 5
        spy.clear();

        animator.setSequence({0, 1}); // a runner with only two frames
        QCOMPARE(animator.fps(), 40);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 6, 2000); // timer still running
        for (const auto &args : spy) {
            const int frame = args.at(0).toInt();
            QVERIFY2(frame == 0 || frame == 1, qPrintable(QString::number(frame))); // never an old index
        }
        QCOMPARE(spy.first().at(0).toInt(), 0); // restarted at the new runner's first frame
    }

    void customSequenceIsFollowed()
    {
        RunnerAnimator animator;
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        animator.setSequence({0, 1, 2, 1});
        animator.setFps(40);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 6, 2000);
        QList<int> frames;
        for (const auto &args : spy)
            frames.append(args.at(0).toInt());
        QCOMPARE(frames.mid(0, 6), (QList<int>{0, 1, 2, 1, 0, 1}));
    }

    void invalidFpsIsIgnored()
    {
        RunnerAnimator animator;
        animator.setFps(20);
        animator.setFps(0);
        animator.setFps(-5);
        QCOMPARE(animator.fps(), 20);
    }

    void pauseStopsAndResumesTheExistingTimer()
    {
        RunnerAnimator animator;
        animator.setSequence({0, 1, 2});
        animator.setFps(40);
        QSignalSpy spy(&animator, &RunnerAnimator::frameChanged);
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() >= 2, 1000);
        animator.setPaused(true);
        QVERIFY(!animator.isRunning());
        const int pausedAt = spy.size();
        QTest::qWait(100);
        QCOMPARE(spy.size(), pausedAt);
        animator.setPaused(false);
        QVERIFY(animator.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() > pausedAt, 1000);
        QCOMPARE(animator.fps(), 40);
    }
};

QTEST_GUILESS_MAIN(TestRunnerAnimator)
#include "tst_runneranimator.moc"
