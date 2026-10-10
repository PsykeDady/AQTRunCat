#include "cpustat.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace aqtruncat {

namespace {

void setError(std::string *error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::string_view aggregateCpuLine(std::string_view text)
{
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view line = text.substr(pos, end - pos);
        // "cpu" followed by whitespace; "cpu0", "cpu1"… are per-CPU lines.
        if (line.size() > 3 && line.substr(0, 3) == "cpu" && (line[3] == ' ' || line[3] == '\t'))
            return line;
        pos = end + 1;
    }
    return {};
}

} // namespace

std::optional<CpuTimes> parseProcStat(std::string_view text, std::string *error)
{
    std::string_view line = aggregateCpuLine(text);
    if (line.empty()) {
        setError(error, "no aggregate \"cpu\" line");
        return std::nullopt;
    }
    line.remove_prefix(3);

    std::array<std::uint64_t, 10> values{};
    std::size_t count = 0;
    while (true) {
        const std::size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string_view::npos)
            break;
        line.remove_prefix(start);
        const std::size_t length = std::min(line.find_first_of(" \t\r"), line.size());
        const std::string_view token = line.substr(0, length);
        line.remove_prefix(length);
        if (count == values.size())
            break; // future kernels may append fields; ignore them
        std::uint64_t value = 0;
        const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (ec != std::errc() || end != token.data() + token.size()) {
            setError(error, "invalid field " + std::to_string(count + 1) + ": \"" + std::string(token) + "\"");
            return std::nullopt;
        }
        values[count++] = value;
    }
    if (count < 4) {
        setError(error, "expected at least 4 fields, got " + std::to_string(count));
        return std::nullopt;
    }

    return CpuTimes{values[0], values[1], values[2], values[3], values[4],
                    values[5], values[6], values[7], values[8], values[9]};
}

int countLogicalCpus(std::string_view text)
{
    int count = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view line = text.substr(pos, end - pos);
        if (line.size() > 3 && line.substr(0, 3) == "cpu" && line[3] >= '0' && line[3] <= '9')
            ++count;
        pos = end + 1;
    }
    return count;
}

std::optional<CpuDelta> cpuBusyBetween(const CpuTimes &previous, const CpuTimes &current)
{
    bool regressed = false;
    auto delta = [&regressed](std::uint64_t before, std::uint64_t after) -> std::uint64_t {
        if (after < before) {
            regressed = true;
            return 0;
        }
        return after - before;
    };

    const std::uint64_t active = delta(previous.user, current.user) + delta(previous.nice, current.nice)
        + delta(previous.system, current.system) + delta(previous.irq, current.irq)
        + delta(previous.softirq, current.softirq) + delta(previous.steal, current.steal);
    const std::uint64_t idle = delta(previous.idle, current.idle) + delta(previous.iowait, current.iowait);
    const std::uint64_t total = active + idle;
    if (total == 0)
        return std::nullopt;

    const double percent = 100.0 * static_cast<double>(active) / static_cast<double>(total);
    return CpuDelta{std::clamp(percent, 0.0, 100.0), regressed};
}

std::optional<CpuDelta> CpuSampler::add(const CpuTimes &sample)
{
    std::optional<CpuDelta> result;
    if (m_previous)
        result = cpuBusyBetween(*m_previous, sample);
    m_previous = sample;
    return result;
}

} // namespace aqtruncat
