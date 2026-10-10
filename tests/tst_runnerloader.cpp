#include "runner/runnerloader.h"
#include "runnerfixture.h"

#include <QTemporaryDir>
#include <QTest>

using aqtruncat::RunnerSource;
using aqtruncat::loadRunner;

namespace {

bool hasWarningContaining(const aqtruncat::Runner &runner, const QString &text)
{
    for (const QString &w : runner.warnings)
        if (w.contains(text))
            return true;
    return false;
}

} // namespace

class TestRunnerLoader : public QObject
{
    Q_OBJECT

private slots:
    void loadsValidGalleryRunner()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.filePath(QStringLiteral("beagle")));
        fixture::writeFrames(dir, fixture::galleryNames(QStringLiteral("beagle"), 9), {70, 36});

        const auto result = loadRunner(dir.path(), QStringLiteral("beagle"), RunnerSource::User);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        const auto &r = *result.runner;
        QCOMPARE(r.id, QStringLiteral("beagle"));
        QCOMPARE(r.displayName, QStringLiteral("Beagle"));
        QCOMPARE(r.source, RunnerSource::User);
        QCOMPARE(r.frames.size(), 9);
        QCOMPARE(r.frameSize, QSize(70, 36));
        QCOMPARE(r.sequence, (QList<int>{0, 1, 2, 3, 4, 5, 6, 7, 8}));
        QVERIFY(r.warnings.isEmpty());
    }

    void keepsNonSquareSizeAndAspectRatio()
    {
        QTemporaryDir tmp;
        for (const QSize size : {QSize(100, 36), QSize(25, 36), QSize(36, 36), QSize(48, 64)}) {
            const QDir dir(tmp.filePath(QStringLiteral("r%1x%2").arg(size.width()).arg(size.height())));
            fixture::writeFrames(dir, fixture::galleryNames(QStringLiteral("r"), 2), size);
            const auto result = loadRunner(dir.path(), QStringLiteral("r"), RunnerSource::User);
            QVERIFY(result.isValid());
            QCOMPARE(result.runner->frameSize, size);
            for (const QImage &frame : result.runner->frames)
                QCOMPARE(frame.size(), size); // never scaled or padded by the loader
        }
    }

    void ordersFramesNaturallyAboveTen()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        // Frame N carries marker N; created in an order the filesystem may keep.
        for (int i : {10, 2, 11, 0, 1, 3, 4, 5, 6, 7, 8, 9})
            fixture::writeFrame(dir.filePath(QStringLiteral("%1.png").arg(i)), {40, 36}, Qt::black, i);
        const auto result = loadRunner(dir.path(), QStringLiteral("n"), RunnerSource::User);
        QVERIFY(result.isValid());
        for (int i = 0; i < 12; ++i)
            QCOMPARE(fixture::markerOf(result.runner->frames[i]), i);
    }

    void missingDirectoryIsFatal()
    {
        const auto result = loadRunner(QStringLiteral("/nonexistent/aqtruncat/runner"), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
        QVERIFY(result.error.contains(QStringLiteral("does not exist")));
    }

    void emptyDirectoryIsFatal()
    {
        QTemporaryDir tmp;
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
        QVERIFY(result.error.contains(QStringLiteral("no PNG frames")));
    }

    void directoryWithOnlyOtherFilesIsFatal()
    {
        QTemporaryDir tmp;
        fixture::writeFile(tmp.filePath(QStringLiteral("readme.txt")), "hello");
        fixture::writeFile(tmp.filePath(QStringLiteral("preview.png")), "not a frame");
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
    }

    void corruptedPngIsFatal()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrames(dir, {QStringLiteral("f-0.png"), QStringLiteral("f-2.png")});
        QFile good(dir.filePath(QStringLiteral("f-0.png")));
        QVERIFY(good.open(QIODevice::ReadOnly));
        const QByteArray bytes = good.readAll();
        // Valid PNG header, truncated image data.
        fixture::writeFile(dir.filePath(QStringLiteral("f-1.png")), bytes.left(bytes.size() / 2));
        const auto result = loadRunner(dir.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
        QVERIFY2(result.error.contains(QStringLiteral("f-1.png")), qPrintable(result.error));
    }

    void nonPngContentWithPngNameIsFatal()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrames(dir, {QStringLiteral("f-0.png")});
        QImage jpeg(56, 36, QImage::Format_RGB32);
        jpeg.fill(Qt::white);
        QVERIFY(jpeg.save(dir.filePath(QStringLiteral("f-1.png")), "JPG"));
        const auto result = loadRunner(dir.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
    }

    void inconsistentSizesAreFatal()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrame(dir.filePath(QStringLiteral("f-0.png")), {56, 36});
        fixture::writeFrame(dir.filePath(QStringLiteral("f-1.png")), {57, 36});
        const auto result = loadRunner(dir.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
        QVERIFY(result.error.contains(QStringLiteral("same size")));
    }

    void oversizedFrameIsFatal()
    {
        QTemporaryDir tmp;
        fixture::writeFrame(QDir(tmp.path()).filePath(QStringLiteral("f-0.png")), {2000, 36});
        QVERIFY(!loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User).isValid());
    }

    void harmlessExtraFilesAreIgnored()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrames(dir, fixture::galleryNames(QStringLiteral("cat"), 5));
        fixture::writeFile(dir.filePath(QStringLiteral(".DS_Store")), QByteArray(64, '\0'));
        fixture::writeFile(dir.filePath(QStringLiteral("._cat-frame-0.png")), "AppleDouble junk");
        fixture::writeFile(dir.filePath(QStringLiteral("notes.txt")), "anything");
        fixture::writeFile(dir.filePath(QStringLiteral("Thumbs.db")), "junk");
        fixture::writeFile(dir.filePath(QStringLiteral("preview.png")), "an APNG in the Gallery");
        dir.mkpath(QStringLiteral("__MACOSX"));
        const auto result = loadRunner(dir.path(), QStringLiteral("cat"), RunnerSource::User);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QCOMPARE(result.runner->frames.size(), 5);
        QVERIFY(result.runner->warnings.isEmpty());
    }

    void missingFrameNumberIsAWarning()
    {
        QTemporaryDir tmp;
        fixture::writeFrames(QDir(tmp.path()), {QStringLiteral("f-0.png"), QStringLiteral("f-1.png"), QStringLiteral("f-3.png")});
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(result.isValid());
        QCOMPARE(result.runner->frames.size(), 3);
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("jump from 1 to 3")));
    }

    void duplicateFrameNumberIsAWarning()
    {
        QTemporaryDir tmp;
        fixture::writeFrames(QDir(tmp.path()), {QStringLiteral("f-1.png"), QStringLiteral("f-01.png"), QStringLiteral("f-2.png")});
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(result.isValid());
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("more than once")));
    }

    void freeFormNamesUseNaturalOrder()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrame(dir.filePath(QStringLiteral("b-step.png")), {40, 36}, Qt::black, 2);
        fixture::writeFrame(dir.filePath(QStringLiteral("a-step.png")), {40, 36}, Qt::black, 1);
        const auto result = loadRunner(dir.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(result.isValid());
        QCOMPARE(fixture::markerOf(result.runner->frames[0]), 1);
        QCOMPARE(fixture::markerOf(result.runner->frames[1]), 2);
        QVERIFY(result.runner->warnings.isEmpty());
    }

    void nonUpstreamSizeAndSingleFrameAreWarnings()
    {
        QTemporaryDir tmp;
        fixture::writeFrames(QDir(tmp.path()), {QStringLiteral("only.png")}, {48, 48});
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(result.isValid());
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("10–100 × 36")));
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("only one frame")));
    }

    void galleryFolderWithExtractedArchive()
    {
        // runners/<name>/ from the Runner Gallery repository after unzipping
        // <name>-frames.zip in place.
        QTemporaryDir tmp;
        const QDir root(tmp.filePath(QStringLiteral("deer")));
        fixture::writeFrames(QDir(root.filePath(QStringLiteral("deer-frames"))), fixture::galleryNames(QStringLiteral("deer"), 6), {60, 36});
        fixture::writeFile(root.filePath(QStringLiteral("deer-frames.zip")), "zip bytes");
        fixture::writeFile(root.filePath(QStringLiteral("preview.png")), "apng");
        fixture::writeFile(root.filePath(QStringLiteral("metadata.json")),
                           R"json({"author": "Someone (someone)", "displayName": "Deer", "type": "color", "tags": ["animal"]})json");
        const auto result = loadRunner(root.path(), QStringLiteral("deer"), RunnerSource::User);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QCOMPARE(result.runner->frames.size(), 6);
        QCOMPARE(result.runner->displayName, QStringLiteral("Deer"));
        QCOMPARE(result.runner->author, QStringLiteral("Someone (someone)"));
        QVERIFY(!result.runner->monochrome);
        QVERIFY(result.runner->path.endsWith(QStringLiteral("deer-frames")));
    }

    void severalFrameFoldersAreAmbiguous()
    {
        QTemporaryDir tmp;
        fixture::writeFrames(QDir(tmp.filePath(QStringLiteral("a"))), {QStringLiteral("f-0.png")});
        fixture::writeFrames(QDir(tmp.filePath(QStringLiteral("b"))), {QStringLiteral("f-0.png")});
        const auto result = loadRunner(tmp.path(), QStringLiteral("x"), RunnerSource::User);
        QVERIFY(!result.isValid());
        QVERIFY(result.error.contains(QStringLiteral("several")));
    }

    void metadataFrameOrder()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrames(dir, fixture::galleryNames(QStringLiteral("m"), 5));
        fixture::writeFile(dir.filePath(QStringLiteral("metadata.json")), R"json({"frameOrder": [0, 1, 2, 3, 4, 3, 2, 1]})json");
        const auto result = loadRunner(dir.path(), QStringLiteral("m"), RunnerSource::User);
        QVERIFY(result.isValid());
        QCOMPARE(result.runner->sequence, (QList<int>{0, 1, 2, 3, 4, 3, 2, 1}));
    }

    void badMetadataIsAWarningNotAFailure()
    {
        QTemporaryDir tmp;
        const QDir dir(tmp.path());
        fixture::writeFrames(dir, fixture::galleryNames(QStringLiteral("m"), 3));
        fixture::writeFile(dir.filePath(QStringLiteral("metadata.json")), "{ not json");
        auto result = loadRunner(dir.path(), QStringLiteral("m"), RunnerSource::User);
        QVERIFY(result.isValid());
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("not a valid JSON")));

        fixture::writeFile(dir.filePath(QStringLiteral("metadata.json")),
                           R"json({"type": "sepia", "displayName": 3, "frameOrder": [0, 7]})json");
        result = loadRunner(dir.path(), QStringLiteral("m"), RunnerSource::User);
        QVERIFY(result.isValid());
        QCOMPARE(result.runner->displayName, QStringLiteral("M"));
        QCOMPARE(result.runner->sequence, (QList<int>{0, 1, 2}));
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("sepia")));
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("displayName")));
        QVERIFY(hasWarningContaining(*result.runner, QStringLiteral("frameOrder")));
    }

    void monochromeDetectionWithoutMetadata()
    {
        QTemporaryDir tmp;
        const QDir grey(tmp.filePath(QStringLiteral("grey")));
        fixture::writeFrames(grey, {QStringLiteral("f-0.png")}, {40, 36}, QColor(80, 80, 80));
        QVERIFY(loadRunner(grey.path(), QStringLiteral("g"), RunnerSource::User).runner->monochrome);

        const QDir red(tmp.filePath(QStringLiteral("red")));
        fixture::writeFrames(red, {QStringLiteral("f-0.png")}, {40, 36}, QColor(220, 30, 30));
        QVERIFY(!loadRunner(red.path(), QStringLiteral("r"), RunnerSource::User).runner->monochrome);
    }

    void slightlyTintedEdgesStayMonochrome()
    {
        // Black artwork whose anti-aliased edge has a few dark blue pixels
        // (like the Gallery "icosahedron" runner): still monochrome.
        QImage frame(40, 36, QImage::Format_ARGB32);
        frame.fill(Qt::transparent);
        for (int y = 0; y < 36; ++y)
            for (int x = 0; x < 40; ++x)
                frame.setPixelColor(x, y, x == 0 && y < 10 ? QColor(0, 0, 96) : QColor(0, 0, 0));
        QVERIFY(aqtruncat::looksMonochrome({frame})); // 10 of 1440 pixels tinted
        QVERIFY(aqtruncat::looksMonochrome({})); // nothing visible
    }

    void displayNameFromId()
    {
        QCOMPARE(aqtruncat::displayNameFromId(QStringLiteral("border-collie")), QStringLiteral("Border Collie"));
        QCOMPARE(aqtruncat::displayNameFromId(QStringLiteral("cat")), QStringLiteral("Cat"));
        QCOMPARE(aqtruncat::displayNameFromId(QStringLiteral("my_runner")), QStringLiteral("My Runner"));
    }
};

QTEST_GUILESS_MAIN(TestRunnerLoader)
#include "tst_runnerloader.moc"
