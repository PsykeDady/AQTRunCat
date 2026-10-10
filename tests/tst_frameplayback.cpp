#include "core/frameplayback.h"

#include <QTest>

using aqtruncat::FramePlayback;

class TestFramePlayback : public QObject
{
    Q_OBJECT

private slots:
    void emptyHasNoFrame()
    {
        FramePlayback p;
        QVERIFY(p.isEmpty());
        QCOMPARE(p.currentFrame(), -1);
        QCOMPARE(p.advance(), -1);
    }

    void advancesAndWrapsAround()
    {
        FramePlayback p({0, 1, 2});
        QCOMPARE(p.currentFrame(), 0);
        QCOMPARE(p.advance(), 1);
        QCOMPARE(p.advance(), 2);
        QCOMPARE(p.advance(), 0); // wrap-around
        QCOMPARE(p.advance(), 1);
    }

    void followsCustomSequence()
    {
        FramePlayback p({0, 1, 2, 1}); // swing-like order with repeated frames
        QList<int> shown{p.currentFrame()};
        for (int i = 0; i < 7; ++i)
            shown.append(p.advance());
        QCOMPARE(shown, (QList<int>{0, 1, 2, 1, 0, 1, 2, 1}));
    }

    void newSequenceRestartsFromItsFirstEntry()
    {
        FramePlayback p({0, 1, 2, 3, 4});
        p.advance();
        p.advance();
        p.advance();
        QCOMPARE(p.currentFrame(), 3);
        p.setSequence({0, 1}); // the old position 3 would not even exist here
        QCOMPARE(p.position(), 0u);
        QCOMPARE(p.currentFrame(), 0);
        QCOMPARE(p.advance(), 1);
        QCOMPARE(p.advance(), 0);
    }

    void singleFrame()
    {
        FramePlayback p({0});
        QCOMPARE(p.advance(), 0);
        QCOMPARE(p.advance(), 0);
    }
};

QTEST_APPLESS_MAIN(TestFramePlayback)
#include "tst_frameplayback.moc"
