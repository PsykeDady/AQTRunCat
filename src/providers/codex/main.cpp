// aqtruncat-codex-metric: F3 Codex adapter for F1 command-backed Custom
// Metrics. "snapshot" is what AQTRunCat runs; "enable"/"disable"/"upgrade"/
// "status"/"live-updates" manage the integration (the AQTRunCat Settings
// window runs exactly these commands, so CLI and GUI share one
// configuration: the metric package). Codex configuration is never touched.
// See docs/integration-codex.md.

#include "codexicon.h"
#include "codexusage.h"
#include "common/package.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

#include <cstdio>

namespace {

constexpr int UsageError = 2;
const char ExecutableName[] = "aqtruncat-codex-metric";
const char MetricId[] = "codex";
const char IconFile[] = "codex.svg";
// Local data is cheap to read; live data is fetched at most every
// LiveRefreshSeconds whatever the interval. The timeout covers a live read.
constexpr int IntervalSeconds = 15;
constexpr int TimeoutSeconds = 30;

void printOut(const QString &text) { std::fputs(qPrintable(text), stdout); }
void printErr(const QString &text) { std::fputs(qPrintable(text + u'\n'), stderr); }

void usage(FILE *stream)
{
    const QString text = QStringLiteral(
        "Usage:\n"
        "  %1 snapshot [--live] [--codex-home DIR]\n"
        "  %1 enable [--live] [--codex-home DIR] [--force]\n"
        "  %1 disable\n"
        "  %1 upgrade\n"
        "  %1 status [--json]\n"
        "  %1 live-updates on|off\n"
        "  %1 -h | --help | -v | --version\n"
        "\n"
        "AQTRunCat Codex integration: your Codex usage limits (the windows Codex's\n"
        "/status shows, e.g. the 5h and weekly limits) with their reset times.\n"
        "Normally managed from AQTRunCat Settings > Integrations.\n"
        "\n"
        "  snapshot      print the metric as an AQTRunCat snapshot JSON (what AQTRunCat\n"
        "                runs every %2 s). Exit 0 with a snapshot, 69 when Codex or any\n"
        "                usage report is absent, 1 when its data cannot be read.\n"
        "  enable        install the metric package in\n"
        "                $XDG_DATA_HOME/AQTRunCat/metrics/codex/ (AQTRunCat shows it\n"
        "                under Integrations at once). Not added to the Metrics Bar.\n"
        "  disable       remove that package.\n"
        "  upgrade       rewrite an enabled integration for this version, keeping its\n"
        "                options (run by install.sh); does nothing when it is off.\n"
        "  status        show whether the integration and live updates are on.\n"
        "  live-updates  on: every %3 minutes AQTRunCat asks Codex (codex app-server,\n"
        "                account/rateLimits/read) for the current limits; Codex contacts\n"
        "                OpenAI with its own login, no tokens are used and nothing is\n"
        "                changed in Codex. off (default): only the limits Codex recorded\n"
        "                in its local session files while you used it.\n"
        "\n"
        "  --live            the same as live-updates on, for snapshot/enable\n"
        "  --codex-home DIR  Codex data directory (default $CODEX_HOME or ~/.codex)\n"
        "  --force           let enable replace a codex package it did not write\n"
        "  --json            machine-readable status (used by AQTRunCat Settings)\n")
        .arg(QLatin1String(ExecutableName)).arg(IntervalSeconds).arg(aqtruncat::codex::LiveRefreshSeconds / 60);
    std::fputs(qPrintable(text), stream);
}

QString packageDirectory() { return aqtruncat::providers::packageDirectory(QLatin1String(MetricId)); }
QString metricFile() { return packageDirectory() + QStringLiteral("/metric.json"); }

bool managedPackage(const QJsonObject &metric)
{
    return aqtruncat::providers::isManagedPackage(metric, QLatin1String(MetricId), QLatin1String(ExecutableName));
}

QString findCodex()
{
    // PATH first; then the usual per-user location, which a desktop
    // session's PATH may lack.
    QString found = QStandardPaths::findExecutable(QStringLiteral("codex"));
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(QStringLiteral("codex"), {QDir::home().filePath(QStringLiteral(".local/bin"))});
    return found;
}

struct CommandLine {
    QString command;
    QString subcommand;
    QString codexHome; // as given; empty: default
    bool live = false;
    bool force = false;
    bool json = false;
};

bool write(const QString &path, const QByteArray &contents)
{
    QString error;
    if (!aqtruncat::providers::writeFileAtomically(path, contents, &error)) {
        printErr(QStringLiteral("%1: %2").arg(QLatin1String(ExecutableName), error));
        return false;
    }
    return true;
}

int enable(const CommandLine &line, bool announce = true)
{
    const QString file = metricFile();
    if (QFileInfo::exists(file) && !managedPackage(aqtruncat::providers::readJsonObject(file)) && !line.force) {
        printErr(QStringLiteral("%1: %2 exists and was not written by %1; nothing changed (use --force to replace it)")
                     .arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    QJsonArray arguments{QStringLiteral("snapshot")};
    if (line.live)
        arguments.append(QStringLiteral("--live"));
    if (!line.codexHome.isEmpty()) {
        arguments.append(QStringLiteral("--codex-home"));
        arguments.append(line.codexHome);
    }
    QJsonObject metric{
        {QStringLiteral("id"), QLatin1String(MetricId)},
        // Provenance for AQTRunCat: shown under Integrations, not Custom Metrics.
        {QStringLiteral("integration"), QLatin1String(MetricId)},
        {QStringLiteral("title"), QStringLiteral("Codex")},
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
        QJsonObject{{QStringLiteral("executable"), QStringLiteral("codex")},
                    {QStringLiteral("feature"), QStringLiteral("Codex usage integration")}}
    });
    // Icon first: the package becomes visible to AQTRunCat with metric.json.
    if (!write(packageDirectory() + u'/' + QLatin1String(IconFile), QByteArray(CodexMetricIconSvg))
        || !write(file, QJsonDocument(metric).toJson(QJsonDocument::Indented)))
        return 1;
    if (announce) {
        printOut(QStringLiteral("Codex integration enabled: %1\n"
                                "AQTRunCat shows it under Integrations; add it to the Metrics Bar from Settings if wanted.\n")
                     .arg(packageDirectory()));
    }
    return 0;
}

// The options recorded in an enabled package.
CommandLine packageOptions(const QJsonObject &metric)
{
    const QJsonArray arguments = aqtruncat::providers::packageArguments(metric);
    CommandLine line;
    line.live = arguments.contains(QStringLiteral("--live"));
    line.codexHome = aqtruncat::providers::packageOption(arguments, QStringLiteral("--codex-home"));
    return line;
}

int disable()
{
    const QString file = metricFile();
    if (!QFileInfo::exists(file)) {
        printOut(QStringLiteral("Codex integration is not enabled (%1 does not exist).\n").arg(file));
        return 0;
    }
    if (!managedPackage(aqtruncat::providers::readJsonObject(file))) {
        printErr(QStringLiteral("%1: %2 was not written by %1; nothing removed").arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    if (!QFile::remove(file)) {
        printErr(QStringLiteral("%1: cannot remove %2").arg(QLatin1String(ExecutableName), file));
        return 1;
    }
    QFile::remove(packageDirectory() + u'/' + QLatin1String(IconFile));
    QDir().rmdir(packageDirectory()); // only when nothing else is left in it
    printOut(QStringLiteral("Codex integration disabled.\n"));
    return 0;
}

int upgrade()
{
    const QJsonObject metric = aqtruncat::providers::readJsonObject(metricFile());
    if (!QFileInfo::exists(metricFile()) || !managedPackage(metric)) {
        printOut(QStringLiteral("Codex integration: nothing to upgrade.\n"));
        return 0;
    }
    if (enable(packageOptions(metric), false) != 0)
        return 1;
    printOut(QStringLiteral("Codex integration upgraded: %1\n").arg(packageDirectory()));
    return 0;
}

int liveUpdates(bool on)
{
    const QJsonObject metric = aqtruncat::providers::readJsonObject(metricFile());
    if (!QFileInfo::exists(metricFile()) || !managedPackage(metric)) {
        printErr(QStringLiteral("%1: the Codex integration is not enabled; enable it first").arg(QLatin1String(ExecutableName)));
        return 1;
    }
    CommandLine line = packageOptions(metric);
    line.live = on;
    if (enable(line, false) != 0)
        return 1;
    printOut(on ? QStringLiteral("Live updates are on: AQTRunCat asks Codex for the current limits every %1 minutes.\n")
                      .arg(aqtruncat::codex::LiveRefreshSeconds / 60)
                : QStringLiteral("Live updates are off: only the limits recorded in Codex's session files are shown.\n"));
    return 0;
}

QString effectiveCodexHome(const CommandLine &line)
{
    if (!line.codexHome.isEmpty())
        return line.codexHome;
    const QJsonObject metric = aqtruncat::providers::readJsonObject(metricFile());
    if (managedPackage(metric)) {
        const QString recorded = packageOptions(metric).codexHome;
        if (!recorded.isEmpty())
            return recorded;
    }
    return aqtruncat::codex::defaultCodexHome();
}

int status(const CommandLine &line)
{
    const QString file = metricFile();
    const QJsonObject metric = aqtruncat::providers::readJsonObject(file);
    const bool exists = QFileInfo::exists(file);
    const bool managed = exists && managedPackage(metric);
    const bool live = managed && packageOptions(metric).live;
    const QString codexHome = effectiveCodexHome(line);
    const QString codex = findCodex();
    const aqtruncat::codex::LiveState state = aqtruncat::codex::readLiveState(aqtruncat::codex::liveStateFile(), codexHome);

    if (line.json) {
        const QJsonObject object{
            {QStringLiteral("integration"), QLatin1String(MetricId)},
            {QStringLiteral("version"), QStringLiteral(AQTRUNCAT_VERSION)},
            {QStringLiteral("enabled"), managed},
            {QStringLiteral("userPackage"), exists && !managed},
            {QStringLiteral("integrationMarked"), managed && metric.value(QStringLiteral("integration")) == QLatin1String(MetricId)},
            {QStringLiteral("metricFile"), file},
            {QStringLiteral("codexFound"), QFileInfo(codexHome).isDir() || !codex.isEmpty()},
            {QStringLiteral("codexHome"), codexHome},
            {QStringLiteral("codexExecutable"), codex},
            {QStringLiteral("liveUpdates"), live ? QStringLiteral("on") : QStringLiteral("off")},
            {QStringLiteral("lastLiveReport"), state.capturedAt.isValid() ? QJsonValue(state.capturedAt.toString(Qt::ISODate)) : QJsonValue()},
            {QStringLiteral("liveError"), state.error},
        };
        printOut(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) + u'\n');
        return 0;
    }
    QStringList lines;
    if (managed)
        lines << QStringLiteral("Integration:   enabled");
    else if (exists)
        lines << QStringLiteral("Integration:   %1 is a user-managed package (not written by %2)").arg(file, QLatin1String(ExecutableName));
    else
        lines << QStringLiteral("Integration:   disabled");
    lines << QStringLiteral("Codex:         %1 (data %2%3)")
                 .arg(QFileInfo(codexHome).isDir() || !codex.isEmpty() ? QStringLiteral("found") : QStringLiteral("not found"),
                      codexHome, codex.isEmpty() ? QStringLiteral(", no codex executable") : QStringLiteral(", ") + codex);
    lines << QStringLiteral("Live updates:  %1").arg(live ? QStringLiteral("on") : QStringLiteral("off"));
    lines << QStringLiteral("Last live report: %1%2")
                 .arg(state.capturedAt.isValid() ? state.capturedAt.toLocalTime().toString(Qt::ISODate) : QStringLiteral("none"),
                      state.error.isEmpty() ? QString() : QStringLiteral(" (last attempt failed: %1)").arg(state.error));
    printOut(lines.join(u'\n') + u'\n');
    return 0;
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
    const QString &command = line.command;
    if (command == QLatin1String("-h") || command == QLatin1String("--help")) {
        usage(stdout);
        return 0;
    }
    if (command == QLatin1String("-v") || command == QLatin1String("--version")) {
        std::printf("%s %s\n", ExecutableName, AQTRUNCAT_VERSION);
        return 0;
    }
    int first = 1;
    if (command == QLatin1String("live-updates")) {
        if (args.size() < 2 || (args.at(1) != QLatin1String("on") && args.at(1) != QLatin1String("off"))) {
            printErr(QStringLiteral("%1: live-updates needs \"on\" or \"off\"; see --help").arg(QLatin1String(ExecutableName)));
            return UsageError;
        }
        line.subcommand = args.at(1);
        first = 2;
    }
    const bool snapshotOrEnable = command == QLatin1String("snapshot") || command == QLatin1String("enable");
    for (int i = first; i < args.size(); ++i) {
        const QString &option = args.at(i);
        if (option == QLatin1String("--codex-home") && (snapshotOrEnable || command == QLatin1String("status"))
            && i + 1 < args.size()) {
            line.codexHome = QDir::cleanPath(QFileInfo(args.at(++i)).absoluteFilePath());
        } else if (option == QLatin1String("--live") && snapshotOrEnable) {
            line.live = true;
        } else if (option == QLatin1String("--force") && command == QLatin1String("enable")) {
            line.force = true;
        } else if (option == QLatin1String("--json") && command == QLatin1String("status")) {
            line.json = true;
        } else {
            printErr(QStringLiteral("%1: invalid option \"%2\" for %3; see --help")
                         .arg(QLatin1String(ExecutableName), option, command));
            return UsageError;
        }
    }

    if (command == QLatin1String("snapshot")) {
        aqtruncat::codex::Options options;
        options.codexHomeOverridden = !line.codexHome.isEmpty() || qEnvironmentVariableIsSet("CODEX_HOME");
        options.codexHome = line.codexHome.isEmpty() ? aqtruncat::codex::defaultCodexHome() : line.codexHome;
        options.codexExecutable = findCodex();
        options.live = line.live;
        options.liveStateFile = aqtruncat::codex::liveStateFile();
        const aqtruncat::codex::Outcome outcome = aqtruncat::codex::collect(options);
        std::fwrite(outcome.standardOutput.constData(), 1, size_t(outcome.standardOutput.size()), stdout);
        if (!outcome.diagnostic.isEmpty())
            printErr(outcome.diagnostic);
        return outcome.exitCode;
    }
    if (command == QLatin1String("enable"))
        return enable(line);
    if (command == QLatin1String("disable"))
        return disable();
    if (command == QLatin1String("upgrade"))
        return upgrade();
    if (command == QLatin1String("status"))
        return status(line);
    if (command == QLatin1String("live-updates"))
        return liveUpdates(line.subcommand == QLatin1String("on"));
    printErr(QStringLiteral("%1: unknown command \"%2\"; see --help").arg(QLatin1String(ExecutableName), command));
    return UsageError;
}
