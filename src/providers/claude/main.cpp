// aqtruncat-claude-metric: F2 Claude Code adapter for F1 command-backed
// Custom Metrics. "snapshot" is what AQTRunCat runs; "enable"/"disable"
// manage the user's metric package. See docs/F2-claude-code-metric.md.

#include "claudecode.h"
#include "claudeicon.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include <cstdio>

namespace {

constexpr int UsageError = 2;
const char ExecutableName[] = "aqtruncat-claude-metric";
const char MetricId[] = "claude-code";
const char IconFile[] = "claude-code.svg";
// Context changes once per Claude response; 10 s keeps the card current
// without polling. The adapter reads a few small files and one file tail.
constexpr int IntervalSeconds = 10;
constexpr int TimeoutSeconds = 5;

void printOut(const QString &text) { std::fputs(qPrintable(text), stdout); }
void printErr(const QString &text) { std::fputs(qPrintable(text + u'\n'), stderr); }

void usage(FILE *stream)
{
    const QString text = QStringLiteral(
        "Usage:\n"
        "  %1 snapshot [--cwd DIR] [--config-dir DIR]\n"
        "  %1 enable [--cwd DIR] [--force]\n"
        "  %1 disable\n"
        "  %1 -h | --help | --version\n"
        "\n"
        "AQTRunCat Claude Code metric adapter (read-only).\n"
        "\n"
        "  snapshot  print the Claude Code metric as an AQTRunCat snapshot JSON\n"
        "            (this is what AQTRunCat runs every %2 s). Exit 0 with a\n"
        "            snapshot, 69 when Claude Code or a running session is absent,\n"
        "            1 when its local data cannot be read or is not understood.\n"
        "  enable    install the metric package in\n"
        "            $XDG_DATA_HOME/AQTRunCat/metrics/claude-code/ so AQTRunCat\n"
        "            shows a \"Claude Code\" card. It is not added to the Metrics\n"
        "            Bar; choose it in Settings if you want it there.\n"
        "  disable   remove that package again.\n"
        "\n"
        "  --cwd DIR         only consider sessions started in DIR or below\n"
        "  --config-dir DIR  Claude Code data directory (default $CLAUDE_CONFIG_DIR\n"
        "                    or ~/.claude)\n"
        "  --force           let enable replace a claude-code package it did not write\n")
        .arg(QLatin1String(ExecutableName)).arg(IntervalSeconds);
    std::fputs(qPrintable(text), stream);
}

QString packageDirectory()
{
    // Same location AQTRunCat discovers packages in (custommetricpaths.cpp).
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("AQTRunCat/metrics/%1").arg(QLatin1String(MetricId)));
}

// A package is ours when its metric.json has our id and runs an executable
// named like this one; only such a package is replaced or removed.
bool managedPackage(const QString &metricFile)
{
    QFile file(metricFile);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject object = QJsonDocument::fromJson(file.read(1024 * 1024)).object();
    const QString executable = object.value(QStringLiteral("source")).toObject()
                                   .value(QStringLiteral("executable")).toString();
    return object.value(QStringLiteral("id")) == QLatin1String(MetricId)
        && QFileInfo(executable).fileName() == QLatin1String(ExecutableName);
}

bool writeAtomically(const QString &path, const QByteArray &contents)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        printErr(QStringLiteral("%1: cannot write %2: %3").arg(QLatin1String(ExecutableName), path, file.errorString()));
        return false;
    }
    return true;
}

