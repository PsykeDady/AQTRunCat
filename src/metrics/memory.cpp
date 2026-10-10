#include "memory.h"

#include <algorithm>
#include <charconv>
#include <map>

namespace aqtruncat::metrics {

namespace {

void setError(std::string *error, std::string message)
{
    if (error)
        *error = std::move(message);
}

std::string_view trim(std::string_view s)
{
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos)
        return {};
    const auto end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

} // namespace

std::optional<MemoryInfo> parseMeminfo(std::string_view text, std::string *error)
{
    static constexpr std::string_view keys[] = {"MemTotal", "MemFree", "MemAvailable", "Buffers", "Cached",
                                                "SReclaimable", "Shmem", "SwapTotal", "SwapFree"};
    std::map<std::string_view, std::uint64_t> kib;

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t end = std::min(text.find('\n', pos), text.size());
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        const std::string_view key = trim(line.substr(0, colon));
        if (std::find(std::begin(keys), std::end(keys), key) == std::end(keys))
            continue; // only the fields we use are validated
        std::string_view value = trim(line.substr(colon + 1));
        if (value.size() > 2 && value.substr(value.size() - 2) == "kB")
            value = trim(value.substr(0, value.size() - 2));
        std::uint64_t number = 0;
        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (ec != std::errc() || ptr != value.data() + value.size()) {
            setError(error, "invalid value for " + std::string(key) + ": \"" + std::string(value) + "\"");
            return std::nullopt;
        }
        kib[key] = number;
    }

    auto field = [&kib](std::string_view key) -> std::optional<std::uint64_t> {
        const auto it = kib.find(key);
        return it == kib.end() ? std::nullopt : std::optional<std::uint64_t>(it->second);
    };
    const auto total = field("MemTotal");
    const auto free = field("MemFree");
    if (!total || *total == 0) {
        setError(error, "MemTotal missing or zero");
        return std::nullopt;
    }
    if (!free) {
        setError(error, "MemFree missing");
        return std::nullopt;
    }

    MemoryInfo info;
    std::uint64_t availableKiB = 0;
    if (const auto available = field("MemAvailable")) {
        availableKiB = *available;
    } else {
        // Pre-3.14 kernels: free memory plus caches the kernel can reclaim.
        const std::uint64_t reclaimable = field("Buffers").value_or(0) + field("Cached").value_or(0)
            + field("SReclaimable").value_or(0);
        const std::uint64_t shmem = field("Shmem").value_or(0); // counted in Cached, not reclaimable
        availableKiB = *free + (reclaimable > shmem ? reclaimable - shmem : 0);
        info.availableIsEstimate = true;
    }
    availableKiB = std::min(availableKiB, *total);

    info.totalBytes = *total * 1024;
    info.availableBytes = availableKiB * 1024;
    info.usedBytes = info.totalBytes - info.availableBytes;
    info.usagePercent = 100.0 * double(info.usedBytes) / double(info.totalBytes);

    const std::uint64_t swapTotal = field("SwapTotal").value_or(0);
    if (swapTotal > 0) {
        const std::uint64_t swapFree = std::min(field("SwapFree").value_or(swapTotal), swapTotal);
        info.swapPresent = true;
        info.swapTotalBytes = swapTotal * 1024;
        info.swapUsedBytes = (swapTotal - swapFree) * 1024;
        info.swapUsagePercent = 100.0 * double(info.swapUsedBytes) / double(info.swapTotalBytes);
    }
    return info;
}

} // namespace aqtruncat::metrics
