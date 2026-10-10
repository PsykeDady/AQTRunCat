#pragma once

// RAM and swap from /proc/meminfo. All sizes in bytes.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aqtruncat::metrics {

struct MemoryInfo {
    std::uint64_t totalBytes = 0;     // MemTotal
    std::uint64_t availableBytes = 0; // MemAvailable: what can be given to applications without swapping
    std::uint64_t usedBytes = 0;      // total - available (page cache that can be dropped is not "used")
    double usagePercent = 0.0;        // used / total
    bool availableIsEstimate = false; // MemAvailable missing (kernel < 3.14): see parseMeminfo

    bool swapPresent = false;         // SwapTotal > 0
    std::uint64_t swapTotalBytes = 0;
    std::uint64_t swapUsedBytes = 0;  // SwapTotal - SwapFree
    double swapUsagePercent = 0.0;
};

// Parses /proc/meminfo content ("Key:   value kB" lines, kB = KiB).
// Requires MemTotal > 0 and MemFree. Without MemAvailable, available is
// estimated as MemFree + Buffers + Cached + SReclaimable - Shmem (clamped to
// [MemFree, MemTotal]) and availableIsEstimate is set. Swap values are
// optional; without SwapTotal there is no swap.
std::optional<MemoryInfo> parseMeminfo(std::string_view text, std::string *error = nullptr);

} // namespace aqtruncat::metrics
