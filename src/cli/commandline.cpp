#include "commandline.h"

#include <QList>

#include <algorithm>

namespace aqtruncat::cli {

namespace {

const QString RunHelp = QStringLiteral(
    "Usage: aqtruncat run [--runner ID] [--fps N] [--verbose]\n"
    "\n"
    "Start AQTRunCat in the system tray. The runner speeds up with the CPU load\n"
    "(10 FPS idle, 40 FPS at full load). Plain \"aqtruncat\" does the same.\n"
    "\n"
    "Options:\n"
    "  --runner ID   show this runner instead of the default (see \"aqtruncat runner list\")\n"
    "  --fps N       diagnostics: fixed frame rate 1-120, CPU load ignored\n"
    "  --verbose     log CPU samples and frame rate decisions\n"
    "  -h, --help    show this help\n");

const QString SettingsHelp = QStringLiteral(
    "Usage: aqtruncat settings\n"
    "\n"
    "Open AQTRunCat Settings in its own window and process. The backend is\n"
    "activated on demand and remains running after Settings closes.\n"
    "\n"
    "Options:\n"
    "  -h, --help    show this help\n");

const QString RunnerHelp = QStringLiteral(
    "Usage: aqtruncat runner <command>\n"
    "\n"
    "Manage runners. Bundled runners ship with AQTRunCat; user runners live in\n"
    "$XDG_DATA_HOME/AQTRunCat/runners and replace bundled runners with the same id.\n"
    "\n"
    "Commands:\n"
    "  list                       show every runner, including invalid ones and why\n"
    "  import FILE.zip [--id ID]  install a runner archive (Runner Gallery ZIP)\n"
    "  remove ID                  uninstall a user runner\n"
    "\n"
    "Run \"aqtruncat runner <command> --help\" for details.\n");

const QString ListHelp = QStringLiteral(
    "Usage: aqtruncat runner list\n"
    "\n"
    "List bundled and user runners: id, name, source, frames, frame size and\n"
    "rendering mode. The default runner is marked with '*'. Invalid runners are\n"
    "listed with the reason; warnings are shown below each runner.\n"
    "\"aqtruncat --list-runners\" is an alias.\n");

const QString ImportHelp = QStringLiteral(
    "Usage: aqtruncat runner import FILE.zip [--id ID] [--force]\n"
    "\n"
    "Install a runner from a ZIP archive, such as a Runner Gallery\n"
    "\"<name>-frames.zip\". The archive is extracted safely into a temporary\n"
    "directory, validated, then installed as a user runner. Nothing is installed\n"
    "if any step fails. The id comes from the archive (\"beagle-frames.zip\" or a\n"
    "single \"beagle-frames/\" folder gives \"beagle\").\n"
    "\n"
    "Options:\n"
    "  --id ID     install under this id instead\n"
    "  --force     replace a user runner with the same id (bundled runners are\n"
    "              never modified; a user runner always takes precedence)\n"
    "  -h, --help  show this help\n");

const QString RemoveHelp = QStringLiteral(
    "Usage: aqtruncat runner remove ID\n"
    "\n"
    "Uninstall the user runner ID. Bundled runners cannot be removed. If ID was\n"
    "a user runner overriding a bundled one, the bundled runner is used again.\n");

const QString MetricsHelp = QStringLiteral(
    "Usage: aqtruncat metrics [--watch]\n"
    "\n"
    "Show CPU usage and temperature, memory, swap, network, storage and disk\n"
    "activity, read directly from /proc, /sys and statvfs. Rates need two samples, so the report comes\n"
    "after about one second. Network counts physical interfaces only (no\n"
    "loopback, VPN, bridges or containers); disk activity counts whole physical\n"
    "disks only (no partitions, device-mapper, loop or zram).\n"
    "\n"
    "Options:\n"
    "  --watch     print one line per second until Ctrl+C\n"
    "  -h, --help  show this help\n");

const QString AutostartHelp = QStringLiteral(
    "Usage: aqtruncat autostart enable|disable|status\n"
    "\n"
    "Start the AQTRunCat Tray Plugin automatically when you log in. This is the same per-user\n"
    "preference as Settings > Desktop: it is saved in AQTRunCat.ini and applied\n"
    "as an XDG autostart entry, $XDG_CONFIG_HOME/autostart/aqtruncat.desktop,\n"
    "pointing at this executable. install.sh uses this command.\n"
    "\n"
    "Commands:\n"
    "  enable    create the autostart entry and save the preference\n"
    "  disable   remove the autostart entry and save the preference\n"
    "  status    show whether the autostart entry exists\n");

struct OptionSpec {
    QString name;  // without leading dashes
    bool takesValue = false;
};

struct ParsedOptions {
    QList<std::pair<QString, QString>> values; // name → value ("" for flags)
    QStringList positionals;
    bool help = false;
    QString error;

