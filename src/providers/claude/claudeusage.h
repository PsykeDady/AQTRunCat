#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

#include "common/quota.h"

#include <optional>

// Claude Code plan usage quotas: the rolling 5-hour window ("Current
// session") and the 7-day window ("Current week (all models)") shown by
// Claude Code's /usage screen. Two local, read-only sources are combined and
// the most recent report of each window wins:
//
//   usage cache   ~/.claude.json "cachedUsageUtilization" (internal): what
//                 /usage itself fetched from the plan-usage endpoint, with
//                 its fetch time. Claude Code refreshes it when /usage or an
//                 IDE usage view is opened, not continuously.
//   status line   rate_limits from the documented status-line JSON, captured
//                 by "aqtruncat-claude-metric statusline" only when the user
//                 turned live updates on (statuslinebridge.h). Claude Code
//                 sends it after every API response.
//
// No credentials are read, nothing is requested from the network and no
// value is ever derived from context-window tokens.
namespace aqtruncat::claude {

inline constexpr qint64 MaximumGlobalConfigBytes = 32 * 1024 * 1024;
inline constexpr qint64 MaximumBridgeStateBytes = 64 * 1024;
inline constexpr qint64 MaximumStatusLineInputBytes = 1024 * 1024;
// Older reports are still shown, with an "as of" time.
inline constexpr qint64 FreshForSeconds = 5 * 60;
// A report never outlives its window's reset, and one older than this is not
// shown as a current value at all (only a weekly report can get this old).
inline constexpr qint64 MaximumReportAgeSeconds = 24 * 60 * 60;

enum class QuotaWindow {
    FiveHour,
    SevenDay,
};

enum class QuotaSource {
    UsageCache,
    StatusLine,
};

struct QuotaReading {
    double usedPercent = 0.0; // as reported; may exceed 100
    QDateTime resetsAt;       // invalid when the source gives none
    QDateTime observedAt;     // when Claude Code obtained or reported it
    QuotaSource source = QuotaSource::UsageCache;
};

struct QuotaSourceResult {
    bool present = false; // the source has usage data at all
    std::optional<QuotaReading> fiveHour;
    std::optional<QuotaReading> sevenDay;
    QString error; // unreadable or unsupported; never contains file content
    QString note;  // why present data was ignored (e.g. another account)

    const std::optional<QuotaReading> &window(QuotaWindow w) const { return w == QuotaWindow::FiveHour ? fiveHour : sevenDay; }
};

// Claude Code keeps its global state in <CLAUDE_CONFIG_DIR>/.claude.json when
// that variable is set, otherwise in ~/.claude.json (outside ~/.claude).
QString globalConfigFile(const QString &configDirectory, bool configDirectoryOverridden);

// Reads only cachedUsageUtilization and oauthAccount.accountUuid. A cache
// written for another account than the signed-in one is ignored.
QuotaSourceResult readUsageCache(const QString &globalConfigFile);

// $XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json (private to the user).
QString bridgeStateFile();

// Only windows captured for the same Claude Code directory are used.
QuotaSourceResult readBridgeState(const QString &stateFile, const QString &configDirectory);

// Status-line side: extracts rate_limits.{five_hour,seven_day} from the JSON
// Claude Code pipes to the status line and merges them into the state file.
// Nothing else of the status-line input is stored. Returns false (with the
// reason) when nothing was written; a status line without rate_limits (API
// key users, before the first response) leaves the file as it was.
bool captureStatusLine(const QByteArray &statusLineJson, const QString &stateFile,
                       const QString &configDirectory, const QDateTime &now, QString *reason = nullptr);

// Judgement and formatting are shared with the other providers.
using aqtruncat::providers::QuotaPresence;
using aqtruncat::providers::formatMoment;
using aqtruncat::providers::quotaCompactText;
using aqtruncat::providers::quotaPercentText;

struct QuotaChoice {
    QuotaPresence presence = QuotaPresence::Missing;
    std::optional<QuotaReading> reading; // the newest report, also when not Current
    bool stale = false;                  // Current, but older than FreshForSeconds
};

// The most recent report of a window among the sources, judged at "now".
QuotaChoice chooseQuota(QuotaWindow window, const QuotaSourceResult &cache, const QuotaSourceResult &bridge,
                        const QDateTime &now);

} // namespace aqtruncat::claude
