#include "runner/framenames.h"

#include <QTest>

#include <algorithm>

using aqtruncat::frameFileNames;
using aqtruncat::naturalLess;
using aqtruncat::trailingFrameNumber;

class TestFrameNames : public QObject
{
    Q_OBJECT

private slots:
    void numbersAboveNineSortNumerically()
    {
        // The filesystem/alphabetical order runcat-tray used: 1, 10, 11, 2, 3.
        const QStringList input{QStringLiteral("1.png"), QStringLiteral("10.png"), QStringLiteral("11.png"),
                                QStringLiteral("2.png"), QStringLiteral("3.png")};
        QCOMPARE(frameFileNames(input), (QStringList{QStringLiteral("1.png"), QStringLiteral("2.png"),
                                                     QStringLiteral("3.png"), QStringLiteral("10.png"),
                                                     QStringLiteral("11.png")}));
    }

    void galleryNamesWithManyFrames()
    {
        QStringList names;
        for (int i = 23; i >= 0; --i) // reversed on purpose
            names.append(QStringLiteral("dojo-panda-frame-%1.png").arg(i));
        const QStringList sorted = frameFileNames(names);
        for (int i = 0; i < 24; ++i)
            QCOMPARE(sorted[i], QStringLiteral("dojo-panda-frame-%1.png").arg(i));
    }

    void leadingZerosAndMixedPadding()
    {
        QVERIFY(naturalLess(QStringLiteral("frame-09.png"), QStringLiteral("frame-10.png")));
        QVERIFY(naturalLess(QStringLiteral("frame-9.png"), QStringLiteral("frame-010.png")));
        // Same value with different padding: still a strict, deterministic order.
        QVERIFY(naturalLess(QStringLiteral("frame-01.png"), QStringLiteral("frame-1.png"))
                != naturalLess(QStringLiteral("frame-1.png"), QStringLiteral("frame-01.png")));
    }

    void hugeNumbersDoNotOverflow()
    {
        QVERIFY(naturalLess(QStringLiteral("f-99999999999999999999.png"), QStringLiteral("f-100000000000000000000.png")));
    }

    void caseInsensitiveTextWithTotalOrder()
    {
        QVERIFY(naturalLess(QStringLiteral("a-2.png"), QStringLiteral("B-1.png")));
        QVERIFY(naturalLess(QStringLiteral("Frame-1.png"), QStringLiteral("frame-1.png"))
                != naturalLess(QStringLiteral("frame-1.png"), QStringLiteral("Frame-1.png")));
    }

    void orderIsIndependentOfInputOrder()
    {
        QStringList a{QStringLiteral("x-3.png"), QStringLiteral("x-1.png"), QStringLiteral("x-20.png"),
                      QStringLiteral("x-2.png")};
        QStringList b = a;
        std::reverse(b.begin(), b.end());
        QCOMPARE(frameFileNames(a), frameFileNames(b));
    }

    void ignoresHarmlessFiles()
    {
        const QStringList input{QStringLiteral("frame-0.png"), QStringLiteral("readme.txt"),
                                QStringLiteral(".DS_Store"), QStringLiteral("._frame-0.png"),
                                QStringLiteral("metadata.json"), QStringLiteral("preview.png"),
                                QStringLiteral("Thumbs.db"), QStringLiteral("frame-1.PNG")};
        QCOMPARE(frameFileNames(input), (QStringList{QStringLiteral("frame-0.png"), QStringLiteral("frame-1.PNG")}));
    }

    void trailingNumber()
    {
        QCOMPARE(trailingFrameNumber(QStringLiteral("beagle-frame-12.png")), 12);
        QCOMPARE(trailingFrameNumber(QStringLiteral("7.PNG")), 7);
        QCOMPARE(trailingFrameNumber(QStringLiteral("walk.png")), -1);
    }
};

QTEST_APPLESS_MAIN(TestFrameNames)
#include "tst_framenames.moc"