int enable(const QString &cwdFilter, bool force)
{
    const QString directory = packageDirectory();
    const QString metricFile = directory + QStringLiteral("/metric.json");
    if (QFileInfo::exists(metricFile) && !managedPackage(metricFile) && !force) {
        printErr(QStringLiteral("%1: %2 exists and was not written by %1; nothing changed (use --force to replace it)")
                     .arg(QLatin1String(ExecutableName), metricFile));
        return 1;
    }
    if (!QDir().mkpath(directory)) {
        printErr(QStringLiteral("%1: cannot create %2").arg(QLatin1String(ExecutableName), directory));
        return 1;
    }
    QJsonArray arguments{QStringLiteral("snapshot")};
    if (!cwdFilter.isEmpty()) {
        arguments.append(QStringLiteral("--cwd"));
        arguments.append(cwdFilter);
    }
    const QJsonObject metric{
        {QStringLiteral("id"), QLatin1String(MetricId)},
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
    // Icon first: the package becomes visible to AQTRunCat with metric.json.
    if (!writeAtomically(directory + u'/' + QLatin1String(IconFile), QByteArray(ClaudeMetricIconSvg))
        || !writeAtomically(metricFile, QJsonDocument(metric).toJson(QJsonDocument::Indented)))
        return 1;
    printOut(QStringLiteral("Claude Code metric enabled: %1\n"
                            "AQTRunCat shows it in the Dashboard; add it to the Metrics Bar from Settings if wanted.\n")
                 .arg(directory));
    return 0;
}

int disable()
{
    const QString directory = packageDirectory();
    const QString metricFile = directory + QStringLiteral("/metric.json");
    if (!QFileInfo::exists(metricFile)) {
        printOut(QStringLiteral("Claude Code metric is not enabled (%1 does not exist).\n").arg(metricFile));
        return 0;
    }
    if (!managedPackage(metricFile)) {
        printErr(QStringLiteral("%1: %2 was not written by %1; nothing removed")
                     .arg(QLatin1String(ExecutableName), metricFile));
        return 1;
    }
    if (!QFile::remove(metricFile)) {
        printErr(QStringLiteral("%1: cannot remove %2").arg(QLatin1String(ExecutableName), metricFile));
        return 1;
    }
    QFile::remove(directory + u'/' + QLatin1String(IconFile));
    QDir().rmdir(directory); // only when nothing else is left in it
    printOut(QStringLiteral("Claude Code metric disabled.\n"));
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
    const QString command = args.first();
    if (command == QLatin1String("-h") || command == QLatin1String("--help")) {
        usage(stdout);
        return 0;
    }
    if (command == QLatin1String("--version")) {
        std::printf("%s %s\n", ExecutableName, AQTRUNCAT_VERSION);
        return 0;
    }

    QString cwdFilter;
    QString configDirectory;
    bool force = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString &option = args.at(i);
        if ((option == QLatin1String("--cwd") || option == QLatin1String("--config-dir")) && i + 1 < args.size()) {
            const QString value = QDir::cleanPath(QFileInfo(args.at(++i)).absoluteFilePath());
            (option == QLatin1String("--cwd") ? cwdFilter : configDirectory) = value;
        } else if (option == QLatin1String("--force") && command == QLatin1String("enable")) {
            force = true;
        } else {
            printErr(QStringLiteral("%1: invalid option \"%2\" for %3; see --help")
                         .arg(QLatin1String(ExecutableName), option, command));
            return UsageError;
        }
    }

    if (command == QLatin1String("snapshot")) {
        aqtruncat::claude::Options options;
        options.configDirectory = configDirectory.isEmpty() ? aqtruncat::claude::defaultConfigDirectory()
                                                            : configDirectory;
        options.cwdFilter = cwdFilter;
        const aqtruncat::claude::Outcome outcome = aqtruncat::claude::collect(options);
        std::fwrite(outcome.standardOutput.constData(), 1, size_t(outcome.standardOutput.size()), stdout);
        if (!outcome.diagnostic.isEmpty())
            printErr(outcome.diagnostic);
        return outcome.exitCode;
    }
    if (!configDirectory.isEmpty()) {
        printErr(QStringLiteral("%1: --config-dir only applies to snapshot").arg(QLatin1String(ExecutableName)));
        return UsageError;
    }
    if (command == QLatin1String("enable"))
        return enable(cwdFilter, force);
    if (command == QLatin1String("disable") && cwdFilter.isEmpty())
        return disable();
    printErr(QStringLiteral("%1: unknown command \"%2\"; see --help").arg(QLatin1String(ExecutableName), command));
    return UsageError;
}