    bool has(const QString &name) const
    {
        for (const auto &[n, v] : values)
            if (n == name)
                return true;
        return false;
    }
    QString value(const QString &name) const
    {
        for (const auto &[n, v] : values)
            if (n == name)
                return v;
        return {};
    }
};

// "--name value", "--name=value", flags, "-h"/"--help", "--" ends options.
ParsedOptions parseOptions(const QStringList &args, const QList<OptionSpec> &specs, const QString &context)
{
    ParsedOptions parsed;
    bool optionsEnded = false;
    for (qsizetype i = 0; i < args.size(); ++i) {
        const QString &arg = args[i];
        if (optionsEnded || !arg.startsWith(u'-') || arg == QLatin1String("-")) {
            parsed.positionals.append(arg);
            continue;
        }
        if (arg == QLatin1String("--")) {
            optionsEnded = true;
            continue;
        }
        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            parsed.help = true;
            continue;
        }
        QString name = arg.startsWith(QLatin1String("--")) ? arg.mid(2) : QString();
        QString inlineValue;
        bool hasInlineValue = false;
        if (const qsizetype eq = name.indexOf(u'='); eq >= 0) {
            inlineValue = name.mid(eq + 1);
            name.truncate(eq);
            hasInlineValue = true;
        }
        const auto spec = std::find_if(specs.cbegin(), specs.cend(), [&name](const OptionSpec &s) { return s.name == name; });
        if (name.isEmpty() || spec == specs.cend()) {
            parsed.error = QStringLiteral("unknown option \"%1\" for \"%2\"").arg(arg, context);
            return parsed;
        }
        if (parsed.has(name)) {
            parsed.error = QStringLiteral("option --%1 given more than once").arg(name);
            return parsed;
        }
        if (!spec->takesValue) {
            if (hasInlineValue) {
                parsed.error = QStringLiteral("option --%1 does not take a value").arg(name);
                return parsed;
            }
            parsed.values.append({name, QString()});
            continue;
        }
        if (!hasInlineValue) {
            if (i + 1 >= args.size()) {
                parsed.error = QStringLiteral("option --%1 needs a value").arg(name);
                return parsed;
            }
            inlineValue = args[++i];
        }
        if (inlineValue.isEmpty()) {
            parsed.error = QStringLiteral("option --%1 needs a non-empty value").arg(name);
            return parsed;
        }
        parsed.values.append({name, inlineValue});
    }
    return parsed;
}

ParseResult usageError(const QString &error, const QString &hint)
{
    ParseResult result;
    result.error = error;
    result.hint = hint;
    return result;
}

ParseResult help(const QString &text)
{
    Invocation invocation;
    invocation.command = Command::Help;
    invocation.helpText = text;
    return ParseResult{invocation, {}, {}};
}

ParseResult parseRun(const QStringList &args, const QString &context)
{
    const QString hint = QStringLiteral("Run \"aqtruncat run --help\".");
    const ParsedOptions options =
        parseOptions(args, {{QStringLiteral("runner"), true}, {QStringLiteral("fps"), true}, {QStringLiteral("verbose"), false}}, context);
    if (!options.error.isEmpty())
        return usageError(options.error, hint);
    if (options.help)
        return help(RunHelp);
    if (!options.positionals.isEmpty())
        return usageError(QStringLiteral("unexpected argument \"%1\"").arg(options.positionals.first()), hint);

    Invocation invocation;
    invocation.command = Command::Run;
    invocation.runnerId = options.value(QStringLiteral("runner"));
    invocation.verbose = options.has(QStringLiteral("verbose"));
    if (options.has(QStringLiteral("fps"))) {
        bool ok = false;
        invocation.fps = options.value(QStringLiteral("fps")).toInt(&ok);
        if (!ok || invocation.fps < 1 || invocation.fps > 120)
            return usageError(QStringLiteral("--fps expects an integer between 1 and 120"), hint);
    }
    return ParseResult{invocation, {}, {}};
}

ParseResult parseRunner(const QStringList &args)
{
    const QString hint = QStringLiteral("Run \"aqtruncat runner --help\".");
    if (args.isEmpty())
        return usageError(QStringLiteral("missing runner command (list, import or remove)"), hint);
    const QString &sub = args.first();
    const QStringList rest = args.mid(1);
    if (sub == QLatin1String("-h") || sub == QLatin1String("--help"))
        return help(RunnerHelp);

    Invocation invocation;
    if (sub == QLatin1String("list")) {
        const ParsedOptions options = parseOptions(rest, {}, QStringLiteral("aqtruncat runner list"));
        if (!options.error.isEmpty())
            return usageError(options.error, hint);
        if (options.help)
            return help(ListHelp);
        if (!options.positionals.isEmpty())
            return usageError(QStringLiteral("unexpected argument \"%1\"").arg(options.positionals.first()), hint);
        invocation.command = Command::RunnerList;
        return ParseResult{invocation, {}, {}};
    }
    if (sub == QLatin1String("import")) {
        const QString importHint = QStringLiteral("Run \"aqtruncat runner import --help\".");
        const ParsedOptions options = parseOptions(rest, {{QStringLiteral("id"), true}, {QStringLiteral("force"), false}},
                                                   QStringLiteral("aqtruncat runner import"));
        if (!options.error.isEmpty())
            return usageError(options.error, importHint);
        if (options.help)
            return help(ImportHelp);
        if (options.positionals.size() != 1)
            return usageError(options.positionals.isEmpty() ? QStringLiteral("missing archive file")
                                                            : QStringLiteral("expected exactly one archive file"),
                              importHint);
        invocation.command = Command::RunnerImport;
        invocation.archivePath = options.positionals.first();
        invocation.id = options.value(QStringLiteral("id"));
        invocation.force = options.has(QStringLiteral("force"));
        return ParseResult{invocation, {}, {}};
    }
    if (sub == QLatin1String("remove")) {
        const QString removeHint = QStringLiteral("Run \"aqtruncat runner remove --help\".");
        const ParsedOptions options = parseOptions(rest, {}, QStringLiteral("aqtruncat runner remove"));
        if (!options.error.isEmpty())
            return usageError(options.error, removeHint);
        if (options.help)
            return help(RemoveHelp);
        if (options.positionals.size() != 1)
            return usageError(options.positionals.isEmpty() ? QStringLiteral("missing runner id")
                                                            : QStringLiteral("expected exactly one runner id"),
                              removeHint);
        invocation.command = Command::RunnerRemove;
        invocation.id = options.positionals.first();
        return ParseResult{invocation, {}, {}};
    }
    return usageError(QStringLiteral("unknown runner command \"%1\"").arg(sub), hint);
}

ParseResult parseMetrics(const QStringList &args)
{
    const QString hint = QStringLiteral("Run \"aqtruncat metrics --help\".");
    const ParsedOptions options = parseOptions(args, {{QStringLiteral("watch"), false}}, QStringLiteral("aqtruncat metrics"));
    if (!options.error.isEmpty())
        return usageError(options.error, hint);
    if (options.help)
        return help(MetricsHelp);
    if (!options.positionals.isEmpty())
        return usageError(QStringLiteral("unexpected argument \"%1\"").arg(options.positionals.first()), hint);
    Invocation invocation;
    invocation.command = Command::Metrics;
    invocation.watch = options.has(QStringLiteral("watch"));
    return ParseResult{invocation, {}, {}};
}

ParseResult parseAutostart(const QStringList &args)
{
    const QString hint = QStringLiteral("Run \"aqtruncat autostart --help\".");
    if (args.isEmpty())
        return usageError(QStringLiteral("missing autostart command (enable, disable or status)"), hint);
    const QString &sub = args.first();
    if (sub == QLatin1String("-h") || sub == QLatin1String("--help"))
        return args.size() == 1 ? help(AutostartHelp)
                                : usageError(QStringLiteral("unexpected argument \"%1\"").arg(args[1]), hint);
    Invocation invocation;
    if (sub == QLatin1String("enable"))
        invocation.command = Command::AutostartEnable;
    else if (sub == QLatin1String("disable"))
        invocation.command = Command::AutostartDisable;
    else if (sub == QLatin1String("status"))
        invocation.command = Command::AutostartStatus;
    else
        return usageError(QStringLiteral("unknown autostart command \"%1\"").arg(sub), hint);
    const ParsedOptions options = parseOptions(args.mid(1), {}, QStringLiteral("aqtruncat autostart ") + sub);
    if (!options.error.isEmpty())
        return usageError(options.error, hint);
    if (options.help)
        return help(AutostartHelp);
    if (!options.positionals.isEmpty())
        return usageError(QStringLiteral("unexpected argument \"%1\"").arg(options.positionals.first()), hint);
    return ParseResult{invocation, {}, {}};
}

} // namespace

