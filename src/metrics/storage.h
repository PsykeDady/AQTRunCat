#pragma once

// Storage usage: capacity of the filesystem holding a path (statvfs).
// Not to be confused with disk activity (I/O rates, diskio.h).

#include <cstdint>
#include <optional>
#include <string>

namespace aqtruncat::metrics {

// The statvfs fields used, in units of fragmentSize.
struct FilesystemStats {
    std::uint64_t fragmentSize = 0;    // f_frsize
    std::uint64_t blocks = 0;          // f_blocks: total size
    std::uint64_t freeBlocks = 0;      // f_bfree: free, including blocks reserved for root
    std::uint64_t availableBlocks = 0; // f_bavail: free for unprivileged users
};

struct StorageInfo {
    std::string path;
    std::uint64_t totalBytes = 0;     // blocks
    std::uint64_t usedBytes = 0;      // blocks - freeBlocks
    std::uint64_t availableBytes = 0; // availableBlocks: what the user can still write
    double usagePercent = 0.0;        // used / (used + available), like df's "Use%"
};

// Same semantics as df: the root-reserved blocks count neither as used nor as
// available, so used + available can be less than total and the percentage
// is relative to the space an ordinary user can have.
std::optional<StorageInfo> computeStorage(const std::string &path, const FilesystemStats &stats,
                                          std::string *error = nullptr);

// statvfs(path); nullopt with *error on failure.
std::optional<FilesystemStats> queryFilesystem(const std::string &path, std::string *error = nullptr);

} // namespace aqtruncat::metrics
