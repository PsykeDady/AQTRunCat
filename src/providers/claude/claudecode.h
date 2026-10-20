#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>

// F2 Claude Code adapter: read-only acquisition from Claude Code's local
// state, turned into one F1 snapshot (custom metric "output": "snapshot").
// Only QtCore; the AQTRunCat core never links this code.
//
// Primary values: the plan usage quotas of /usage (claudeusage.h). Optional
// secondary values (--context): the context window of the most recently
// active session, from
//
//   <config>/sessions/<pid>.json         live-session registry (internal,
//                                        Claude Code >= 2.1.x): pid,
//                                        procStart, sessionId, cwd, status
//   <config>/projects/*/<session>.jsonl  session transcript; only the tail is
//                                        read, only usage/model fields kept
//
// <config> is $CLAUDE_CONFIG_DIR or ~/.claude. Nothing is written there and
// no prompt, response or credential is ever read into the output.
namespace aqtruncat::claude {

inline constexpr qint64 StandardContextWindow = 200000;
inline constexpr qint64 ExtendedContextWindow = 1000000;
inline constexpr qint64 InitialTailBytes = 256 * 1024;
inline constexpr qint64 MaximumTailBytes = 8 * 1024 * 1024;
inline constexpr qint64 MaximumRegistryFileBytes = 64 * 1024;
inline constexpr int MaximumRegistryEntries = 256;

struct LiveSession {
    qint64 pid = 0;
    QString sessionId;
    QString cwd;
    QString status;    // e.g. "busy", "idle"; empty when not reported
    qint64 updatedAtMilliseconds = 0;
    QString transcriptPath; // empty until the first message is written
    QDateTime transcriptModified;
};

struct RegistryScan {
    QList<LiveSession> live;
    int entries = 0;     // <pid>.json files considered
    int unsupported = 0; // unreadable or without pid/sessionId
};

// Reads the registry and keeps entries whose process is still running.
// procRoot is "/proc" outside tests.
RegistryScan scanRegistry(const QString &configDirectory, const QString &procRoot = QStringLiteral("/proc"));

// True when <procRoot>/<pid> is the process that wrote the entry. With
// procStart (field 22 of /proc/<pid>/stat) a reused PID is rejected; older
// entries without it fall back to a "claude" command line.
bool processMatches(const QString &procRoot, qint64 pid, const QString &procStart);

// <config>/projects/*/<sessionId>.jsonl, one directory level only.
QString findTranscript(const QString &configDirectory, const QString &sessionId);

struct Usage {
    qint64 inputTokens = 0;
    qint64 cacheCreationInputTokens = 0;
    qint64 cacheReadInputTokens = 0;
    qint64 outputTokens = 0;
    QString model;          // message.model, e.g. "claude-opus-5-5"
    QString requestedModel; // e.g. "claude-opus-5-5[1m]"; empty if absent
    QDateTime timestamp;
    bool compactedSince = false; // a /compact boundary follows this response

    // Context-window occupancy as Claude Code's status line computes it:
    // input tokens only (fresh + cache writes + cache reads), not output.
    qint64 contextTokens() const { return inputTokens + cacheCreationInputTokens + cacheReadInputTokens; }
};

enum class UsageStatus {
    Found,
    NoResponse,  // no main-conversation response recorded yet
    Unreadable,  // I/O or permission error
    Unsupported, // the format no longer matches what is documented in F2
};

struct UsageResult {
    UsageStatus status = UsageStatus::NoResponse;
    std::optional<Usage> usage;
    QString error; // never contains transcript text
};

// Scans the transcript backwards from its end, InitialTailBytes first and
// growing up to maximumBytes, for the last main-conversation (non-sidechain,
// non-synthetic) assistant entry with usage.
UsageResult readLatestUsage(const QString &transcriptPath, qint64 maximumBytes = MaximumTailBytes);

// The denominator for the context percentage. Claude Code documents 200000
// by default and 1000000 for extended-context models, selected with the
// "[1m]" model suffix. Unknown (empty) requested model: no denominator.
std::optional<qint64> contextWindowFor(const QString &requestedModel);

// 950 → "950", 152345 → "152.3k", 1000000 → "1M", 1250000 → "1.25M".
QString formatTokens(qint64 tokens);

struct Options {
    QString configDirectory;
    QString globalConfigFile;  // .claude.json (claudeusage.h globalConfigFile)
    QString bridgeStateFile;   // live-updates state; empty: not used
    QString procRoot = QStringLiteral("/proc");
    bool includeContext = false;
    QString cwdFilter; // context: only sessions whose cwd is this directory or below
    QDateTime now = QDateTime::currentDateTimeUtc();
    QTimeZone timeZone = QTimeZone::systemTimeZone();
};

// Exit status contract for F1: 0 with a snapshot on stdout; 69
// (EX_UNAVAILABLE) when Claude Code is not installed, or when there is no
// usage report at all (and no context section was asked for); 1 when local
// data cannot be read or is not understood. The diagnostic goes to stderr.
struct Outcome {
    int exitCode = 0;
    QByteArray standardOutput;
    QString diagnostic;
};

Outcome collect(const Options &options);

QString defaultConfigDirectory();

} // namespace aqtruncat::claude
