#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

// 2.3.4 automatic /usage refresh (opt-in). Claude Code refreshes its usage
// cache (~/.claude.json "cachedUsageUtilization", claudeusage.h) only when
// /usage runs. Verified on Claude Code 2.1.316: in print mode
//
//   claude -p /usage --output-format json
//
// runs /usage as a local command ("local_command": "usage", num_turns 0,
// every token counter 0, total_cost_usd 0) and rewrites that cache in under
// a second. So when the newest usage report is older than the configured
// interval, the adapter runs it once and reads the cache again.
//
// Safety: the command's JSON result is checked after every run. Should a
// Claude Code version ever send "/usage" to the model instead (tokens, cost
// or turns above zero, or another local command), automatic refresh stops
// at once and stays stopped until it is turned off and on again: at most one
// prompt is ever spent.
//
// Nothing is read from the command's output except that JSON envelope; the
// usage values themselves still come only from the cache.
namespace aqtruncat::claude {

inline constexpr int DefaultRefreshMinutes = 5;
// A failed run is retried after this, not on every snapshot.
inline constexpr qint64 RefreshRetrySeconds = 60;
// A click on the metric forces a run unless the last one began this recently.
inline constexpr qint64 ManualRefreshSpacingSeconds = 30;
inline constexpr int RefreshTimeoutMs = 20000;
inline constexpr qint64 MaximumCommandOutputBytes = 1024 * 1024;

// claude in PATH, else the per-user install locations a desktop session's
// PATH may lack (~/.local/bin, ~/.claude/local). Empty when not found.
QString findClaudeExecutable();

// The arguments after the executable. --no-session-persistence: no session
// file is written; --strict-mcp-config: no MCP server is started.
QStringList usageCommandArguments();
// For people: "claude -p /usage --output-format json …".
QString usageCommandText();

// $XDG_RUNTIME_DIR/aqtruncat/claude-usage-refresh.json (private to the user).
QString refreshStateFile();

struct RefreshState {
    QDateTime lastAttemptAt;
    QDateTime lastSuccessAt;
    QString error;   // of the last attempt, if it failed
    QString stopped; // why automatic refresh stopped itself (see above)
};

// Only state recorded for the same Claude Code directory is used.
RefreshState readRefreshState(const QString &stateFile, const QString &configDirectory);
bool writeRefreshState(const QString &stateFile, const QString &configDirectory, const RefreshState &state);

enum class RefreshTrigger {
    Schedule, // AQTRunCat's periodic run
    Manual,   // a click on the metric (AQTRUNCAT_REFRESH=manual)
};

// Whether to run /usage now. Schedule: the newest report (none counts as
// old) is at least refreshSeconds old, and the last attempt is at least
// refreshSeconds ago (RefreshRetrySeconds after a failure). Manual: the last
// attempt began at least ManualRefreshSpacingSeconds ago. Never once stopped.
bool refreshDue(const RefreshState &state, const std::optional<QDateTime> &newestReport, RefreshTrigger trigger,
                qint64 refreshSeconds, const QDateTime &now);

struct UsageCommandResult {
    bool ok = false;
    bool modelUsed = false; // the stop condition above
    QString error;          // never contains the command's output
};

// Judges the JSON that `claude -p … --output-format json` printed.
UsageCommandResult judgeUsageCommandOutput(const QByteArray &standardOutput);

// Runs the command in its own process group with stdin at /dev/null and
// stops the whole group on timeout. CLAUDE_CONFIG_DIR is set only when the
// Claude Code directory was chosen explicitly.
UsageCommandResult runUsageCommand(const QString &claudeExecutable, const QString &configDirectory,
                                   bool configDirectoryOverridden, int timeoutMs = RefreshTimeoutMs);

} // namespace aqtruncat::claude
