#pragma once

#include "metrics/systemmetrics.h"

#include <QString>
#include <QStringList>

#include <cstdint>

// F6: the compact system summary shown when the pointer rests on the runner
// (tray tooltip of the Qt frontend). Built from the snapshot the frontend
// already has: no sampling, no I/O. The Plasmoid builds the same lines from
// the API payload in contents/code/logic.js (hoverLines); both use the
// backend's formatting (metricsformat), so they read alike.
//
//   CPU: 18.4 %
//   RAM: 46.0 % · 7.2 GiB / 15.5 GiB used
//   Swap: 2.5 % · 0.3 GiB / 12.0 GiB        (left out when there is no swap)
//
// A value that is not there is said in words ("waiting for data",
// "unavailable", "error"), never shown as 0; the backend's own messages are
// not repeated here (the Dashboard has them).
namespace aqtruncat::hover {

enum class Connection {
    Connecting,  // no state from the backend yet
    Connected,
    Unavailable, // the backend went away (after the client's grace period)
};

inline const QString ConnectingText = QStringLiteral("Connecting to the backend…");
inline const QString UnavailableText = QStringLiteral("Backend not running");

// The lines after the title. Not connected: one line about the backend
// and no values (they would not be current).
QStringList summaryLines(Connection connection, const metrics::SystemMetricsSnapshot *snapshot);

// "AQTRunCat — <runner>" followed by the lines, one per row.
QString runnerToolTip(const QString &runnerName, const QStringList &lines);

// A past moment (epoch seconds) in local time: "14:05" today, else
// "Oct 21, 14:05" (C locale). Shared by the API detail texts and the
// Metrics Bar tooltips ("checked 14:05").
QString whenText(std::int64_t epochSeconds);

} // namespace aqtruncat::hover
