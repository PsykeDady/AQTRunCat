#include "archivefixture.h"
#include "runner/runnerservice.h"
#include "runnerfixture.h"

#include <QDirIterator>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using aqtruncat::ImportOptions;
using aqtruncat::RunnerError;
using aqtruncat::RunnerService;
using aqtruncat::RunnerSource;

namespace {

// bundled/ (with a 5-frame "cat"), user/, a private TMPDIR and archives/.
struct World {
    QTemporaryDir tmp;
    QString bundled = tmp.filePath(QStringLiteral("bundled"));
    QString user = tmp.filePath(QStringLiteral("user"));
    QString temp = tmp.filePath(QStringLiteral("tmp"));
    QByteArray oldTmpDir = qgetenv("TMPDIR");
    RunnerService service{{bundled, user}};

    World()
    {
        fixture::writeFrames(QDir(QDir(bundled).filePath(QStringLiteral("cat"))), fixture::galleryNames(QStringLiteral("cat"), 5));
        QDir().mkpath(temp);
        qputenv("TMPDIR", QFile::encodeName(temp)); // QTemporaryDir follows it
    }
    ~World() { oldTmpDir.isNull() ? qunsetenv("TMPDIR") : qputenv("TMPDIR", oldTmpDir); }

    QString zip(const QString &fileName, const QList<archivefixture::Entry> &entries)
    {
        QDir().mkpath(tmp.filePath(QStringLiteral("archives")));
        const QString path = tmp.filePath(QStringLiteral("archives/") + fileName);
        archivefixture::writeZip(path, entries);
        return path;
    }

    // Hidden leftovers (staging, backups, trash) in user/ and anything in TMPDIR.
    QStringList leftovers() const
    {
        QStringList found = QDir(user).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        found.erase(std::remove_if(found.begin(), found.end(), [](const QString &n) { return !n.startsWith(u'.'); }), found.end());
        found += QDir(temp).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        return found;
    }

    int framesOf(const QString &id) const
    {
        const auto choice = service.choose(id, QStringLiteral("cat"));
        return choice.runner ? int(choice.runner->frames.size()) : -1;
    }

    RunnerSource sourceOf(const QString &id) const
    {
        for (const auto &e : service.entries())
            if (e.id == id)
                return e.source;
        return RunnerSource::Bundled;
    }
};

bool listed(const RunnerService &service, const QString &id)
{
    for (const auto &e : service.entries())
        if (e.id == id)
            return true;
    return false;
}

} // namespace

class TestRunnerLifecycle : public QObject
{
    Q_OBJECT

private slots:
    void importSelectRemove()
    {
        World w;
        const QString archive = w.zip(QStringLiteral("beagle-frames.zip"), archivefixture::galleryEntries(QStringLiteral("beagle"), 9, {70, 36}));
        const auto imported = w.service.importArchive(archive);
        QVERIFY2(imported.ok(), qPrintable(imported.message));
        QCOMPARE(imported.runner->id, QStringLiteral("beagle"));
        QCOMPARE(imported.installedPath, QDir(w.user).filePath(QStringLiteral("beagle")));
        QVERIFY(!imported.overridesBundled);

        QVERIFY(listed(w.service, QStringLiteral("beagle")));
        QCOMPARE(w.sourceOf(QStringLiteral("beagle")), RunnerSource::User);
        const auto choice = w.service.choose(QStringLiteral("beagle"), QStringLiteral("cat"));
        QVERIFY(choice.runner);
        QCOMPARE(choice.runner->frameSize, QSize(70, 36));

        const auto removed = w.service.remove(QStringLiteral("beagle"));
        QVERIFY2(removed.ok(), qPrintable(removed.message));
        QVERIFY(!removed.bundledVisibleAgain);
        QVERIFY(!listed(w.service, QStringLiteral("beagle")));
        QCOMPARE(w.service.choose(QStringLiteral("beagle"), QStringLiteral("cat")).errorKind, RunnerError::NotFound);
        QVERIFY2(w.leftovers().isEmpty(), qPrintable(w.leftovers().join(u' ')));
    }

