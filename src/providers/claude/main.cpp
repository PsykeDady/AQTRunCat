// aqtruncat-claude-metric: F2 Claude Code adapter for F1 command-backed
// Custom Metrics. "snapshot" is what AQTRunCat runs; "enable"/"disable"/
// "status"/"live-updates" manage the integration (the AQTRunCat Settings
// window runs exactly these commands, so CLI and GUI share one
// configuration); "statusline" is what Claude Code runs when live updates are
// on. See docs/integration-claude-code.md.

#include "claudecode.h"
#include "claudeicon.h"
#include "claudeusage.h"
#include "statuslinebridge.h"
#include "common/package.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr int UsageError = 2;
const char ExecutableName[] = "aqtruncat-claude-metric";
const char MetricId[] = "claude-code";
const char IconFile[] = "claude-code.svg";
// Quotas change with every Claude response (live updates) or every /usage
// (cache); 10 s keeps the card current without polling. One run reads two
// small files (plus, with --context, a few more and one file tail).
constexpr int IntervalSeconds = 10;
constexpr int TimeoutSeconds = 5;

void printOut(const QString &text) { std::fputs(qPrintable(text), stdout); }
void printErr(const QString &text) { std::fputs(qPrintable(text + u'\n'), stderr); }

void usage(FILE *stream)
{
    const QString text = QStringLiteral(
        "Usage:\n"
        "  %1 snapshot [--context [--cwd DIR]] [--config-dir DIR]\n"
        "  %1 enable [--context [--cwd DIR]] [--config-dir DIR] [--force]\n"
        "  %1 disable\n"
        "  %1 upgrade\n"
        "  %1 status [--json] [--config-dir DIR]\n"
        "  %1 live-updates on|off [--config-dir DIR]\n"
        "  %1 -h | --help | -v | --version\n"
        "\n"
        "AQTRunCat Claude Code integration: your Claude plan usage, as on Claude\n"
        "Code's /usage screen (current 5-hour session window and current week, all\n"
        "models). Normally managed from AQTRunCat Settings > Integrations.\n"
        "\n"
        "  snapshot      print the metric as an AQTRunCat snapshot JSON (what\n"
        "                AQTRunCat runs every %2 s). Exit 0 with a snapshot, 69 when\n"
        "                Claude Code or any usage report is absent, 1 when its local\n"
        "                data cannot be read or is not understood.\n"
        "  enable        install the metric package in\n"
        "                $XDG_DATA_HOME/AQTRunCat/metrics/claude-code/ (AQTRunCat\n"
        "                shows it at once). It is not added to the Metrics Bar.\n"
        "  disable       turn live updates off (if on) and remove the package.\n"
        "  upgrade       rewrite an enabled integration for this version, keeping its\n"
        "                options (run by install.sh); does nothing when it is off.\n"
        "  status        show whether the integration and live updates are on.\n"
        "  live-updates  on: Claude Code reports usage to AQTRunCat after every\n"
        "                response. This sets \"statusLine\" in Claude Code's\n"
        "                settings.json; an existing status line keeps running and is\n"
        "                restored by \"off\". Without it, values update when /usage\n"
        "                is opened in Claude Code.\n"
        "\n"
        "  --context         also show the context window of the most recently\n"
        "                    active Claude Code session\n"
        "  --cwd DIR         with --context: only sessions started in DIR or below\n"
        "  --config-dir DIR  Claude Code data directory (default $CLAUDE_CONFIG_DIR\n"
        "                    or ~/.claude)\n"
        "  --force           let enable replace a claude-code package it did not write\n"
        "  --json            machine-readable status (used by AQTRunCat Settings)\n")
        .arg(QLatin1String(ExecutableName)).arg(IntervalSeconds);
    std::fputs(qPrintable(text), stream);
}

QString dataDirectory()
{
    return aqtruncat::providers::aqtruncatDataDirectory();
}

QString packageDirectory()
{
    return aqtruncat::providers::packageDirectory(QLatin1String(MetricId));
}

QString metricFile()
{
    return packageDirectory() + QStringLiteral("/metric.json");
}

aqtruncat::claude::BridgePaths bridgePaths(const QString &configDirectory)
{
    return {QDir(configDirectory).filePath(QStringLiteral("settings.json")),
            dataDirectory() + QStringLiteral("/integrations/claude-code/statusline-backup.json"),
            QCoreApplication::applicationFilePath()};
}

QJsonObject readMetric(const QString &path)
{
    return aqtruncat::providers::readJsonObject(path);
}

