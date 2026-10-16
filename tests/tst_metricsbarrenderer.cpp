#include "app/metricsbarrenderer.h"

#include <QApplication>
#include <QTest>

using namespace aqtruncat::metricsbar;

namespace {

Item item(Status status, const QString &value, Origin origin = Origin::BuiltIn)
{
    Item result;
    result.id = QStringLiteral("cpu");
    result.origin = origin;
    result.label = QStringLiteral("CPU");
    result.shortLabel = QStringLiteral("CPU");
    result.shortValue = value;
    result.status = status;
    return result;
}

// Bounding box of pixels drawn with any alpha, in image pixels.
QRect inkBounds(const QImage &image)
{
    QRect bounds;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) > 0)
                bounds |= QRect(x, y, 1, 1);
    return bounds;
}

// Every drawn pixel has exactly the requested colour (alpha aside).
bool onlyColour(const QImage &image, const QColor &colour)
{
    const QImage plain = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < plain.height(); ++y) {
        for (int x = 0; x < plain.width(); ++x) {
            const QRgb pixel = plain.pixel(x, y);
            if (qAlpha(pixel) < 128)
                continue;
            if (qAbs(qRed(pixel) - colour.red()) > 2 || qAbs(qGreen(pixel) - colour.green()) > 2
                || qAbs(qBlue(pixel) - colour.blue()) > 2)
                return false;
        }
    }
    return true;
}

} // namespace

class TestMetricsBarRenderer : public QObject
{
    Q_OBJECT

private slots:
    void contentPolicy()
    {
        QCOMPARE(iconContentFor(item(Status::Available, QStringLiteral("23%"))).text, QStringLiteral("23%"));
        QCOMPARE(iconContentFor(item(Status::Available, QStringLiteral("23%"))).caption, QStringLiteral("CPU"));
        QCOMPARE(iconContentFor(item(Status::Pending, {})).text, PendingText);
        QCOMPARE(iconContentFor(item(Status::Unavailable, {})).text, UnavailableText);
        QCOMPARE(iconContentFor(item(Status::Error, QStringLiteral("5%"))).text, ErrorText);
        // A custom file being retried keeps its last value; built-ins never do.
        QCOMPARE(iconContentFor(item(Status::Pending, QStringLiteral("5.4%"), Origin::Custom)).text, QStringLiteral("5.4%"));
        QCOMPARE(iconContentFor(item(Status::Pending, QStringLiteral("5.4%"))).text, PendingText);
        QCOMPARE(iconContentFor(item(Status::Unavailable, QStringLiteral("5.4%"), Origin::Custom)).text, UnavailableText);
    }

    void shortValuesFitWithCaption_data()
    {
        QTest::addColumn<QString>("caption");
        QTest::addColumn<QString>("text");
        QTest::newRow("percent") << "CPU" << "23%";
        QTest::newRow("full") << "CPU" << "100%";
        QTest::newRow("rate") << "NET↓" << "1.2M";
        QTest::newRow("upload arrow") << "NET↑" << "850K";
        QTest::newRow("disk") << "DSK↑" << "32M";
        QTest::newRow("swap") << "SWAP" << "—";
        QTest::newRow("symbol caption") << "✣" << "5.4%";
        QTest::newRow("pending") << "MEM" << "…";
        QTest::newRow("error") << "ROOT" << "!";
    }

    void shortValuesFitWithCaption()
    {
        QFETCH(QString, caption);
        QFETCH(QString, text);
        const RenderedIcon icon = renderIcon({caption, text}, QApplication::font(), Qt::black, 1.0);
        QVERIFY(!icon.overflow);
        QVERIFY(icon.captionShown);
        QVERIFY(icon.valuePixelSize >= ValueMinPixelSize);
        QVERIFY(icon.valuePixelSize <= ValueMaxPixelSize);
        QVERIFY(icon.captionPixelSize >= CaptionMinPixelSize);
        const QRectF canvas(0, 0, CanvasSide, CanvasSide);
        QVERIFY(canvas.contains(icon.valueRect));
        QVERIFY(canvas.contains(icon.captionRect));
        QVERIFY(icon.captionRect.bottom() <= icon.valueRect.top() + 0.01); // no overlap
        QVERIFY(!inkBounds(icon.image).isEmpty());
    }