    void userOverrideThenRemoveRestoresBundled()
    {
        World w;
        QCOMPARE(w.framesOf(QStringLiteral("cat")), 5);
        QCOMPARE(w.sourceOf(QStringLiteral("cat")), RunnerSource::Bundled);

        const auto imported = w.service.importArchive(w.zip(QStringLiteral("cat-frames.zip"), archivefixture::galleryEntries(QStringLiteral("cat"), 8)));
        QVERIFY2(imported.ok(), qPrintable(imported.message));
        QVERIFY(imported.overridesBundled);
        QCOMPARE(w.sourceOf(QStringLiteral("cat")), RunnerSource::User);
        QCOMPARE(w.framesOf(QStringLiteral("cat")), 8);

        const auto removed = w.service.remove(QStringLiteral("cat"));
        QVERIFY(removed.ok());
        QVERIFY(removed.bundledVisibleAgain);
        QCOMPARE(w.sourceOf(QStringLiteral("cat")), RunnerSource::Bundled);
        QCOMPARE(w.framesOf(QStringLiteral("cat")), 5);
        QVERIFY(QFileInfo::exists(QDir(w.bundled).filePath(QStringLiteral("cat/cat-frame-0.png")))); // untouched
    }

    void bundledRunnerCannotBeRemoved()
    {
        World w;
        const auto removed = w.service.remove(QStringLiteral("cat"));
        QCOMPARE(removed.error, RunnerError::Conflict);
        QVERIFY(removed.message.contains(QStringLiteral("bundled")));
        QCOMPARE(w.framesOf(QStringLiteral("cat")), 5);
    }

    void unknownRunnerRemovalIsNotFound()
    {
        World w;
        QCOMPARE(w.service.remove(QStringLiteral("dragon")).error, RunnerError::NotFound);
    }

    void ambiguousUserIdIsNotRemoved()
    {
        World w;
        fixture::writeFrames(QDir(QDir(w.user).filePath(QStringLiteral("deer"))), fixture::galleryNames(QStringLiteral("deer"), 3));
        fixture::writeFrames(QDir(QDir(w.user).filePath(QStringLiteral("deer-frames"))), fixture::galleryNames(QStringLiteral("deer"), 3));
        const auto removed = w.service.remove(QStringLiteral("deer"));
        QCOMPARE(removed.error, RunnerError::Conflict);
        QVERIFY(QFileInfo::exists(QDir(w.user).filePath(QStringLiteral("deer"))));
        QVERIFY(QFileInfo::exists(QDir(w.user).filePath(QStringLiteral("deer-frames"))));
    }

    void userUserCollisionFailsWithoutForce()
    {
        World w;
        QVERIFY(w.service.importArchive(w.zip(QStringLiteral("a.zip"), archivefixture::galleryEntries(QStringLiteral("deer"), 3))).ok());
        const auto again = w.service.importArchive(w.zip(QStringLiteral("b.zip"), archivefixture::galleryEntries(QStringLiteral("deer"), 7)));
        QCOMPARE(again.error, RunnerError::Conflict);
        QVERIFY(again.message.contains(QStringLiteral("already installed")));
        QCOMPARE(w.framesOf(QStringLiteral("deer")), 3); // untouched
        QVERIFY(w.leftovers().isEmpty());
    }

    void forceReplacesUserRunner()
    {
        World w;
        // The existing one was copied by hand as "deer-frames/".
        fixture::writeFrames(QDir(QDir(w.user).filePath(QStringLiteral("deer-frames"))), fixture::galleryNames(QStringLiteral("deer"), 3));
        ImportOptions options;
        options.replaceExisting = true;
        const auto replaced = w.service.importArchive(w.zip(QStringLiteral("deer-frames.zip"), archivefixture::galleryEntries(QStringLiteral("deer"), 7)), options);
        QVERIFY2(replaced.ok(), qPrintable(replaced.message));
        QCOMPARE(replaced.replacedUserPath, QDir(w.user).filePath(QStringLiteral("deer-frames")));
        QCOMPARE(w.framesOf(QStringLiteral("deer")), 7);
        QVERIFY(!QFileInfo::exists(QDir(w.user).filePath(QStringLiteral("deer-frames"))));
        QVERIFY2(w.leftovers().isEmpty(), qPrintable(w.leftovers().join(u' ')));
    }

    void failedReplacementKeepsTheOldRunner()
    {
        if (::geteuid() == 0)
            QSKIP("file permissions are not enforced for root");
        World w;
        fixture::writeFrames(QDir(QDir(w.user).filePath(QStringLiteral("deer"))), fixture::galleryNames(QStringLiteral("deer"), 3));
        // A validated source whose copy fails half-way (unreadable file).
        const QDir source(w.tmp.filePath(QStringLiteral("source")));
        fixture::writeFrames(source, fixture::galleryNames(QStringLiteral("deer"), 6));
        QFile locked(source.filePath(QStringLiteral("deer-frame-4.png")));
        QVERIFY(locked.setPermissions(QFileDevice::Permissions()));

        const auto result = aqtruncat::installRunner(source.path(), QStringLiteral("deer"), w.user, true);
        locked.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(result.error, RunnerError::Filesystem);
        QCOMPARE(w.framesOf(QStringLiteral("deer")), 3); // old runner intact
        QVERIFY2(w.leftovers().isEmpty(), qPrintable(w.leftovers().join(u' ')));
    }