// A package is ours when its metric.json has our id and runs an executable
// named like this one; only such a package is replaced or removed.
bool managedPackage(const QJsonObject &metric)
{
    return aqtruncat::providers::isManagedPackage(metric, QLatin1String(MetricId), QLatin1String(ExecutableName));
}

bool writeAtomically(const QString &path, const QByteArray &contents)
{
    QString error;
    if (!aqtruncat::providers::writeFileAtomically(path, contents, &error)) {
        printErr(QStringLiteral("%1: %2").arg(QLatin1String(ExecutableName), error));
        return false;
    }
    return true;
}

struct CommandLine {
    QString command;
    QString subcommand; // live-updates on|off
    QString cwdFilter;
    QString configDirectory; // as given; empty: default
    bool context = false;
    bool force = false;
    bool json = false;
};

QString configDirectoryOf(const CommandLine &line)
{
    return line.configDirectory.isEmpty() ? aqtruncat::claude::defaultConfigDirectory() : line.configDirectory;
}

int enable(const CommandLine &line, bool announce = true)
{
    const QString directory = packageDirectory();
    const QString file = metricFile();
    if (QFileInfo::exists(file) && !managedPackage(readMetric(file)) && !line.force) {
        printErr(QStringLiteral("%1: %2 exists and was not written by %1; nothing changed (use --force to replace it)")
                     .arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    if (!QDir().mkpath(directory)) {
        printErr(QStringLiteral("%1: cannot create %2").arg(QLatin1String(ExecutableName), directory));
        return 1;
    }
    QJsonArray arguments{QStringLiteral("snapshot")};
    if (line.context)
        arguments.append(QStringLiteral("--context"));
    if (!line.cwdFilter.isEmpty()) {
        arguments.append(QStringLiteral("--cwd"));
        arguments.append(line.cwdFilter);
    }
    if (!line.configDirectory.isEmpty()) {
        arguments.append(QStringLiteral("--config-dir"));
        arguments.append(line.configDirectory);
    }
    QJsonObject metric{
        {QStringLiteral("id"), QLatin1String(MetricId)},
        // Provenance for AQTRunCat: shown under Integrations, not Custom Metrics.
        {QStringLiteral("integration"), QLatin1String(MetricId)},
        {QStringLiteral("title"), QStringLiteral("Claude Code")},
        {QStringLiteral("symbol"), QJsonObject{{QStringLiteral("type"), QStringLiteral("file")},
                                                {QStringLiteral("value"), QStringLiteral("./%1").arg(QLatin1String(IconFile))}}},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("command")},
             {QStringLiteral("executable"), QCoreApplication::applicationFilePath()},
             {QStringLiteral("arguments"), arguments},
             {QStringLiteral("output"), QStringLiteral("snapshot")},
             {QStringLiteral("intervalSeconds"), IntervalSeconds},
             {QStringLiteral("timeoutSeconds"), TimeoutSeconds}}},
    };
    metric.insert(QStringLiteral("dependencies"), QJsonArray{
        QJsonObject{{QStringLiteral("executable"), QStringLiteral("claude")},
                    {QStringLiteral("feature"), QStringLiteral("Claude Code usage integration")}}
    });
    // Icon first: the package becomes visible to AQTRunCat with metric.json.
    if (!writeAtomically(directory + u'/' + QLatin1String(IconFile), QByteArray(ClaudeMetricIconSvg))
        || !writeAtomically(file, QJsonDocument(metric).toJson(QJsonDocument::Indented)))
        return 1;
    if (announce) {
        printOut(QStringLiteral("Claude Code integration enabled: %1\n"
                                "AQTRunCat shows it in the Dashboard; add it to the Metrics Bar from Settings if wanted.\n")
                     .arg(directory));
    }
    return 0;
}

int liveUpdates(const QString &configDirectory, bool on)
{
    QString message;
    const auto paths = bridgePaths(configDirectory);
    const bool ok = on ? aqtruncat::claude::installBridge(paths, &message)
                       : aqtruncat::claude::removeBridge(paths, &message);
    if (!ok) {
        printErr(QStringLiteral("%1: %2").arg(QLatin1String(ExecutableName), message));
        return 1;
    }
    printOut(message + u'\n');
    return 0;
}

