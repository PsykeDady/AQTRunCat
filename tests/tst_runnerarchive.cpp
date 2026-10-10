#include "archivefixture.h"
#include "runner/runnerarchive.h"
#include "runner/runnerloader.h"

#include <QDir>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QTest>

using aqtruncat::ArchiveEntryKind;
using aqtruncat::ArchiveLimits;
using aqtruncat::RunnerError;
using aqtruncat::extractRunnerArchive;
using archivefixture::Entry;

namespace {

// A sandbox per test: archive in sandbox/, extraction into sandbox/out/.
// After each extraction the sandbox is checked for files outside out/.
struct Sandbox {
    QTemporaryDir tmp;
    QString archive = tmp.filePath(QStringLiteral("runner.zip"));
    QString out = tmp.filePath(QStringLiteral("out"));

    Sandbox() { QDir().mkpath(out); }

    aqtruncat::ExtractionResult extract(const ArchiveLimits &limits = {}) const
    {
        return extractRunnerArchive(archive, out, limits);
    }

    // Everything in the sandbox except the archive itself must be under out/.
    QStringList strayFiles() const
    {
        QStringList stray;
        QDirIterator it(tmp.path(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path != archive && path != out && !path.startsWith(out + u'/'))
                stray.append(path);
        }
        return stray;
    }
};

} // namespace

class TestRunnerArchive : public QObject
{
    Q_OBJECT

private slots:
    // --- path and entry checks -------------------------------------------