    void invalidRunnerArchiveInstallsNothing()
    {
        World w;
        // Valid ZIP, but frames of different sizes.
        QList<archivefixture::Entry> entries = archivefixture::galleryEntries(QStringLiteral("odd"), 2);
        entries.append({QStringLiteral("odd-frames/odd-frame-2.png"), archivefixture::png({60, 36}), AE_IFREG, {}});
        const auto result = w.service.importArchive(w.zip(QStringLiteral("odd.zip"), entries));
        QCOMPARE(result.error, RunnerError::Invalid);
        QVERIFY(result.message.contains(QStringLiteral("same size")));
        QVERIFY(!listed(w.service, QStringLiteral("odd")));
        QVERIFY(QDir(w.user).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(w.leftovers().isEmpty());
    }

    void archiveWithoutFramesIsInvalid()
    {
        World w;
        const auto result = w.service.importArchive(w.zip(QStringLiteral("notes.zip"), {{QStringLiteral("readme.txt"), "hello", AE_IFREG, {}}}));
        QCOMPARE(result.error, RunnerError::Invalid);
        QVERIFY(result.message.contains(QStringLiteral("no PNG frames")));
    }

    void archiveWithSeveralRunnersIsRejected()
    {
        World w;
        QList<archivefixture::Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries += archivefixture::galleryEntries(QStringLiteral("dog"), 2);
        const auto result = w.service.importArchive(w.zip(QStringLiteral("two.zip"), entries));
        QCOMPARE(result.error, RunnerError::Invalid);
        QVERIFY(result.message.contains(QStringLiteral("several")));
        QVERIFY(w.leftovers().isEmpty());
    }

    void hostileArchiveInstallsNothing()
    {
        World w;
        QList<archivefixture::Entry> entries = archivefixture::galleryEntries(QStringLiteral("cat"), 2);
        entries.append({QStringLiteral("../../evil.png"), archivefixture::png(), AE_IFREG, {}});
        const auto result = w.service.importArchive(w.zip(QStringLiteral("evil.zip"), entries));
        QCOMPARE(result.error, RunnerError::Invalid);
        QVERIFY(!QFileInfo::exists(w.tmp.filePath(QStringLiteral("evil.png"))));
        QVERIFY(QDir(w.user).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(w.leftovers().isEmpty());
    }

    void idOverrideAndInvalidIds()
    {
        World w;
        ImportOptions options;
        options.id = QStringLiteral("my-beagle");
        const auto imported = w.service.importArchive(w.zip(QStringLiteral("beagle-frames.zip"), archivefixture::galleryEntries(QStringLiteral("beagle"), 3)), options);
        QVERIFY2(imported.ok(), qPrintable(imported.message));
        QVERIFY(listed(w.service, QStringLiteral("my-beagle")));

        for (const QString &bad : {QStringLiteral("../x"), QStringLiteral(".hidden"), QStringLiteral("a b"), QStringLiteral("a/b")}) {
            options.id = bad;
            const auto rejected = w.service.importArchive(w.zip(QStringLiteral("x.zip"), archivefixture::galleryEntries(QStringLiteral("x"), 2)), options);
            QCOMPARE(rejected.error, RunnerError::Invalid);
        }
        // Archive names that are not usable ids need --id.
        const auto weird = w.service.importArchive(w.zip(QStringLiteral("My Runner (1).zip"),
                                                         {{QStringLiteral("0.png"), archivefixture::png(), AE_IFREG, {}}}));
        QCOMPARE(weird.error, RunnerError::Invalid);
        QVERIFY(weird.message.contains(QStringLiteral("--id")));
        QVERIFY(w.leftovers().isEmpty());
    }

    void validIds()
    {
        QVERIFY(aqtruncat::isValidRunnerId(QStringLiteral("cat")));
        QVERIFY(aqtruncat::isValidRunnerId(QStringLiteral("border-collie")));
        QVERIFY(aqtruncat::isValidRunnerId(QStringLiteral("runner_2.0")));
        QVERIFY(!aqtruncat::isValidRunnerId(QString()));
        QVERIFY(!aqtruncat::isValidRunnerId(QStringLiteral("..")));
        QVERIFY(!aqtruncat::isValidRunnerId(QStringLiteral("-cat")));
        QVERIFY(!aqtruncat::isValidRunnerId(QString(101, u'a')));
    }
};

QTEST_GUILESS_MAIN(TestRunnerLifecycle)
#include "tst_runnerlifecycle.moc"