int disable(const QString &configDirectory)
{
    const QString file = metricFile();
    const QString directory = packageDirectory();
    const QJsonObject metric = readMetric(file);
    if (QFileInfo::exists(file) && !managedPackage(metric)) {
        printErr(QStringLiteral("%1: %2 was not written by %1; nothing removed")
                     .arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    // Live updates only serve this integration: the user's Claude Code
    // status line is put back before the package goes away.
    const auto paths = bridgePaths(configDirectory);
    const auto bridge = aqtruncat::claude::bridgeStatus(paths);
    if (bridge.state != aqtruncat::claude::BridgeState::Off && liveUpdates(configDirectory, false) != 0)
        return 1;
    if (!QFileInfo::exists(file)) {
        printOut(QStringLiteral("Claude Code integration is not enabled (%1 does not exist).\n").arg(file));
        return 0;
    }
    if (!QFile::remove(file)) {
        printErr(QStringLiteral("%1: cannot remove %2").arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    QFile::remove(directory + u'/' + QLatin1String(IconFile));
    QDir().rmdir(directory); // only when nothing else is left in it
    printOut(QStringLiteral("Claude Code integration disabled.\n"));
    return 0;
}

QString packageOption(const QJsonArray &arguments, const QString &option)
{
    return aqtruncat::providers::packageOption(arguments, option);
}

// --config-dir, else the Claude Code directory the package was enabled for,
// else the default one.
QString effectiveConfigDirectory(const CommandLine &line)
{
    if (!line.configDirectory.isEmpty())
        return line.configDirectory;
    const QJsonObject metric = readMetric(metricFile());
    if (managedPackage(metric)) {
        const QString recorded = packageOption(metric.value(QStringLiteral("source")).toObject()
                                                   .value(QStringLiteral("arguments")).toArray(),
                                               QStringLiteral("--config-dir"));
        if (!recorded.isEmpty())
            return recorded;
    }
    return aqtruncat::claude::defaultConfigDirectory();
}

// Brings an enabled integration written by an earlier version (or for an
// adapter at another path) up to date: the package is rewritten with its own
// options, and live updates, if on, are pointed at this executable. Nothing
// is created when the integration is off, and a user-managed package is never
// touched. install.sh runs this as the real user after an installation.
int upgrade()
{
    const QJsonObject metric = readMetric(metricFile());
    if (!QFileInfo::exists(metricFile()) || !managedPackage(metric)) {
        printOut(QStringLiteral("Claude Code integration: nothing to upgrade.\n"));
        return 0;
    }
    const QJsonArray arguments = metric.value(QStringLiteral("source")).toObject()
                                     .value(QStringLiteral("arguments")).toArray();
    CommandLine line;
    line.context = arguments.contains(QStringLiteral("--context"));
    line.cwdFilter = packageOption(arguments, QStringLiteral("--cwd"));
    line.configDirectory = packageOption(arguments, QStringLiteral("--config-dir"));
    if (enable(line, false) != 0)
        return 1;
    const QString configDirectory = effectiveConfigDirectory(line);
    if (aqtruncat::claude::bridgeStatus(bridgePaths(configDirectory)).state == aqtruncat::claude::BridgeState::On
        && liveUpdates(configDirectory, true) != 0)
        return 1;
    printOut(QStringLiteral("Claude Code integration upgraded: %1\n").arg(packageDirectory()));
    return 0;
}

int status(const CommandLine &line)
{
    const QString file = metricFile();
    const QJsonObject metric = readMetric(file);
    const bool exists = QFileInfo::exists(file);
    const bool managed = exists && managedPackage(metric);
    const QJsonArray arguments = metric.value(QStringLiteral("source")).toObject()
                                     .value(QStringLiteral("arguments")).toArray();
    const QString configDirectory = effectiveConfigDirectory(line);
    const auto paths = bridgePaths(configDirectory);
    const auto bridge = aqtruncat::claude::bridgeStatus(paths);
    const auto live = aqtruncat::claude::readBridgeState(aqtruncat::claude::bridgeStateFile(), configDirectory);
    QDateTime lastLive;
    for (const auto *reading : {&live.fiveHour, &live.sevenDay}) {
        if (*reading && (!lastLive.isValid() || (*reading)->observedAt > lastLive))
            lastLive = (*reading)->observedAt;
    }
    const bool claudeFound = QFileInfo(configDirectory).isDir()
        || QFileInfo::exists(aqtruncat::claude::globalConfigFile(
               configDirectory, configDirectory != aqtruncat::claude::defaultConfigDirectory()
                                    || qEnvironmentVariableIsSet("CLAUDE_CONFIG_DIR")));
    QString liveState;
    switch (bridge.state) {
    case aqtruncat::claude::BridgeState::Off: liveState = QStringLiteral("off"); break;
    case aqtruncat::claude::BridgeState::On: liveState = QStringLiteral("on"); break;
    case aqtruncat::claude::BridgeState::Replaced: liveState = QStringLiteral("replaced"); break;
    }

    if (line.json) {
        const QJsonObject object{
            {QStringLiteral("integration"), QLatin1String(MetricId)},
            {QStringLiteral("version"), QStringLiteral(AQTRUNCAT_VERSION)},
            {QStringLiteral("enabled"), managed},
            {QStringLiteral("userPackage"), exists && !managed},
            // Packages written before the provenance field existed lack it;
            // Settings rewrites them with "enable".
            {QStringLiteral("integrationMarked"),
             managed && metric.value(QStringLiteral("integration")) == QLatin1String(MetricId)},
            {QStringLiteral("metricFile"), file},
            {QStringLiteral("context"), managed && arguments.contains(QStringLiteral("--context"))},
            {QStringLiteral("cwd"), managed ? packageOption(arguments, QStringLiteral("--cwd")) : QString()},
            {QStringLiteral("claudeFound"), claudeFound},
            {QStringLiteral("configDirectory"), configDirectory},
            {QStringLiteral("settingsFile"), paths.settingsFile},
            {QStringLiteral("liveUpdates"), liveState},
            {QStringLiteral("otherStatusLine"), bridge.otherStatusLine},
            {QStringLiteral("chainedStatusLine"), bridge.chained},
            {QStringLiteral("settingsError"), bridge.error},
            {QStringLiteral("lastLiveReport"), lastLive.isValid() ? QJsonValue(lastLive.toString(Qt::ISODate)) : QJsonValue()},
        };
        printOut(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) + u'\n');
        return 0;
    }
    QStringList lines;
    if (managed) {
        QString options = arguments.contains(QStringLiteral("--context")) ? QStringLiteral("context window: on")
                                                                          : QStringLiteral("context window: off");
        const QString cwd = packageOption(arguments, QStringLiteral("--cwd"));
        if (!cwd.isEmpty())
            options += QStringLiteral(", only sessions in %1").arg(cwd);
        lines << QStringLiteral("Integration:   enabled (%1)").arg(options);
    } else if (exists) {
        lines << QStringLiteral("Integration:   %1 is a user-managed package (not written by %2)")
                     .arg(file, QLatin1String(ExecutableName));
    } else {
        lines << QStringLiteral("Integration:   disabled");
    }
    lines << QStringLiteral("Claude Code:   %1 (%2)").arg(claudeFound ? QStringLiteral("found") : QStringLiteral("not found"),
                                                        configDirectory);
    QString liveText = liveState;
    if (bridge.state == aqtruncat::claude::BridgeState::On && bridge.chained)
        liveText += QStringLiteral(" (your previous status line runs after it)");
    else if (bridge.state == aqtruncat::claude::BridgeState::Replaced)
        liveText = QStringLiteral("off (%1 was changed since live updates were turned on)").arg(paths.settingsFile);
    else if (bridge.state == aqtruncat::claude::BridgeState::Off && bridge.otherStatusLine)
        liveText += QStringLiteral(" (another status line is configured; it would keep running)");
    if (!bridge.error.isEmpty())
        liveText += QStringLiteral(" — %1").arg(bridge.error);
    lines << QStringLiteral("Live updates:  %1").arg(liveText);
    lines << QStringLiteral("Last live report: %1").arg(lastLive.isValid() ? lastLive.toLocalTime().toString(Qt::ISODate)
                                                                             : QStringLiteral("none"));
    printOut(lines.join(u'\n') + u'\n');
    return 0;
}

// Run by Claude Code as its status line. It must never break or slow down
// the user's status line: failures are silent, and a previous status-line
// command (kept in the live-updates backup) replaces this process and gets
// the very same input.
int statusLine()
{
    QByteArray input;
    char buffer[65536];
    for (;;) {
        const size_t count = std::fread(buffer, 1, sizeof buffer, stdin);
        if (count == 0)
            break;
        if (input.size() < 16 * aqtruncat::claude::MaximumStatusLineInputBytes)
            input.append(buffer, qsizetype(count));
    }
    const QString configDirectory = aqtruncat::claude::defaultConfigDirectory();
    if (input.size() <= aqtruncat::claude::MaximumStatusLineInputBytes)
        aqtruncat::claude::captureStatusLine(input, aqtruncat::claude::bridgeStateFile(), configDirectory,
                                             QDateTime::currentDateTimeUtc());

    const std::optional<QString> previous = aqtruncat::claude::chainedCommand(bridgePaths(configDirectory).backupFile);
    if (!previous)
        return 0;
    const int memory = ::memfd_create("aqtruncat-statusline", MFD_CLOEXEC);
    if (memory < 0)
        return 0;
    qsizetype written = 0;
    while (written < input.size()) {
        const ssize_t n = ::write(memory, input.constData() + written, size_t(input.size() - written));
        if (n <= 0)
            return 0;
        written += n;
    }
    if (::lseek(memory, 0, SEEK_SET) != 0 || ::dup2(memory, STDIN_FILENO) < 0)
        return 0;
    ::close(memory);
    std::fflush(stdout);
    const QByteArray command = previous->toLocal8Bit();
    ::execl("/bin/sh", "sh", "-c", command.constData(), static_cast<char *>(nullptr));
    return 0; // exec failed: an empty status line rather than an error
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.isEmpty()) {
        usage(stderr);
        return UsageError;
    }
    CommandLine line;
    line.command = args.first();
    if (line.command == QLatin1String("-h") || line.command == QLatin1String("--help")) {
        usage(stdout);
        return 0;
    }
    if (line.command == QLatin1String("-v") || line.command == QLatin1String("--version")) {
        std::printf("%s %s\n", ExecutableName, AQTRUNCAT_VERSION);
        return 0;
    }
    if (line.command == QLatin1String("statusline"))
        return statusLine(); // whatever Claude Code passes, never a usage error

    auto invalid = [&](const QString &option) {
        printErr(QStringLiteral("%1: invalid option \"%2\" for %3; see --help")
                     .arg(QLatin1String(ExecutableName), option, line.command));
        return UsageError;
    };
    const QString &command = line.command;
    const bool snapshotOrEnable = command == QLatin1String("snapshot") || command == QLatin1String("enable");
    int first = 1;
    if (command == QLatin1String("live-updates")) {
        if (args.size() < 2 || (args.at(1) != QLatin1String("on") && args.at(1) != QLatin1String("off"))) {
            printErr(QStringLiteral("%1: live-updates needs \"on\" or \"off\"; see --help").arg(QLatin1String(ExecutableName)));
            return UsageError;
        }
        line.subcommand = args.at(1);
        first = 2;
    }
    for (int i = first; i < args.size(); ++i) {
        const QString &option = args.at(i);
        const bool takesDirectory = option == QLatin1String("--config-dir")
            || (option == QLatin1String("--cwd") && snapshotOrEnable);
        if (takesDirectory && i + 1 < args.size()) {
            const QString value = QDir::cleanPath(QFileInfo(args.at(++i)).absoluteFilePath());
            (option == QLatin1String("--cwd") ? line.cwdFilter : line.configDirectory) = value;
        } else if (option == QLatin1String("--context") && snapshotOrEnable) {
            line.context = true;
        } else if (option == QLatin1String("--force") && command == QLatin1String("enable")) {
            line.force = true;
        } else if (option == QLatin1String("--json") && command == QLatin1String("status")) {
            line.json = true;
        } else {
            return invalid(option);
        }
    }
    if (!line.cwdFilter.isEmpty() && !line.context) {
        printErr(QStringLiteral("%1: --cwd only applies together with --context").arg(QLatin1String(ExecutableName)));
        return UsageError;
    }

    if (command == QLatin1String("snapshot")) {
        aqtruncat::claude::Options options;
        options.configDirectory = configDirectoryOf(line);
        options.globalConfigFile = aqtruncat::claude::globalConfigFile(
            options.configDirectory, !line.configDirectory.isEmpty() || qEnvironmentVariableIsSet("CLAUDE_CONFIG_DIR"));
        options.bridgeStateFile = aqtruncat::claude::bridgeStateFile();
        options.liveUpdatesOn = aqtruncat::claude::bridgeStatus(bridgePaths(options.configDirectory)).state
            == aqtruncat::claude::BridgeState::On;
        options.includeContext = line.context;
        options.cwdFilter = line.cwdFilter;
        const aqtruncat::claude::Outcome outcome = aqtruncat::claude::collect(options);
        std::fwrite(outcome.standardOutput.constData(), 1, size_t(outcome.standardOutput.size()), stdout);
        if (!outcome.diagnostic.isEmpty())
            printErr(outcome.diagnostic);
        return outcome.exitCode;
    }
    if (command == QLatin1String("enable"))
        return enable(line);
    if (command == QLatin1String("upgrade"))
        return upgrade();
    if (command == QLatin1String("status"))
        return status(line);
    if (command == QLatin1String("live-updates"))
        return liveUpdates(effectiveConfigDirectory(line), line.subcommand == QLatin1String("on"));
    if (command == QLatin1String("disable"))
        return disable(effectiveConfigDirectory(line));
    printErr(QStringLiteral("%1: unknown command \"%2\"; see --help").arg(QLatin1String(ExecutableName), command));
    return UsageError;
}
