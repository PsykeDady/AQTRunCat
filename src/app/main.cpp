// AQTRunCat — A Cute Run Cat for Linux.
//
// Entry point: parse the command line (src/cli), then dispatch. "run" starts
// the tray application; "runner …" commands use RunnerService, the same
// logic also used by the Settings UI.

#include "appsettings.h"
#include "autostartcommand.h"
#include "autostartmanager.h"
#include "cli/commandline.h"
#include "cli/metricscommand.h"
#include "cli/runnercommands.h"
#include "runner/runnercatalog.h"
#include "trayapp.h"

#include <QApplication>
#include <QTextStream>

#include <cstdio>

namespace {

// Bundled runners are compiled in as Qt resources (see src/CMakeLists.txt).
const QString BundledRunnersRoot = QStringLiteral(":/runners");
const QString DefaultRunnerId = QStringLiteral(AQTRUNCAT_DEFAULT_RUNNER);

aqtruncat::RunnerService runnerService()
{
    return aqtruncat::RunnerService({BundledRunnersRoot, aqtruncat::userRunnersDirectory()});
}

} // namespace

int main(int argc, char *argv[])
{
    QStringList arguments;
    for (int i = 1; i < argc; ++i)
        arguments.append(QString::fromLocal8Bit(argv[i]));

    using namespace aqtruncat::cli;
    const ParseResult parsed = parseCommandLine(arguments);
    if (!parsed.invocation) {
        // Command-line errors always go to stderr (Qt logging may go to journald).
        std::fprintf(stderr, "aqtruncat: %s\n", qPrintable(parsed.error));
        if (!parsed.hint.isEmpty())
            std::fprintf(stderr, "%s\n", qPrintable(parsed.hint));
        return ExitUsage;
    }
    const Invocation &invocation = *parsed.invocation;

    switch (invocation.command) {
    case Command::Help:
        std::fputs(qPrintable(invocation.helpText), stdout);
        return ExitOk;
    case Command::Version:
        std::printf("AQTRunCat %s\n", AQTRUNCAT_VERSION);
        return ExitOk;
    case Command::Run: {
        QApplication app(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("AQTRunCat"));
        QCoreApplication::setApplicationName(QStringLiteral("AQTRunCat"));
        QCoreApplication::setApplicationVersion(QStringLiteral(AQTRUNCAT_VERSION));
        return runTrayApplication(app, runnerService(),
                                  RunOptions{invocation.runnerId, invocation.fps, invocation.verbose, DefaultRunnerId});
    }
    case Command::Metrics: {
        // Plain C++ metrics layer: no QCoreApplication (and no process locale) needed.
        QTextStream out(stdout);
        QTextStream err(stderr);
        return runMetrics(invocation, out, err);
    }
    case Command::AutostartEnable:
    case Command::AutostartDisable:
    case Command::AutostartStatus: {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("AQTRunCat"));
        aqtruncat::AppSettings settings(DefaultRunnerId);
        const AutostartManager autostart;
        QTextStream out(stdout);
        QTextStream err(stderr);
        const std::optional<bool> enable = invocation.command == Command::AutostartStatus
            ? std::nullopt
            : std::optional<bool>(invocation.command == Command::AutostartEnable);
        return runAutostartCommand(enable, settings, autostart, out, err);
    }
    case Command::RunnerList:
    case Command::RunnerImport:
    case Command::RunnerRemove:
        break;
    }

    // Runner management needs no display.
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AQTRunCat"));
    QTextStream out(stdout);
    QTextStream err(stderr);
    const aqtruncat::RunnerService service = runnerService();
    switch (invocation.command) {
    case Command::RunnerList:
        return runnerList(service, DefaultRunnerId, out);
    case Command::RunnerImport:
        return runnerImport(service, invocation, out, err);
    case Command::RunnerRemove:
        return runnerRemove(service, invocation, out, err);
    default:
        return ExitFailure;
    }
}
