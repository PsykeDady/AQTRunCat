#pragma once

// Human readable values for metrics: binary units (KiB = 1024 B), one decimal,
// '.' as decimal separator whatever the process locale. Presentation only:
// the metrics model always keeps raw bytes, bytes/s and percent.

#include <cstdint>
#include <optional>
#include <string>

namespace aqtruncat::metrics {

// 0 → "0 B", 1023 → "1023 B", 1024 → "1.0 KiB", 1536 → "1.5 KiB", … TiB.
std::string formatBytes(std::uint64_t bytes);

// formatBytes plus "/s"; fractions of a byte per second round to whole bytes.
std::string formatRate(double bytesPerSecond);

// 23.44 → "23.4 %"
std::string formatPercent(double percent);

// F5: 54.4 → "54 °C"; 143.6 → "144 FPS"; counts → "Up to date", "1 update", "12 updates".
std::string formatCelsius(double celsius);
std::string formatFps(double fps);
std::string formatUpdateCount(long long count);

// Shown for values that are not available (never "0").
inline constexpr const char *UnavailableText = "n/a";

} // namespace aqtruncat::metrics
