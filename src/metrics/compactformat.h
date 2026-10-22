#pragma once

// Very short values for tray-sized presentations (the Metrics Bar). Separate
// from metricsformat.h, which serves the Dashboard and the CLI with full units.
// Locale-independent ('.' decimal separator) and never longer than 4
// characters for any finite input in range.

#include <optional>
#include <string>

namespace aqtruncat::metrics {

// Rounded to a whole percent and clamped to [0, 100]:
// 0 → "0%", 23.4 → "23%", 99.5 → "100%". Non-finite → nullopt.
std::optional<std::string> compactPercent(double percent);

// Bytes per second with binary single-letter units, the same scale as the
// Dashboard's KiB/MiB/GiB (K = KiB/s, M = MiB/s, G = GiB/s, then T, P, E):
//   below 1000 B/s      → whole bytes:   "0B", "850B"
//   below 9.95 of unit  → one decimal:   "1.0K", "1.2M"
//   below 999.5 of unit → whole number:  "10K", "850K"
//   otherwise the next unit ("1.0M" for 1000 KiB/s).
// Values past the largest unit saturate to ">999E". Negative or non-finite
// rates have no value (nullopt).
std::optional<std::string> compactRate(double bytesPerSecond);

// F5. A count ("0", "12", "9999", then "10k+"); negative → nullopt.
std::optional<std::string> compactCount(long long count);
// A whole number, rounded ("143" FPS); ≥ 9999.5 → "9999"; negative or
// non-finite → nullopt.
std::optional<std::string> compactWhole(double value);
// Degrees Celsius, rounded: "54°", "-5°"; out of [-99, 999] → nullopt.
std::optional<std::string> compactCelsius(double celsius);

} // namespace aqtruncat::metrics
