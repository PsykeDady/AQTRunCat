#include "archivefixture.h"
#include "cli/commandline.h"
#include "cli/runnercommands.h"
#include "runnerfixture.h"

#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::cli;

namespace {

Invocation parseOk(const QStringList &args)
{
    const ParseResult result = parseCommandLine(args);
    if (!result.invocation)
        qWarning("unexpected parse error for %s: %s", qPrintable(args.join(u' ')), qPrintable(result.error));
    if (result.invocation)
        return *result.invocation;
    Invocation failed;
    failed.command = Command::Version; // never expected by the tests below
    failed.fps = -1;
    return failed;
}

// Runs a "runner …" command against temporary bundled/user roots and captures
// stdout and stderr separately.
struct CliWorld {
    QTemporaryDir tmp;
    QString bundled = tmp.filePath(QStringLiteral("bundled"));
    QString user = tmp.filePath(QStringLiteral("user"));
    aqtruncat::RunnerService service{{bundled, user}};
    QString out, err;

    CliWorld()
    {
        fixture::writeFrames(QDir(QDir(bundled).filePath(QStringLiteral("cat"))), fixture::galleryNames(QStringLiteral("cat"), 5));
        fixture::writeFrames(QDir(QDir(bundled).filePath(QStringLiteral("coffee"))), fixture::galleryNames(QStringLiteral("coffee"), 10), {38, 36});
    }

    int run(const QStringList &args)
    {
        out.clear();
        err.clear();
        const ParseResult parsed = parseCommandLine(args);
        if (!parsed.invocation)
            return ExitUsage;
        QTextStream o(&out), e(&err);
        switch (parsed.invocation->command) {
        case Command::RunnerList:
            return runnerList(service, QStringLiteral("cat"), o);
        case Command::RunnerImport:
            return runnerImport(service, *parsed.invocation, o, e);
        case Command::RunnerRemove:
            return runnerRemove(service, *parsed.invocation, o, e);
        default:
            return -1;
        }
    }
};

} // namespace

class TestCli : public QObject
{
    Q_OBJECT

private slots:
    // --- grammar -----------------------------------------------------------

    void bareInvocationRunsTheTray()
    {
        const Invocation i = parseOk({});
        QCOMPARE(i.command, Command::Run);
        QVERIFY(i.runnerId.isEmpty());
        QCOMPARE(i.fps, 0);
    }

    void runCommand()
    {
        QCOMPARE(parseOk({QStringLiteral("run")}).command, Command::Run);
        QCOMPARE(parseOk({QStringLiteral("run"), QStringLiteral("--fps"), QStringLiteral("20")}).fps, 20);
        QCOMPARE(parseOk({QStringLiteral("run"), QStringLiteral("--runner"), QStringLiteral("coffee")}).runnerId, QStringLiteral("coffee"));
        const Invocation all = parseOk({QStringLiteral("run"), QStringLiteral("--runner=coffee"), QStringLiteral("--fps=40"), QStringLiteral("--verbose")});
        QCOMPARE(all.runnerId, QStringLiteral("coffee"));
        QCOMPARE(all.fps, 40);
        QVERIFY(all.verbose);
    }

    void legacyShortForms()
    {
        // M1/M2 forms keep working as aliases.
        const Invocation fps = parseOk({QStringLiteral("--fps"), QStringLiteral("20")});
        QCOMPARE(fps.command, Command::Run);
        QCOMPARE(fps.fps, 20);
        const Invocation runner = parseOk({QStringLiteral("--runner"), QStringLiteral("coffee"), QStringLiteral("--verbose")});
        QCOMPARE(runner.command, Command::Run);
        QCOMPARE(runner.runnerId, QStringLiteral("coffee"));
        QVERIFY(runner.verbose);
        QCOMPARE(parseOk({QStringLiteral("--list-runners")}).command, Command::RunnerList);
    }

    void runnerCommands()
    {
        QCOMPARE(parseOk({QStringLiteral("runner"), QStringLiteral("list")}).command, Command::RunnerList);

        const Invocation import = parseOk({QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("foo.zip")});
        QCOMPARE(import.command, Command::RunnerImport);
        QCOMPARE(import.archivePath, QStringLiteral("foo.zip"));
        QVERIFY(import.id.isEmpty());
        QVERIFY(!import.force);

        const Invocation forced = parseOk({QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("--force"),
                                           QStringLiteral("foo.zip"), QStringLiteral("--id"), QStringLiteral("bar")});
        QCOMPARE(forced.archivePath, QStringLiteral("foo.zip"));
        QCOMPARE(forced.id, QStringLiteral("bar"));
        QVERIFY(forced.force);

        // "--" allows file names starting with a dash.
        QCOMPARE(parseOk({QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("--"), QStringLiteral("-odd.zip")}).archivePath,
                 QStringLiteral("-odd.zip"));

        const Invocation remove = parseOk({QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("foo")});
        QCOMPARE(remove.command, Command::RunnerRemove);
        QCOMPARE(remove.id, QStringLiteral("foo"));
    }