    void acceptsNormalPaths_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("normalised");
        QTest::newRow("file") << QStringLiteral("frame-0.png") << QStringLiteral("frame-0.png");
        QTest::newRow("nested") << QStringLiteral("cat-frames/cat-frame-0.png") << QStringLiteral("cat-frames/cat-frame-0.png");
        QTest::newRow("directory") << QStringLiteral("cat-frames/") << QStringLiteral("cat-frames");
        QTest::newRow("unicode") << QStringLiteral("gatto/frame-ü.png") << QStringLiteral("gatto/frame-ü.png");
    }

    void acceptsNormalPaths()
    {
        QFETCH(QString, raw);
        QFETCH(QString, normalised);
        QString error;
        QCOMPARE(aqtruncat::safeArchivePath(raw, &error).value_or(QStringLiteral("<rejected>")), normalised);
    }

    void rejectsUnsafePaths_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::newRow("parent") << QStringLiteral("../file");
        QTest::newRow("parent dir") << QStringLiteral("../runner/frame-0.png");
        QTest::newRow("nested traversal") << QStringLiteral("foo/../../../bar");
        QTest::newRow("inner parent") << QStringLiteral("a/../b.png");
        QTest::newRow("absolute") << QStringLiteral("/etc/foo");
        QTest::newRow("absolute home") << QStringLiteral("/home/foo/.bashrc");
        QTest::newRow("double slash") << QStringLiteral("//etc/foo");
        QTest::newRow("empty component") << QStringLiteral("a//b.png");
        QTest::newRow("dot component") << QStringLiteral("./frame-0.png");
        QTest::newRow("backslash traversal") << QStringLiteral("..\\..\\evil.png");
        QTest::newRow("drive letter") << QStringLiteral("C:/evil.png");
        QTest::newRow("control char") << QStringLiteral("frame\n0.png");
        QTest::newRow("empty") << QString();
        QTest::newRow("too long") << QString(600, u'a');
    }

    void rejectsUnsafePaths()
    {
        QFETCH(QString, raw);
        QString error;
        QVERIFY(!aqtruncat::safeArchivePath(raw, &error).has_value());
        QVERIFY(!error.isEmpty());
    }

    void onlyRegularFilesAndDirectoriesAreAccepted()
    {
        QVERIFY(aqtruncat::unsupportedEntryKind(ArchiveEntryKind::Regular).isEmpty());
        QVERIFY(aqtruncat::unsupportedEntryKind(ArchiveEntryKind::Directory).isEmpty());
        QVERIFY(aqtruncat::unsupportedEntryKind(ArchiveEntryKind::Symlink).contains(QStringLiteral("symbolic")));
        QVERIFY(aqtruncat::unsupportedEntryKind(ArchiveEntryKind::Hardlink).contains(QStringLiteral("hard links")));
        QVERIFY(aqtruncat::unsupportedEntryKind(ArchiveEntryKind::Other).contains(QStringLiteral("special")));
    }

    void junkEntries()
    {
        QVERIFY(aqtruncat::isArchiveJunk(QStringLiteral("__MACOSX/cat-frames/._cat-frame-0.png")));
        QVERIFY(aqtruncat::isArchiveJunk(QStringLiteral("cat-frames/.DS_Store")));
        QVERIFY(!aqtruncat::isArchiveJunk(QStringLiteral("cat-frames/cat-frame-0.png")));
    }

    // --- valid archives -----------------------------------------------------

    void extractsGalleryArchive()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("beagle"), 12, {70, 36})));
        const auto result = box.extract();
        QVERIFY2(result.runner, qPrintable(result.error));
        QCOMPARE(result.runner->suggestedId, QStringLiteral("beagle"));
        QCOMPARE(result.runner->runnerRoot, QDir(box.out).filePath(QStringLiteral("beagle-frames")));
        QCOMPARE(result.runner->files, 12);
        const auto loaded = aqtruncat::loadRunner(result.runner->runnerRoot, QStringLiteral("beagle"), aqtruncat::RunnerSource::User);
        QVERIFY(loaded.isValid());
        QCOMPARE(loaded.runner->frames.size(), 12);
        QCOMPARE(loaded.runner->frameSize, QSize(70, 36));
        QVERIFY(box.strayFiles().isEmpty());
    }

    void extractsStoredEntriesWithoutDirectoryEntry()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("deer"), 5, {56, 36}, false), "store"));
        const auto result = box.extract();
        QVERIFY2(result.runner, qPrintable(result.error));
        QCOMPARE(result.runner->suggestedId, QStringLiteral("deer"));
        QCOMPARE(result.runner->files, 5);
    }

    void framesAtTheTopLevelUseTheArchiveName()
    {
        QTemporaryDir tmp;
        const QString archive = tmp.filePath(QStringLiteral("My-Runner-frames.ZIP"));
        QList<Entry> entries;
        for (int i = 0; i < 3; ++i)
            entries.append({QStringLiteral("%1.png").arg(i), archivefixture::png(), AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(archive, entries));
        QDir().mkpath(tmp.filePath(QStringLiteral("out")));
        const auto result = extractRunnerArchive(archive, tmp.filePath(QStringLiteral("out")));
        QVERIFY2(result.runner, qPrintable(result.error));
        QCOMPARE(result.runner->suggestedId, QStringLiteral("My-Runner"));
        QCOMPARE(result.runner->runnerRoot, QDir(tmp.filePath(QStringLiteral("out"))).absolutePath());
    }

    void packagingJunkIsSkipped()
    {
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 3);
        entries.append({QStringLiteral("__MACOSX/cat-frames/._cat-frame-0.png"), "apple double", AE_IFREG, {}});
        entries.append({QStringLiteral("cat-frames/.DS_Store"), QByteArray(16, '\0'), AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        const auto result = box.extract();
        QVERIFY2(result.runner, qPrintable(result.error));
        QCOMPARE(result.runner->skipped.size(), 2);
        QVERIFY(!QFileInfo::exists(QDir(box.out).filePath(QStringLiteral("__MACOSX"))));
        QCOMPARE(result.runner->suggestedId, QStringLiteral("cat"));
    }

    // --- malformed archives ---------------------------------------------------

    void missingFileIsNotFound()
    {
        Sandbox box;
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QCOMPARE(result.errorKind, RunnerError::NotFound);
    }

    void notAZipIsInvalid()
    {
        Sandbox box;
        QFile file(box.archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is a text file, not a ZIP archive\n");
        file.close();
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QCOMPARE(result.errorKind, RunnerError::Invalid);
    }

    void corruptedDataIsInvalid()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("cat"), 3)));
        QFile file(box.archive);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray bytes = file.readAll();
        const qsizetype data = bytes.indexOf("cat-frame-1.png") + 40; // inside the compressed data
        for (int i = 0; i < 64; ++i)
            bytes[data + i] = char(bytes[data + i] ^ 0x5a);
        file.seek(0);
        file.write(bytes);
        file.close();
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QCOMPARE(result.errorKind, RunnerError::Invalid);
        QVERIFY(box.strayFiles().isEmpty());
    }

    void truncatedArchiveIsInvalid()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("cat"), 3)));
        QFile file(box.archive);
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.resize(file.size() / 2);
        file.close();
        QCOMPARE(box.extract().errorKind, RunnerError::Invalid);
    }

    void emptyArchiveIsInvalid()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, {}));
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QCOMPARE(result.errorKind, RunnerError::Invalid);
    }

    void onlyDirectoriesIsInvalid()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, {{QStringLiteral("cat-frames/"), {}, AE_IFDIR, {}}}));
        QVERIFY(box.extract().error.contains(QStringLiteral("no files")));
    }

    void duplicateEntriesAreInvalid()
    {
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append(entries.last());
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QVERIFY(box.extract().error.contains(QStringLiteral("more than once")));
    }

    // --- hostile archives -------------------------------------------------------

    void pathTraversalIsRejected_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("../") << QStringLiteral("../evil.png");
        QTest::newRow("nested") << QStringLiteral("cat-frames/../../evil.png");
        QTest::newRow("deep") << QStringLiteral("foo/../../../bar.png");
        QTest::newRow("absolute") << QStringLiteral("/tmp/aqtruncat-evil.png");
    }

    void pathTraversalIsRejected()
    {
        QFETCH(QString, name);
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append({name, archivefixture::png(), AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QVERIFY2(archivefixture::storedNames(box.archive).contains(name), "the writer altered the hostile name");
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QCOMPARE(result.errorKind, RunnerError::Invalid);
        QVERIFY(result.error.contains(QStringLiteral("unsafe entry")));
        QVERIFY2(box.strayFiles().isEmpty(), qPrintable(box.strayFiles().join(u' ')));
        QVERIFY(!QFileInfo::exists(QStringLiteral("/tmp/aqtruncat-evil.png")));
    }

    void symlinkIsRejected()
    {
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append({QStringLiteral("cat-frames/cat-frame-2.png"), {}, AE_IFLNK, QStringLiteral("/etc/passwd")});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QVERIFY(result.error.contains(QStringLiteral("symbolic links")));
        QVERIFY(box.strayFiles().isEmpty());
    }

    void symlinkToDirectoryIsRejected()
    {
        // Classic attack: a link to a directory outside, then a file "through" it.
        Sandbox box;
        QList<Entry> entries{{QStringLiteral("escape"), {}, AE_IFLNK, QStringLiteral("..")},
                             {QStringLiteral("escape/evil.png"), archivefixture::png(), AE_IFREG, {}}};
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QCOMPARE(box.extract().errorKind, RunnerError::Invalid);
        QVERIFY(box.strayFiles().isEmpty());
    }

    void specialFilesAreRejected_data()
    {
        QTest::addColumn<uint>("mode");
        QTest::newRow("character device") << uint(S_IFCHR | 0644);
        QTest::newRow("block device") << uint(S_IFBLK | 0644);
        QTest::newRow("socket") << uint(S_IFSOCK | 0644);
    }

    void specialFilesAreRejected()
    {
        // ZIP writers do not create these; a crafted Unix mode in the central
        // directory does, and libarchive reports them as such.
        QFETCH(uint, mode);
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append({QStringLiteral("cat-frames/special"), "x", AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QVERIFY(archivefixture::patchUnixMode(box.archive, QStringLiteral("cat-frames/special"), mode));
        const auto result = box.extract();
        QVERIFY(!result.runner);
        QVERIFY2(result.error.contains(QStringLiteral("special files")), qPrintable(result.error));
    }

    void fifoModeNeverCreatesAFifo()
    {
        // libarchive (3.8) maps a FIFO mode in a ZIP to a regular file: the
        // entry is extracted as plain data, never as a FIFO.
        Sandbox box;
        QList<Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append({QStringLiteral("cat-frames/fifo"), "x", AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QVERIFY(archivefixture::patchUnixMode(box.archive, QStringLiteral("cat-frames/fifo"), S_IFIFO | 0644));
        const auto result = box.extract();
        const QString path = QDir(box.out).filePath(QStringLiteral("cat-frames/fifo"));
        if (result.runner) {
            struct stat st {};
            QCOMPARE(::lstat(QFile::encodeName(path).constData(), &st), 0);
            QVERIFY(S_ISREG(st.st_mode));
        } else {
            QVERIFY(result.error.contains(QStringLiteral("special files")));
        }
    }

    void entryCountLimit()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("cat"), 5)));
        ArchiveLimits limits;
        limits.maxEntries = 4; // 1 directory + 5 files
        const auto result = box.extract(limits);
        QVERIFY(!result.runner);
        QVERIFY(result.error.contains(QStringLiteral("entries")));
    }

    void singleFileSizeLimit()
    {
        Sandbox box;
        // 256 KiB of zeros: tiny once compressed, large when extracted.
        QList<Entry> entries{{QStringLiteral("bomb.png"), QByteArray(256 * 1024, '\0'), AE_IFREG, {}}};
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        QVERIFY(QFileInfo(box.archive).size() < 4096);
        ArchiveLimits limits;
        limits.maxFileBytes = 64 * 1024;
        const auto result = box.extract(limits);
        QVERIFY(!result.runner);
        QVERIFY(result.error.contains(QStringLiteral("larger than")));
    }

    void totalSizeLimit()
    {
        Sandbox box;
        QList<Entry> entries;
        for (int i = 0; i < 8; ++i)
            entries.append({QStringLiteral("frame-%1.png").arg(i), QByteArray(32 * 1024, char('a' + i)), AE_IFREG, {}});
        QVERIFY(archivefixture::writeZip(box.archive, entries));
        ArchiveLimits limits;
        limits.maxTotalBytes = 100 * 1024; // each file fits, all together do not
        const auto result = box.extract(limits);
        QVERIFY(!result.runner);
        QVERIFY(result.error.contains(QStringLiteral("expands to more than")));
    }

    void archiveSizeLimit()
    {
        Sandbox box;
        QVERIFY(archivefixture::writeZip(box.archive, archivefixture::galleryEntries(QStringLiteral("cat"), 3)));
        ArchiveLimits limits;
        limits.maxArchiveBytes = 100;
        QCOMPARE(box.extract(limits).errorKind, RunnerError::Invalid);
    }

    void defaultLimitsFitRealRunners()
    {
        // Runner Gallery: up to 24 frames, at most 100x36 px; archives < 2 MiB.
        const ArchiveLimits limits;
        QVERIFY(limits.maxEntries >= 2 * aqtruncat::RunnerMaxFrames);
        QVERIFY(limits.maxFileBytes >= 2LL << 20);
        QVERIFY(limits.maxArchiveBytes >= 16LL << 20);
    }
};

QTEST_GUILESS_MAIN(TestRunnerArchive)
#include "tst_runnerarchive.moc"
