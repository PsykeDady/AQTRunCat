#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QTimeZone>

#include <optional>

// Shared by the provider adapters (F2 Claude Code, F3 Codex): how one usage
// quota window is judged (current / reset since / too old / missing) and
// presented as an F1 snapshot item. Only QtCore; no provider formats here.
namespace aqtruncat::providers {

struct QuotaValue {
    double usedPercent = 0.0; // as reported; may exceed 100
    QDateTime resetsAt;       // invalid when the source gives none
    QDateTime observedAt;     // when the provider obtained or reported it
};

enum class QuotaPresence {
    Current, // a value to show
    Expired, // the report predates the window's reset
    TooOld,  // older than the maximum age
    Missing, // no source reported this window
};

struct QuotaJudgement {
    QuotaPresence presence = QuotaPresence::Missing;
    std::optional<QuotaValue> value; // the report, also when not Current
    bool stale = false;              // Current, but older than freshForSeconds
};

// A report is never shown after its window's reset; older than
// maximumAgeSeconds it is not shown at all; older than freshForSeconds it
// is shown with its time.
QuotaJudgement judgeQuota(const std::optional<QuotaValue> &newest, const QDateTime &now,
                          qint64 freshForSeconds, qint64 maximumAgeSeconds);

// The more recent of two reports of the same window.
std::optional<QuotaValue> newerQuota(const std::optional<QuotaValue> &a, const std::optional<QuotaValue> &b);

// "3%", "23.5%": one decimal only when reported.
QString quotaPercentText(double percent);
// Metrics Bar: rounded, "<1%" between 0 and 1.
QString quotaCompactText(double percent);
// "13:30" today, otherwise "Oct 23, 13:30", in the given time zone.
QString formatMoment(const QDateTime &moment, const QDateTime &now, const QTimeZone &zone);

struct QuotaItemSpec {
    QString id;          // snapshot item id (stable; Metrics Bar selection)
    QString title;       // e.g. "Current session (5h)"
    QString barLabel;    // Metrics Bar caption, e.g. "5h"
    QString missingHint; // detail when no source reported the window
    bool showRemaining = false; // detail starts with "97% left"
};

// One snapshot item: "3% used" (+ " — limit reached" at 100% or more),
// normalizedValue, detail with the reset time ("Resets at 13:30" today,
// "Resets Oct 23, 20:00" otherwise) and " · as of HH:MM" when stale; the
// bar value and caption only while Current. Expired/TooOld → "Out of date"
// with the reason, Missing → "Not reported" with the hint, both marked as a
// warning. Never 0 for missing.
QJsonObject quotaItem(const QuotaItemSpec &spec, const QuotaJudgement &judgement, const QDateTime &now,
                      const QTimeZone &zone);

} // namespace aqtruncat::providers