    void autostartCommands()
    {
        QCOMPARE(parseOk({"autostart", "enable"}).command, Command::AutostartEnable);
        QCOMPARE(parseOk({"autostart", "disable"}).command, Command::AutostartDisable);
        QCOMPARE(parseOk({"autostart", "status"}).command, Command::AutostartStatus);
        QCOMPARE(parseOk({"autostart", "--help"}).command, Command::Help);
        QVERIFY(parseOk({"autostart", "-h"}).helpText.contains(QStringLiteral("XDG autostart entry")));
        QCOMPARE(parseOk({"autostart", "enable", "--help"}).command, Command::Help);
        for (const QStringList &bad : {QStringList{"autostart"}, QStringList{"autostart", "on"},
                                       QStringList{"autostart", "enable", "now"},
                                       QStringList{"autostart", "status", "--verbose"}}) {
            const ParseResult result = parseCommandLine(bad);
            QVERIFY2(!result.invocation, qPrintable(bad.join(u' ')));
            QVERIFY(result.hint.contains(QStringLiteral("autostart --help")));
        }
        QVERIFY(mainHelp().contains(QStringLiteral("aqtruncat autostart")));
    }

    void helpForEveryCommand_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<QString>("expected");
        QTest::newRow("main") << QStringList{QStringLiteral("--help")} << QStringLiteral("aqtruncat runner <command>");
        QTest::newRow("main -h") << QStringList{QStringLiteral("-h")} << QStringLiteral("Exit status");
        QTest::newRow("run") << QStringList{QStringLiteral("run"), QStringLiteral("--help")} << QStringLiteral("Usage: aqtruncat run");
        QTest::newRow("runner") << QStringList{QStringLiteral("runner"), QStringLiteral("--help")} << QStringLiteral("Usage: aqtruncat runner <command>");
        QTest::newRow("list") << QStringList{QStringLiteral("runner"), QStringLiteral("list"), QStringLiteral("--help")} << QStringLiteral("Usage: aqtruncat runner list");
        QTest::newRow("import") << QStringList{QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("--help")} << QStringLiteral("Usage: aqtruncat runner import");
        QTest::newRow("remove") << QStringList{QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("-h")} << QStringLiteral("Usage: aqtruncat runner remove");
    }

    void helpForEveryCommand()
    {
        QFETCH(QStringList, args);
        QFETCH(QString, expected);
        const Invocation i = parseOk(args);
        QCOMPARE(i.command, Command::Help);
        QVERIFY2(i.helpText.contains(expected), qPrintable(i.helpText));
        QVERIFY(i.helpText.count(u'\n') < 30); // short
    }

    void version()
    {
        QCOMPARE(parseOk({QStringLiteral("--version")}).command, Command::Version);
    }

    void usageErrors_data()
    {
        QTest::addColumn<QStringList>("args");
        QTest::addColumn<QString>("message");
        QTest::newRow("unknown command") << QStringList{QStringLiteral("frobnicate")} << QStringLiteral("unknown command");
        QTest::newRow("missing runner command") << QStringList{QStringLiteral("runner")} << QStringLiteral("missing runner command");
        QTest::newRow("unknown runner command") << QStringList{QStringLiteral("runner"), QStringLiteral("rename")} << QStringLiteral("unknown runner command");
        QTest::newRow("import without file") << QStringList{QStringLiteral("runner"), QStringLiteral("import")} << QStringLiteral("missing archive");
        QTest::newRow("import two files") << QStringList{QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("a.zip"), QStringLiteral("b.zip")} << QStringLiteral("exactly one");
        QTest::newRow("--id without value") << QStringList{QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("a.zip"), QStringLiteral("--id")} << QStringLiteral("needs a value");
        QTest::newRow("remove without id") << QStringList{QStringLiteral("runner"), QStringLiteral("remove")} << QStringLiteral("missing runner id");
        QTest::newRow("fps zero") << QStringList{QStringLiteral("run"), QStringLiteral("--fps"), QStringLiteral("0")} << QStringLiteral("--fps");
        QTest::newRow("fps text") << QStringList{QStringLiteral("run"), QStringLiteral("--fps"), QStringLiteral("fast")} << QStringLiteral("--fps");
        QTest::newRow("fps too high") << QStringList{QStringLiteral("--fps"), QStringLiteral("121")} << QStringLiteral("--fps");
        QTest::newRow("fps missing") << QStringList{QStringLiteral("run"), QStringLiteral("--fps")} << QStringLiteral("needs a value");
        QTest::newRow("unknown option") << QStringList{QStringLiteral("run"), QStringLiteral("--colour")} << QStringLiteral("unknown option");
        QTest::newRow("flag with value") << QStringList{QStringLiteral("run"), QStringLiteral("--verbose=yes")} << QStringLiteral("does not take a value");
        QTest::newRow("repeated option") << QStringList{QStringLiteral("run"), QStringLiteral("--fps"), QStringLiteral("10"), QStringLiteral("--fps"), QStringLiteral("20")} << QStringLiteral("more than once");
        QTest::newRow("stray argument") << QStringList{QStringLiteral("run"), QStringLiteral("now")} << QStringLiteral("unexpected argument");
        QTest::newRow("list extra") << QStringList{QStringLiteral("--list-runners"), QStringLiteral("x")} << QStringLiteral("no other arguments");
        QTest::newRow("empty runner id") << QStringList{QStringLiteral("run"), QStringLiteral("--runner=")} << QStringLiteral("non-empty");
    }

    void usageErrors()
    {
        QFETCH(QStringList, args);
        QFETCH(QString, message);
        const ParseResult result = parseCommandLine(args);
        QVERIFY(!result.invocation);
        QVERIFY2(result.error.contains(message), qPrintable(result.error));
        QVERIFY(!result.hint.isEmpty());
    }

    // --- runner commands -----------------------------------------------------

    void exitCodes()
    {
        QCOMPARE(exitCodeFor(aqtruncat::RunnerError::None), 0);
        QCOMPARE(exitCodeFor(aqtruncat::RunnerError::Filesystem), 1);
        QCOMPARE(exitCodeFor(aqtruncat::RunnerError::NotFound), 3);
        QCOMPARE(exitCodeFor(aqtruncat::RunnerError::Invalid), 4);
        QCOMPARE(exitCodeFor(aqtruncat::RunnerError::Conflict), 5);
    }

    void unknownRunnerForRunIsNotFound()
    {
        CliWorld w;
        const auto choice = w.service.choose(QStringLiteral("dragon"), QStringLiteral("cat"));
        QCOMPARE(exitCodeFor(choice.errorKind), int(ExitNotFound));
        QVERIFY(choice.error.contains(QStringLiteral("dragon")));
    }

    void listShowsValidAndInvalidRunners()
    {
        CliWorld w;
        QDir(w.user).mkpath(QStringLiteral("broken"));
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("list")}), 0);
        QVERIFY(w.out.contains(QStringLiteral("cat *")));
        QVERIFY(w.out.contains(QStringLiteral("56x36")));
        QVERIFY(w.out.contains(QStringLiteral("coffee")));
        QVERIFY(w.out.contains(QStringLiteral("bundled")));
        QVERIFY(w.out.contains(QStringLiteral("broken")));
        QVERIFY(w.out.contains(QStringLiteral("INVALID")));
        QVERIFY(w.err.isEmpty());
        // Same output through the legacy alias.
        const QString viaCommand = w.out;
        QCOMPARE(w.run({QStringLiteral("--list-runners")}), 0);
        QCOMPARE(w.out, viaCommand);
    }

    void importListRemoveThroughTheCli()
    {
        CliWorld w;
        const QString archive = w.tmp.filePath(QStringLiteral("beagle-frames.zip"));
        QVERIFY(archivefixture::writeZip(archive, archivefixture::galleryEntries(QStringLiteral("beagle"), 9, {70, 36})));

        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), archive}), 0);
        QVERIFY(w.out.contains(QStringLiteral("Imported runner \"beagle\"")));
        QVERIFY(w.out.contains(QStringLiteral("aqtruncat run --runner beagle")));

        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("list")}), 0);
        QVERIFY(w.out.contains(QStringLiteral("beagle")));
        QVERIFY(w.out.contains(QStringLiteral("70x36")));

        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), archive}), int(ExitConflict));
        QVERIFY(w.out.isEmpty());
        QVERIFY(w.err.contains(QStringLiteral("already installed")));
        QVERIFY(w.err.contains(QStringLiteral("--force")));
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), QStringLiteral("--force"), archive}), 0);

        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("beagle")}), 0);
        QVERIFY(w.out.contains(QStringLiteral("Removed user runner \"beagle\"")));
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("beagle")}), int(ExitNotFound));
        QVERIFY(w.err.contains(QStringLiteral("no user runner")));
    }

    void importErrorsAndExitCodes()
    {
        CliWorld w;
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), w.tmp.filePath(QStringLiteral("missing.zip"))}), int(ExitNotFound));
        QVERIFY(w.err.startsWith(QStringLiteral("aqtruncat: cannot import")));

        const QString text = w.tmp.filePath(QStringLiteral("text.zip"));
        fixture::writeFile(text, "not a zip");
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), text}), int(ExitInvalid));
        QVERIFY(w.out.isEmpty());
    }

    void bundledRunnerRemovalIsAConflict()
    {
        CliWorld w;
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("cat")}), int(ExitConflict));
        QVERIFY(w.err.contains(QStringLiteral("bundled")));
    }

    void overrideRemovalReportsBundledFallback()
    {
        CliWorld w;
        const QString archive = w.tmp.filePath(QStringLiteral("cat-frames.zip"));
        QVERIFY(archivefixture::writeZip(archive, archivefixture::galleryEntries(QStringLiteral("cat"), 4)));
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("import"), archive}), 0);
        QVERIFY(w.out.contains(QStringLiteral("precedence over the bundled runner")));
        QCOMPARE(w.run({QStringLiteral("runner"), QStringLiteral("remove"), QStringLiteral("cat")}), 0);
        QVERIFY(w.out.contains(QStringLiteral("bundled runner \"cat\" is used again")));
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
