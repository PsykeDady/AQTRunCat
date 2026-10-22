#include "compactformat.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace aqtruncat::metrics {

namespace {

std::string fixed(double value, int decimals)
{
    char buffer[64];
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, decimals);
    return ec == std::errc() ? std::string(buffer, end) : std::string("?");
}

} // namespace

std::optional<std::string> compactPercent(double percent)
{
    if (!std::isfinite(percent))
        return std::nullopt;
    return fixed(std::round(std::clamp(percent, 0.0, 100.0)), 0) + "%";
}

std::optional<std::string> compactRate(double bytesPerSecond)
{
    if (!std::isfinite(bytesPerSecond) || bytesPerSecond < 0.0)
        return std::nullopt;
    if (std::round(bytesPerSecond) < 1000.0)
        return fixed(std::round(bytesPerSecond), 0) + "B";

    static constexpr char units[] = {'K', 'M', 'G', 'T', 'P', 'E'};
    double value = bytesPerSecond;
    for (const char unit : units) {
        value /= 1024.0;
        // Decide on the rounded value, so 9.96 becomes "10K", never "10.0K",
        // and 999.6 becomes "1.0M", never "1000K".
        if (std::round(value * 10.0) < 100.0)
            return fixed(value, 1) + unit;
        if (std::round(value) < 1000.0)
            return fixed(value, 0) + unit;
    }
    return std::string(">999E");
}

std::optional<std::string> compactCount(long long count)
{
    if (count < 0)
        return std::nullopt;
    if (count > 9999)
        return std::string("10k+");
    return std::to_string(count);
}

std::optional<std::string> compactWhole(double value)
{
    if (!std::isfinite(value) || value < 0.0)
        return std::nullopt;
    return fixed(std::min(std::round(value), 9999.0), 0);
}

std::optional<std::string> compactCelsius(double celsius)
{
    if (!std::isfinite(celsius))
        return std::nullopt;
    const double rounded = std::round(celsius);
    if (rounded < -99.0 || rounded > 999.0)
        return std::nullopt;
    return fixed(rounded, 0) + "°";
}

} // namespace aqtruncat::metrics
