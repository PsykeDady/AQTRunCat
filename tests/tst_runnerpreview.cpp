#include "app/runnerpreviewwidget.h"
#include "app/runnerpresentation.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

namespace {

aqtruncat::Runner runner(const QString &id, const QColor &colour, bool monochrome, QSize size = {100, 36})
{
    aqtruncat::Runner result;
    result.id = id;
    result.displayName = id;
    result.monochrome = monochrome;
    result.frameSize = size;
    for (int i = 0; i < 3; ++i) {
        QImage frame(size, QImage::Format_ARGB32_Premultiplied);
        frame.fill(colour);
        result.frames.append(frame);
        result.sequence.append(i);
    }
    return result;
}

} // namespace

class TestRunnerPreview : public QObject
{
    Q_OBJECT

private slots:
    void trayForegroundFollowsTheReportedColourScheme()
    {
        // Generic Qt on a non-KDE desktop: light palette, black text.
        QPalette light;
        light.setColor(QPalette::WindowText, QColor(0x00, 0x00, 0x00));
        light.setColor(QPalette::Window, QColor(0xfa, 0xf9, 0xf8));
        QCOMPARE(trayForegroundColour(light, Qt::ColorScheme::Unknown), QColor(0x00, 0x00, 0x00));
        QCOMPARE(trayForegroundColour(light, Qt::ColorScheme::Light), QColor(0x00, 0x00, 0x00));
        // Dark scheme reported but palette unchanged: use the palette's light role.
        QCOMPARE(trayForegroundColour(light, Qt::ColorScheme::Dark), QColor(0xfa, 0xf9, 0xf8));

        // A platform palette that already matches (KDE Breeze Dark) is kept.
        QPalette dark;
        dark.setColor(QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
        dark.setColor(QPalette::Window, QColor(0x20, 0x23, 0x26));
        QCOMPARE(trayForegroundColour(dark, Qt::ColorScheme::Dark), QColor(0xfc, 0xfc, 0xfc));
        QCOMPARE(trayForegroundColour(dark, Qt::ColorScheme::Unknown), QColor(0xfc, 0xfc, 0xfc));
        QCOMPARE(trayForegroundColour(dark, Qt::ColorScheme::Light), QColor(0x20, 0x23, 0x26));
    }

    void validRunnerAdvancesAtFixedPreviewRate()
    {
        RunnerPreviewWidget preview;
        preview.resize(320, 140);
        preview.setRunner(runner(QStringLiteral("cat"), Qt::black, true));
        QSignalSpy frames(&preview, &RunnerPreviewWidget::framePresented);
        preview.show();
        QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 3, 1000);
        QCOMPARE(preview.runnerId(), QStringLiteral("cat"));
        QVERIFY(preview.currentFrame() >= 0);
        preview.hide();
        const int stopped = frames.size();
        QTest::qWait(150);
        QCOMPARE(frames.size(), stopped);
    }

    void preservesWideAspectRatio()
    {
        RunnerPreviewWidget preview;
        preview.resize(300, 150);
        preview.setRunner(runner(QStringLiteral("wide"), Qt::red, false, {100, 36}));
        preview.show();
        QTest::qWait(20);
        const QSize rendered = preview.renderedPixmapSize();
        QVERIFY(rendered.isValid());
        QVERIFY(qAbs(double(rendered.width()) / rendered.height() - 100.0 / 36.0) < 0.05);
    }

    void colourAndMonochromeAndRunnerChange()
    {
        RunnerPreviewWidget preview;
        preview.resize(240, 120);
        preview.setRunner(runner(QStringLiteral("mono"), Qt::black, true));
        preview.show();
        QCOMPARE(preview.runnerId(), QStringLiteral("mono"));
        preview.setRunner(runner(QStringLiteral("colour"), Qt::red, false));
        QCOMPARE(preview.runnerId(), QStringLiteral("colour"));
        preview.clearRunner();
        QVERIFY(preview.runnerId().isEmpty());
        QCOMPARE(preview.currentFrame(), -1);
    }

    void templateTintAndColourSemanticsAreShared()
    {
        QImage black({2, 2}, QImage::Format_ARGB32_Premultiplied);
        black.fill(Qt::black);
        const QImage tinted = presentRunnerFrame(black, true, QColor(10, 200, 30));
        QCOMPARE(tinted.pixelColor(0, 0), QColor(10, 200, 30));

        QImage colour({2, 2}, QImage::Format_ARGB32_Premultiplied);
        colour.fill(QColor(210, 20, 40));
        QCOMPARE(presentRunnerFrame(colour, false, Qt::green).pixelColor(0, 0), QColor(210, 20, 40));
    }
};

QTEST_MAIN(TestRunnerPreview)
#include "tst_runnerpreview.moc"
