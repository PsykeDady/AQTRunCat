// aqtruncatd: the AQTRunCat backend (F4.1). One instance per user session,
// owner of io.github.psykedady.AQTRunCat on the D-Bus session bus. It needs
// no display, no tray and no Qt Widgets/Gui: QtCore + QtDBus only.
// Contract: docs/F4.1-headless-backend-ipc-cli.md.

#include "api/apischema.h"
#include "api/backendservice.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signalPipe[2] = {-1, -1};

void onUnixSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(signalPipe[1], &c, 1);
}

// The frontend and the adapters are installed next to this executable.
QString sibling(const QString &name)
{
    const QFileInfo file(QDir(QCoreApplication::applicationDirPath()).filePath(name));
    return file.isFile() && file.isExecutable() ? file.absoluteFilePath() : QStandardPaths::findExecutable(name);
}

void usage(FILE *stream)
{
    std::fputs("Usage: aqtruncatd [--verbose] | -h, --help | -v, --version\n"
               "\n"
               "AQTRunCat backend: samples the system metrics, runs Custom Metrics and the\n"
               "Claude Code / Codex integrations, computes the runner speed and keeps the\n"
               "settings, for every frontend at once (the tray application \"aqtruncat\",\n"
               "\"aqtruncatctl\", other clients). It serves the D-Bus session bus as\n"
               "io.github.psykedady.AQTRunCat and is normally started on demand by D-Bus\n"
               "activation; a second instance exits at once. It stops on SIGTERM/SIGINT\n"
               "or when the session ends.\n",
               stream);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("AQTRunCat"));
    QCoreApplication::setApplicationName(QStringLiteral("AQTRunCat"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AQTRUNCAT_VERSION));
    const QStringList args = app.arguments().mid(1);
    bool verbose = false;
    for (const QString &arg : args) {
        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            usage(stdout);
            return 0;
        }
        if (arg == QLatin1String("-v") || arg == QLatin1String("--version")) {
            std::printf("aqtruncatd %s\n", AQTRUNCAT_VERSION);
            return 0;
        }
        if (arg == QLatin1String("--verbose")) {
            verbose = true;
            continue;
        }
        std::fprintf(stderr, "aqtruncatd: unknown option \"%s\"\n", qPrintable(arg));
        usage(stderr);
        return 2;
    }
    if (verbose)
        QLoggingCategory::setFilterRules(QStringLiteral("aqtruncat.*.debug=true"));

    BackendService::Config config;
    config.defaultRunnerId = QStringLiteral(AQTRUNCAT_DEFAULT_RUNNER);
    config.homePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
    config.frontendExecutable = sibling(QStringLiteral("aqtruncat"));
    config.claudeAdapter = sibling(QStringLiteral("aqtruncat-claude-metric"));
    config.codexAdapter = sibling(QStringLiteral("aqtruncat-codex-metric"));
    BackendService backend(config);

    QDBusConnection bus = QDBusConnection::sessionBus();
    QString error;
    if (!backend.registerOn(bus, &error)) {
        if (bus.isConnected()) {
            // Single instance: someone else serves the session already.
            std::fprintf(stderr, "aqtruncatd: %s; nothing to do\n", qPrintable(error));
            return 0;
        }
        std::fprintf(stderr, "aqtruncatd: %s\n", qPrintable(error));
        return 1;
    }
    // The session bus going away (logout) ends the backend.
    bus.connect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                QStringLiteral("Disconnected"), &app, SLOT(quit()));
    backend.start();

    ::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe);
    QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    std::signal(SIGINT, onUnixSignal);
    std::signal(SIGTERM, onUnixSignal);
    const int status = app.exec();
    bus.unregisterService(aqtruncat::api::ServiceName);
    return status;
}
