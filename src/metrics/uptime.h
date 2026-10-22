#pragma once

// F5.1 system uptime: time since boot as the kernel reports it in
// /proc/uptime (CLOCK_BOOTTIME, suspend included). Not the uptime of any
// process. Values are raw seconds; formatting is separate.

#include "metricstatus.h"

#include <optional>
#include <string>
#include <string_view>

namespace aqtruncat::metrics {

struct UptimeMetrics {
    MetricStatus status;
    std::optional<double> seconds; // since boot
};

// First field of /proc/uptime ("12345.67 98765.43"); nullopt (with a reason)
// when it is missing, negative or not a number.
std::optional<double> parseProcUptime(std::string_view text, std::string *error = nullptr);

// Whole minutes in days, hours and minutes, largest unit first and without
// zero leading units: 59 s → "0m", 3600 → "1h 0m", 90061 → "1d 1h 1m".
// Seconds are truncated, never rounded up. Negative/non-finite → "".
std::string formatDuration(double seconds);

// At most 4 characters for the Metrics Bar: the largest whole unit,
// truncated: "0m", "59m", "23h", "6d", "999d", then ">999" days → "999d".
std::string compactDuration(double seconds);

} // namespace aqtruncat::metrics
