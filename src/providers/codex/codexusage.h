#pragma once

#include "common/quota.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTimeZone>

#include <optional>

// F3 Codex adapter: Codex usage limits (the windows Codex's /status shows,
// e.g. "5h limit" and "Weekly limit") turned into one F1 snapshot. Only
// QtCore; the AQTRunCat core never links this code. Two read-only sources,
// combined per window (the most recent report wins):
//
//   local  <codex home>/sessions/**/rollout-*.jsonl (and archived_sessions):
//          the rate_limits of the newest "token_count" event of the "codex"
//          limit bucket. Codex writes them while it works; nothing else of
//          the session is used. Offline, no Codex process.
//   live   opt-in: "codex app-server" (stdio JSON-RPC) → initialize →
//          account/rateLimits/read, at most every LiveRefreshSeconds,
//          cached in $XDG_RUNTIME_DIR/aqtruncat/codex-rate-limits.json.
//          Codex asks OpenAI with its own login; no tokens are used and no
//          credential is read by AQTRunCat.
//
// <codex home> is $CODEX_HOME or ~/.codex. Nothing is written there.
namespace aqtruncat::codex {

inline constexpr int MaximumRolloutFiles = 8;              // newest files searched
inline constexpr qint64 InitialTailBytes = 256 * 1024;
inline constexpr qint64 MaximumTailBytes = 8 * 1024 * 1024; // per file
inline constexpr int MaximumScannedFiles = 20000;
inline constexpr qint64 LiveRefreshSeconds = 5 * 60;
inline constexpr qint64 LiveRetrySeconds = 60;
inline constexpr int LiveTimeoutMs = 20000;
// Live data is refreshed every 5 minutes: "as of" only after 10.
inline constexpr qint64 FreshForSeconds = 10 * 60;
inline constexpr qint64 MaximumReportAgeSeconds = 24 * 60 * 60;
// The limit bucket of Codex usage ("codex"); older Codex versions wrote no
// limit_id. Other buckets (e.g. "premium") are not this quota.
inline const QString CodexLimitId = QStringLiteral("codex");

enum class Source {
    Local,
    Live,
};

struct Window {
    int minutes = 0; // window length as reported (300 = 5 hours)
    aqtruncat::providers::QuotaValue value;
};

struct Report {
    QList<Window> windows;
    QDateTime observedAt;
    QString planType;          // e.g. "plus"; empty when not reported
    QString limitReachedType;  // e.g. "rate_limit_reached"; empty when not reached
    std::optional<bool> ordinaryUsageAllowed; // live only
};

struct SourceResult {
    std::optional<Report> report;
    QString error; // unreadable/unsupported/failed; never contains private text
};

QString defaultCodexHome();

// Newest "codex" rate_limits among the newest rollout files.
SourceResult readLocalReport(const QString &codexHome);

// One rollout line → a report if it is a token_count event of the codex
// bucket with at least one window (exposed for tests).
std::optional<Report> reportFromRolloutLine(const QByteArray &line);

// account/rateLimits/read "result" object → a report observed at `now`.
std::optional<Report> reportFromLiveResult(const QJsonObject &result, const QDateTime &now, QString *error);

// Window titles and captions from the reported length, never assumed:
// 300 → "5h limit"/"cx5h", 10080 → "Weekly limit"/"cx7d", 1440 → "Daily
// limit"/"cx1d", others "N-day"/"Nh"/"N-minute".
QString windowTitle(int minutes);
QString windowBarLabel(int minutes);
QString windowId(int minutes); // "window_300m"

// --- live state ------------------------------------------------------------

QString liveStateFile(); // $XDG_RUNTIME_DIR/aqtruncat/codex-rate-limits.json

struct LiveState {
    std::optional<Report> report; // last successful read
    QDateTime capturedAt;
    QDateTime lastAttemptAt;
    QString error; // of the last attempt, if it failed
};

LiveState readLiveState(const QString &stateFile, const QString &codexHome);
bool writeLiveState(const QString &stateFile, const QString &codexHome, const LiveState &state);

// Runs `<codex> app-server`, reads the usage and stops it. Never sends a
// prompt; never modifies Codex configuration.
// The report is stamped with `now` (the caller's clock).
SourceResult fetchLive(const QString &codexExecutable, const QString &codexHome, bool codexHomeOverridden,
                       const QDateTime &now = QDateTime::currentDateTimeUtc(), int timeoutMs = LiveTimeoutMs);

// --- snapshot --------------------------------------------------------------

struct Options {
    QString codexHome;
    bool codexHomeOverridden = false;
    QString codexExecutable;  // empty: not found
    bool live = false;
    QString liveStateFile;    // empty: live data not used
    QDateTime now = QDateTime::currentDateTimeUtc();
    QTimeZone timeZone = QTimeZone::systemTimeZone();
};

struct Outcome {
    int exitCode = 0; // 0 snapshot, 69 Codex or any report absent, 1 error
    QByteArray standardOutput;
    QString diagnostic;
};

// With options.live, refreshes the live state first when it is due.
Outcome collect(const Options &options);

} // namespace aqtruncat::codex
