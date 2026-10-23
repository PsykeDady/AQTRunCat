// F7: the bundled runners as shipped — the real frames and metadata that the
// build installs (share/aqtruncat/runners in the build tree) and compiles into
// aqtruncat: Cat and Coffee unchanged, Walking Goose, Party Parrot and Engine
// added, then Rickroll (v2F1, 2.1.1).
// Discovery, frames, order, names, selection, persistence, and what happens
// when an installation lacks a runner or ships a broken one.
#include "app/appsettings.h"
#include "app/runnerselection.h"
#include "runner/runnerloader.h"
#include "runner/runnerservice.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using aqtruncat::RunnerSource;

namespace {

QString installedRoot() { return QStringLiteral(AQTRUNCAT_BUILD_RUNNERS); }
QString sourceRoot() { return QStringLiteral(AQTRUNCAT_SOURCE_DIR); }

QByteArray sha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

bool copyRunner(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    for (const QString &name : QDir(from).entryList(QDir::Files))
        if (!QFile::copy(QDir(from).filePath(name), QDir(to).filePath(name)))
            return false;
    return true;
}

} // namespace

class TestBundledRunners : public QObject
{
    Q_OBJECT

private slots:
    void shipsTheFrames_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("displayName");
        QTest::addColumn<QString>("source");   // frames in the source tree
        QTest::addColumn<QString>("prefix");   // frame file names
        QTest::addColumn<int>("frames");
        QTest::addColumn<QSize>("size");
        QTest::newRow("cat") << "cat" << "Cat" << "third_party/runcatneo/runners/cat" << "cat" << 5 << QSize(56, 36);
        QTest::newRow("coffee") << "coffee" << "Coffee" << "third_party/runcatneo/runners/coffee" << "coffee" << 10 << QSize(38, 36);
        QTest::newRow("walkinggoose") << "walkinggoose" << "Walking Goose" << "third_party/walkinggoose/runners/goose"
                                      << "goose" << 6 << QSize(24, 36);
        QTest::newRow("partyparrot") << "partyparrot" << "Party Parrot" << "third_party/partyparrot/runners/parrot"
                                     << "parrot" << 10 << QSize(50, 36);
        QTest::newRow("engine") << "engine" << "Engine" << "third_party/runcatneo/runners/engine" << "engine" << 10 << QSize(83, 36);
        QTest::newRow("rickroll") << "rickroll" << "Rickroll" << "third_party/rickroll/runners/rickroll" << "rickroll" << 31
                                  << QSize(33, 36);
    }

    void shipsTheFrames()
    {
        QFETCH(QString, id);
        QFETCH(QString, displayName);
        QFETCH(QString, source);
        QFETCH(QString, prefix);
        QFETCH(int, frames);
        QFETCH(QSize, size);

        const QString dir = QDir(installedRoot()).filePath(id);
        const aqtruncat::RunnerLoadResult result = aqtruncat::loadRunner(dir, id, RunnerSource::Bundled);
        QVERIFY2(result.isValid(), qPrintable(result.error));
        const aqtruncat::Runner &runner = *result.runner;
        QCOMPARE(runner.id, id);
        QCOMPARE(runner.displayName, displayName);
        QVERIFY2(runner.warnings.isEmpty(), qPrintable(runner.warnings.join(u'\n')));
        QVERIFY(runner.monochrome);
        QCOMPARE(runner.frames.size(), frames);
        QCOMPARE(runner.frameSize, size);

        // Natural order of the files, played in that order and looped.
        QList<int> sequence;
        for (int i = 0; i < frames; ++i) {
            sequence.append(i);
            QCOMPARE(QFileInfo(runner.frameFiles.at(i)).fileName(), QStringLiteral("%1-frame-%2.png").arg(prefix).arg(i));
            // The installed file is the source file, byte for byte.
            const QString original = QDir(sourceRoot()).filePath(QStringLiteral("%1/%2-frame-%3.png").arg(source, prefix).arg(i));
            QCOMPARE(sha256(runner.frameFiles.at(i)), sha256(original));
        }
        QCOMPARE(runner.sequence, sequence);

        // Transparent background, a drawing on it: no opaque box.
        for (const QImage &frame : runner.frames) {
            const QImage image = frame.convertToFormat(QImage::Format_ARGB32);
            int transparent = 0, visible = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const int alpha = qAlpha(image.pixel(x, y));
                    transparent += alpha == 0;
                    visible += alpha > 0;
                }
            }
            QVERIFY(transparent > image.width() * image.height() / 4);
            QVERIFY(visible > 0);
        }
    }

    // What every frontend lists: aqtruncat (frames compiled in) and the
    // backend's catalogue directory (the files above).
    void listedByTheApplication()
    {
        QTemporaryDir home;
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("HOME"), home.path());
        env.insert(QStringLiteral("XDG_DATA_HOME"), home.filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), home.filePath(QStringLiteral("config")));
        process.setProcessEnvironment(env);
        process.start(QStringLiteral(AQTRUNCAT_APP), {QStringLiteral("runner"), QStringLiteral("list")});
        QVERIFY(process.waitForFinished(20000));
        QCOMPARE(process.exitCode(), 0);
        const QString out = QString::fromUtf8(process.readAllStandardOutput());
        QVERIFY2(out.contains(QRegularExpression(QStringLiteral("^cat \\*\\s+Cat\\s+bundled\\s+5\\s+56x36"),
                                                 QRegularExpression::MultilineOption)), qPrintable(out));
        QVERIFY(out.contains(QRegularExpression(QStringLiteral("^coffee\\s+Coffee\\s+bundled\\s+10\\s+38x36"),
                                                QRegularExpression::MultilineOption)));
        QVERIFY(out.contains(QRegularExpression(QStringLiteral("^walkinggoose\\s+Walking Goose\\s+bundled\\s+6\\s+24x36\\s+monochrome"),
                                                QRegularExpression::MultilineOption)));
        QVERIFY(out.contains(QRegularExpression(QStringLiteral("^partyparrot\\s+Party Parrot\\s+bundled\\s+10\\s+50x36\\s+monochrome"),
                                                QRegularExpression::MultilineOption)));
        QVERIFY(out.contains(QRegularExpression(QStringLiteral("^engine\\s+Engine\\s+bundled\\s+10\\s+83x36\\s+monochrome"),
                                                QRegularExpression::MultilineOption)));
        QVERIFY(out.contains(QRegularExpression(QStringLiteral("^rickroll\\s+Rickroll\\s+bundled\\s+31\\s+33x36\\s+monochrome"),
                                                QRegularExpression::MultilineOption)));

        QTemporaryDir user;
        const aqtruncat::RunnerService runners({installedRoot(), user.path()});
        QStringList ids;
        for (const aqtruncat::ListedRunner &listed : runners.list()) {
            QVERIFY(listed.result.isValid());
            ids.append(listed.entry.id);
        }
        ids.sort();
        QCOMPARE(ids, (QStringList{QStringLiteral("cat"), QStringLiteral("coffee"), QStringLiteral("engine"),
                                   QStringLiteral("partyparrot"), QStringLiteral("rickroll"), QStringLiteral("walkinggoose")}));
    }

    void selectionPersists()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("AQTRunCat.ini"));
        {
            aqtruncat::AppSettings settings(QStringLiteral("cat"), ini);
            QCOMPARE(settings.runnerId(), QStringLiteral("cat")); // the default is unchanged
            settings.setRunnerId(QStringLiteral("partyparrot"));
        }
        aqtruncat::AppSettings reloaded(QStringLiteral("cat"), ini);
        QCOMPARE(reloaded.runnerId(), QStringLiteral("partyparrot"));
        reloaded.restoreDefaults();
        QCOMPARE(reloaded.runnerId(), QStringLiteral("cat")); // Restore Defaults: back to Cat

        QTemporaryDir user;
        const aqtruncat::RunnerService runners({installedRoot(), user.path()});
        for (const QString &id : {QStringLiteral("walkinggoose"), QStringLiteral("partyparrot"), QStringLiteral("engine"),
                                  QStringLiteral("rickroll")}) {
            const StartupRunnerSelection startup = selectStartupRunner(runners, id, QStringLiteral("cat"), false);
            QVERIFY(startup.choice.runner);
            QCOMPARE(startup.choice.runner->id, id);
            QVERIFY(!startup.repairPersistedRunner);
            QVERIFY(selectStartupRunner(runners, id, QStringLiteral("cat"), true).choice.runner); // --runner
        }
    }

    // An installation without a runner, or with a broken one: the default
    // runner is used, nothing crashes; only a missing id repairs the setting.
    void missingOrBrokenRunnerFallsBack()
    {
        QTemporaryDir bundled, user;
        QVERIFY(copyRunner(QDir(installedRoot()).filePath(QStringLiteral("cat")), bundled.filePath(QStringLiteral("cat"))));
        QVERIFY(QDir().mkpath(bundled.filePath(QStringLiteral("walkinggoose"))));
        QFile broken(bundled.filePath(QStringLiteral("walkinggoose/goose-frame-0.png")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not a png");
        broken.close();
        const aqtruncat::RunnerService runners({bundled.path(), user.path()});

        StartupRunnerSelection missing = selectStartupRunner(runners, QStringLiteral("partyparrot"), QStringLiteral("cat"), false);
        QVERIFY(missing.choice.runner);
        QCOMPARE(missing.choice.runner->id, QStringLiteral("cat"));
        QVERIFY(missing.repairPersistedRunner);
        QVERIFY(!missing.choice.notes.isEmpty());

        StartupRunnerSelection invalid = selectStartupRunner(runners, QStringLiteral("walkinggoose"), QStringLiteral("cat"), false);
        QVERIFY(invalid.choice.runner);
        QCOMPARE(invalid.choice.runner->id, QStringLiteral("cat"));
        QVERIFY(!invalid.repairPersistedRunner); // may be temporary: the setting is kept
        QVERIFY(!invalid.choice.notes.isEmpty());

        // Asked for explicitly (--runner): an error, never another runner.
        QVERIFY(!selectStartupRunner(runners, QStringLiteral("partyparrot"), QStringLiteral("cat"), true).choice.runner);
    }
};

QTEST_GUILESS_MAIN(TestBundledRunners)
#include "tst_bundledrunners.moc"
