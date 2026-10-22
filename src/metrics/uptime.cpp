#include "uptime.h"

#include "textparse.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace aqtruncat::metrics {

std::optional<double> parseProcUptime(std::string_view text, std::string *error)
{
    const auto fields = splitFields(trimView(text));
    double seconds = 0.0;
    if (fields.empty()) {
        if (error)
            *error = "empty uptime";
        return std::nullopt;
    }
    const std::string_view first = fields.front();
    const auto [ptr, ec] = std::from_chars(first.data(), first.data() + first.size(), seconds);
    if (ec != std::errc() || ptr != first.data() + first.size() || !std::isfinite(seconds) || seconds < 0.0) {
        if (error)
            *error = "malformed uptime \"" + std::string(first.substr(0, 32)) + "\"";
        return std::nullopt;
    }
    return seconds;
}

std::string formatDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return {};
    const auto minutesTotal = std::uint64_t(seconds / 60.0);
    const std::uint64_t days = minutesTotal / (24 * 60);
    const std::uint64_t hours = minutesTotal / 60 % 24;
    const std::uint64_t minutes = minutesTotal % 60;
    std::string text;
    if (days > 0)
        text = std::to_string(days) + "d ";
    if (days > 0 || hours > 0)
        text += std::to_string(hours) + "h ";
    return text + std::to_string(minutes) + "m";
}

std::string compactDuration(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return {};
    const auto minutes = std::uint64_t(seconds / 60.0);
    if (minutes < 60)
        return std::to_string(minutes) + "m";
    if (minutes < 24 * 60)
        return std::to_string(minutes / 60) + "h";
    const std::uint64_t days = minutes / (24 * 60);
    return std::to_string(days > 999 ? 999 : days) + "d";
}

} // namespace aqtruncat::metrics
