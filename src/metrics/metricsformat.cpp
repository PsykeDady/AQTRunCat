#include "metricsformat.h"

#include <charconv>
#include <cmath>

namespace aqtruncat::metrics {

namespace {

std::string oneDecimal(double value)
{
    char buffer[64];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, 1);
    return ec == std::errc() ? std::string(buffer, end) : std::string("?");
}

std::string scaled(double value, const char *suffix)
{
    static const char *const units[] = {"KiB", "MiB", "GiB", "TiB", "PiB"};
    if (value < 1023.5) // rounds below 1024: 1023.6 B is shown as 1.0 KiB, not "1024 B"
        return std::to_string(static_cast<std::uint64_t>(std::llround(value))) + " B" + suffix;
    int unit = -1;
    do {
        value /= 1024.0;
        ++unit;
    } while (value >= 1024.0 && unit < 4);
    // 1023.96 KiB would print as "1024.0 KiB": move to the next unit instead.
    if (std::round(value * 10.0) >= 10240.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return oneDecimal(value) + " " + units[unit] + suffix;
}

} // namespace

std::string formatBytes(std::uint64_t bytes)
{
    return scaled(double(bytes), "");
}

std::string formatRate(double bytesPerSecond)
{
    if (!std::isfinite(bytesPerSecond) || bytesPerSecond < 0.0)
        return UnavailableText;
    return scaled(bytesPerSecond, "/s");
}

std::string formatPercent(double percent)
{
    if (!std::isfinite(percent))
        return UnavailableText;
    return oneDecimal(percent) + " %";
}

} // namespace aqtruncat::metrics