    void tooLongValueShowsOverflowMarkerNotACut()
    {
        const RenderedIcon icon = renderIcon({QStringLiteral("✣"), QStringLiteral("1234567 tokens")},
                                             QApplication::font(), Qt::black, 1.0);
        QVERIFY(icon.overflow);
        QVERIFY(icon.valuePixelSize >= 1.0);
        QVERIFY(QRectF(0, 0, CanvasSide, CanvasSide).contains(icon.valueRect));
    }

    void longCaptionIsDroppedButValueStays()
    {
        const RenderedIcon icon = renderIcon({QStringLiteral("Claude Code usage"), QStringLiteral("5%")},
                                             QApplication::font(), Qt::black, 1.0);
        QVERIFY(!icon.captionShown);
        QVERIFY(!icon.overflow);
        QVERIFY(icon.valueRect.height() > 0);
    }

    void emptyTextFallsBackToPending()
    {
        const RenderedIcon empty = renderIcon({QString(), QString()}, QApplication::font(), Qt::black, 1.0);
        const RenderedIcon pending = renderIcon({QString(), PendingText}, QApplication::font(), Qt::black, 1.0);
        QCOMPARE(empty.image, pending.image);
    }

    void structuredSymbolOccupiesCaptionBandWithFallback()
    {
        aqtruncat::custommetrics::IconReference reference;
        reference.type = aqtruncat::custommetrics::IconType::File;
        reference.value = QStringLiteral("./missing.svg");
        reference.resolvedValue = QStringLiteral("/definitely/missing.svg");
        const RenderedIcon icon = renderIcon(
            {QString(), QStringLiteral("23.4"), reference}, QApplication::font(), Qt::black, 1.0);
        QVERIFY(icon.captionShown);
        QVERIFY(!icon.overflow);
        QVERIFY(!inkBounds(icon.image).isEmpty());
        QVERIFY(icon.captionRect.bottom() <= icon.valueRect.top() + 0.01);
    }

    void squareImageScalesWithDevicePixelRatio_data()
    {
        QTest::addColumn<double>("dpr");
        QTest::addColumn<int>("side");
        QTest::newRow("1x") << 1.0 << 44;
        QTest::newRow("1.25x") << 1.25 << 44;
        QTest::newRow("2x") << 2.0 << 44;
        QTest::newRow("3x") << 3.0 << 66;
        QTest::newRow("invalid") << 0.0 << 44;
    }

    void squareImageScalesWithDevicePixelRatio()
    {
        QFETCH(double, dpr);
        QFETCH(int, side);
        const RenderedIcon icon = renderIcon({QStringLiteral("CPU"), QStringLiteral("23%")},
                                             QApplication::font(), Qt::black, dpr);
        QCOMPARE(icon.image.size(), QSize(side, side));
        // Layout is resolution independent: same canvas geometry at any ratio.
        const RenderedIcon reference = renderIcon({QStringLiteral("CPU"), QStringLiteral("23%")},
                                                  QApplication::font(), Qt::black, 1.0);
        QCOMPARE(icon.valuePixelSize, reference.valuePixelSize);
        const QRect ink = inkBounds(icon.image);
        QVERIFY(ink.width() > side / 3); // the value is not a speck
    }

    void usesOnlyTheGivenPaletteColour_data()
    {
        QTest::addColumn<QColor>("colour");
        QTest::newRow("light theme text") << QColor(0x23, 0x26, 0x29);
        QTest::newRow("dark theme text") << QColor(0xfc, 0xfc, 0xfc);
    }

    void usesOnlyTheGivenPaletteColour()
    {
        QFETCH(QColor, colour);
        const RenderedIcon icon = renderIcon({QStringLiteral("NET↓"), QStringLiteral("1.2M")},
                                             QApplication::font(), colour, 1.0);
        QVERIFY(onlyColour(icon.image, colour));
        // Transparent background: the corners are untouched.
        QCOMPARE(qAlpha(icon.image.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(icon.image.pixel(icon.image.width() - 1, icon.image.height() - 1)), 0);
    }
};

QTEST_MAIN(TestMetricsBarRenderer)
#include "tst_metricsbarrenderer.moc"
