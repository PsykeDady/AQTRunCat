#pragma once

// AQTRunCat command line grammar:
//
//   aqtruncat [run options]                 same as "aqtruncat run" (legacy/short form)
//   aqtruncat run [--runner ID] [--fps N] [--verbose]
//   aqtruncat runner list
//   aqtruncat runner import FILE.zip [--id ID] [--force]
//   aqtruncat runner remove ID
//   aqtruncat metrics [--watch]
//   aqtruncat autostart enable|disable|status
//   aqtruncat --list-runners                legacy alias of "aqtruncat runner list"
//   aqtruncat [<command>] --help | -h,  aqtruncat --version | -v
//
// Parsing is pure (no QCoreApplication needed) so it can be unit tested.
// New command groups (e.g. "config") are added as new words in
// parseCommandLine() with their own option table.

#include <QString>
#include <QStringList>

#include <optional>

namespace aqtruncat::cli {

// Exit status of every command.
enum ExitCode : int {
    ExitOk = 0,
    ExitFailure = 1,  // filesystem error, tray application failure, anything else
    ExitUsage = 2,    // invalid command line
    ExitNotFound = 3, // runner or file not found
    ExitInvalid = 4,  // invalid runner, archive or id
    ExitConflict = 5, // already installed, bundled runner protected, ambiguous id
};

enum class Command {
    Run, RunnerList, RunnerImport, RunnerRemove, Metrics,
    AutostartEnable, AutostartDisable, AutostartStatus,
    Help, Version
};

struct Invocation {
    Command command = Command::Run;

    // run
    QString runnerId;
    int fps = 0; // 0: CPU driven
    bool verbose = false;

    // runner import / remove
    QString archivePath;
    QString id; // import: optional id override; remove: the runner to remove
    bool force = false;

    // metrics
    bool watch = false;

    // help
    QString helpText;
};

struct ParseResult {
    std::optional<Invocation> invocation;
    QString error; // usage error, when invocation is empty
    QString hint;  // e.g. "Run 'aqtruncat runner --help'."
};

// `arguments` excludes the program name.
ParseResult parseCommandLine(const QStringList &arguments);

QString mainHelp();

} // namespace aqtruncat::cli
