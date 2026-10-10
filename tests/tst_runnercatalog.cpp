#include "runner/runnercatalog.h"
#include "runnerfixture.h"

#include <QTemporaryDir>
#include <QTest>

using aqtruncat::RunnerEntry;
using aqtruncat::RunnerSource;
using aqtruncat::chooseRunner;
using aqtruncat::discoverRunners;

namespace {

// bundled/ and user/ roots inside a temporary directory.
struct Roots {
    QTemporaryDir tmp;
    QString bundled = tmp.filePath(QStringLiteral("bundled"));
    QString user = tmp.filePath(QStringLiteral("user"));

    void add(const QString &root, const QString &dirName, int frames = 3, QSize size = {56, 36})
    {
        fixture::writeFrames(QDir(QDir(root).filePath(dirName)), fixture::galleryNames(QStringLiteral("x"), frames), size);
    }
};

} // namespace

class TestRunnerCatalog : public QObject
{
    Q_OBJECT

private slots:
    void discoversBundledAndUserRunners()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        roots.add(roots.bundled, QStringLiteral("coffee"));
        roots.add(roots.user, QStringLiteral("beagle"));
        const auto entries = discoverRunners(roots.bundled, roots.user);
        QCOMPARE(entries.size(), 3);
        QCOMPARE(entries[0].id, QStringLiteral("beagle")); // sorted by id
        QCOMPARE(entries[0].source, RunnerSource::User);
        QCOMPARE(entries[1].id, QStringLiteral("cat"));
        QCOMPARE(entries[1].source, RunnerSource::Bundled);
    }

    void missingRootsAreFine()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        QCOMPARE(discoverRunners(roots.bundled, roots.tmp.filePath(QStringLiteral("nope"))).size(), 1);
        QCOMPARE(discoverRunners(QString(), QString()).size(), 0);
    }

    void userRunnerReplacesBundledRunnerWithSameId()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"), 5);
        roots.add(roots.user, QStringLiteral("cat"), 7);
        QStringList notes;
        const auto entries = discoverRunners(roots.bundled, roots.user, &notes);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].source, RunnerSource::User);
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes[0].contains(QStringLiteral("replaces")));

        const auto choice = chooseRunner(entries, QString(), QStringLiteral("cat"));
        QVERIFY(choice.runner);
        QCOMPARE(choice.runner->frames.size(), 7); // the user's version is the one loaded
    }

    void galleryArchiveFolderNameMapsToRunnerId()
    {
        Roots roots;
        roots.add(roots.user, QStringLiteral("beagle-frames")); // a Gallery ZIP extracted as-is
        const auto entries = discoverRunners(roots.bundled, roots.user);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].id, QStringLiteral("beagle"));
        QCOMPARE(aqtruncat::runnerIdForDirectoryName(QStringLiteral("-frames")), QStringLiteral("-frames"));
    }

    void collisionInsideOneRootIsDeterministic()
    {
        Roots roots;
        roots.add(roots.user, QStringLiteral("beagle-frames"));
        roots.add(roots.user, QStringLiteral("beagle"));
        QStringList notes;
        const auto entries = discoverRunners(roots.bundled, roots.user, &notes);
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries[0].path.endsWith(QStringLiteral("/beagle"))); // natural order: "beagle" first
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes[0].contains(QStringLiteral("ignored")));
    }

    void hiddenDirectoriesAreSkipped()
    {
        Roots roots;
        roots.add(roots.user, QStringLiteral(".cache"));
        roots.add(roots.user, QStringLiteral("cat"));
        const auto entries = discoverRunners(roots.bundled, roots.user);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].id, QStringLiteral("cat"));
    }

    void userDirectoryFollowsXdgDataHome()
    {
        QTemporaryDir tmp;
        const QByteArray oldXdg = qgetenv("XDG_DATA_HOME");
        const QByteArray oldHome = qgetenv("HOME");

        qputenv("XDG_DATA_HOME", QFile::encodeName(tmp.filePath(QStringLiteral("xdg"))));
        QCOMPARE(aqtruncat::userRunnersDirectory(), tmp.filePath(QStringLiteral("xdg/AQTRunCat/runners")));

        qunsetenv("XDG_DATA_HOME");
        qputenv("HOME", QFile::encodeName(tmp.path()));
        QCOMPARE(aqtruncat::userRunnersDirectory(), tmp.filePath(QStringLiteral(".local/share/AQTRunCat/runners")));

        oldXdg.isNull() ? qunsetenv("XDG_DATA_HOME") : qputenv("XDG_DATA_HOME", oldXdg);
        qputenv("HOME", oldHome);
    }

    void requestedRunnerIsUsed()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        roots.add(roots.user, QStringLiteral("beagle"), 9, {70, 36});
        const auto choice = chooseRunner(discoverRunners(roots.bundled, roots.user), QStringLiteral("beagle"), QStringLiteral("cat"));
        QVERIFY(choice.runner);
        QCOMPARE(choice.runner->id, QStringLiteral("beagle"));
        QCOMPARE(choice.runner->frameSize, QSize(70, 36));
        QVERIFY(choice.notes.isEmpty());
    }

    void unknownRequestedRunnerFailsWithoutFallback()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        const auto choice = chooseRunner(discoverRunners(roots.bundled, roots.user), QStringLiteral("dragon"), QStringLiteral("cat"));
        QVERIFY(!choice.runner);
        QVERIFY(choice.error.contains(QStringLiteral("unknown runner \"dragon\"")));
        QVERIFY(choice.error.contains(QStringLiteral("cat"))); // lists what is available
    }

    void invalidRequestedRunnerFailsWithoutFallback()
    {
        Roots roots;
        roots.add(roots.bundled, QStringLiteral("cat"));
        QDir(roots.user).mkpath(QStringLiteral("broken"));
        const auto choice = chooseRunner(discoverRunners(roots.bundled, roots.user), QStringLiteral("broken"), QStringLiteral("cat"));
        QVERIFY(!choice.runner);
        QVERIFY(choice.error.contains(QStringLiteral("invalid")));
    }

    void brokenDefaultFallsBackWithANote()
    {
        Roots roots;
        QDir(roots.user).mkpath(QStringLiteral("cat")); // user "cat" shadows nothing valid: empty
        roots.add(roots.bundled, QStringLiteral("coffee"));
        roots.add(roots.user, QStringLiteral("alpha"));
        const auto choice = chooseRunner(discoverRunners(roots.bundled, roots.user), QString(), QStringLiteral("cat"));
        QVERIFY(choice.runner);
        QCOMPARE(choice.runner->id, QStringLiteral("coffee")); // bundled runners are tried first
        QCOMPARE(choice.notes.size(), 2);
        QVERIFY(choice.notes[0].contains(QStringLiteral("invalid")));
    }

    void noValidRunnerIsAnError()
    {
        Roots roots;
        QDir(roots.user).mkpath(QStringLiteral("empty"));
        const auto choice = chooseRunner(discoverRunners(roots.bundled, roots.user), QString(), QStringLiteral("cat"));
        QVERIFY(!choice.runner);
        QVERIFY(!choice.error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRunnerCatalog)
#include "tst_runnercatalog.moc"