QString mainHelp()
{
    return QStringLiteral(
        "AQTRunCat - A Cute Run Cat for Linux\n"
        "\n"
        "Usage:\n"
        "  aqtruncat [run options]      start the tray runner (same as \"aqtruncat run\")\n"
        "  aqtruncat run [options]      start the tray runner\n"
        "  aqtruncat settings           open standalone Settings\n"
        "  aqtruncat runner <command>   list, import or remove runners\n"
        "  aqtruncat metrics [--watch]  show system metrics (CPU, memory, network, disk)\n"
        "  aqtruncat autostart <cmd>    enable, disable or show Qt Tray Plugin login start\n"
        "\n"
        "Options:\n"
        "  -h, --help      show help; \"aqtruncat <command> --help\" for a command\n"
        "  -v, --version   show the version\n"
        "\n"
        "Exit status: 0 success, 1 failure, 2 invalid command line, 3 not found,\n"
        "4 invalid runner or archive, 5 conflict (already installed, protected).\n");
}

ParseResult parseCommandLine(const QStringList &arguments)
{
    const QString hint = QStringLiteral("Run \"aqtruncat --help\".");
    if (arguments.isEmpty())
        return ParseResult{Invocation{}, {}, {}};

    const QString &first = arguments.first();
    if (first == QLatin1String("-h") || first == QLatin1String("--help")) {
        if (arguments.size() > 1)
            return usageError(QStringLiteral("unexpected argument \"%1\"").arg(arguments[1]), hint);
        return help(mainHelp());
    }
    if (first == QLatin1String("-v") || first == QLatin1String("--version")) {
        if (arguments.size() > 1)
            return usageError(QStringLiteral("unexpected argument \"%1\"").arg(arguments[1]), hint);
        Invocation invocation;
        invocation.command = Command::Version;
        return ParseResult{invocation, {}, {}};
    }
    if (first == QLatin1String("--list-runners")) { // legacy alias (M2)
        if (arguments.size() > 1)
            return usageError(QStringLiteral("--list-runners takes no other arguments"), hint);
        Invocation invocation;
        invocation.command = Command::RunnerList;
        return ParseResult{invocation, {}, {}};
    }
    if (first.startsWith(u'-')) // legacy short form (M1/M2): "aqtruncat --fps 20" == "aqtruncat run --fps 20"
        return parseRun(arguments, QStringLiteral("aqtruncat"));
    if (first == QLatin1String("run"))
        return parseRun(arguments.mid(1), QStringLiteral("aqtruncat run"));
    if (first == QLatin1String("settings")) {
        const ParsedOptions options = parseOptions(arguments.mid(1), {}, QStringLiteral("aqtruncat settings"));
        if (!options.error.isEmpty())
            return usageError(options.error, QStringLiteral("Run \"aqtruncat settings --help\"."));
        if (options.help)
            return help(SettingsHelp);
        if (!options.positionals.isEmpty())
            return usageError(QStringLiteral("unexpected argument \"%1\"").arg(options.positionals.first()),
                              QStringLiteral("Run \"aqtruncat settings --help\"."));
        Invocation invocation;
        invocation.command = Command::Settings;
        return ParseResult{invocation, {}, {}};
    }
    if (first == QLatin1String("runner"))
        return parseRunner(arguments.mid(1));
    if (first == QLatin1String("metrics"))
        return parseMetrics(arguments.mid(1));
    if (first == QLatin1String("autostart"))
        return parseAutostart(arguments.mid(1));
    return usageError(QStringLiteral("unknown command \"%1\"").arg(first), hint);
}

} // namespace aqtruncat::cli
