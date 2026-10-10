#include "storage.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <sys/statvfs.h>

namespace aqtruncat::metrics {

std::optional<StorageInfo> computeStorage(const std::string &path, const FilesystemStats &stats, std::string *error)
{
    auto fail = [error](std::string message) -> std::optional<StorageInfo> {
        if (error)
            *error = std::move(message);
        return std::nullopt;
    };
    if (stats.fragmentSize == 0 || stats.blocks == 0)
        return fail("filesystem reports no size");
    if (stats.freeBlocks > stats.blocks || stats.availableBlocks > stats.freeBlocks)
        return fail("inconsistent filesystem statistics");
    if (stats.blocks > std::numeric_limits<std::uint64_t>::max() / stats.fragmentSize)
        return fail("filesystem size overflows");

    StorageInfo info;
    info.path = path;
    info.totalBytes = stats.blocks * stats.fragmentSize;
    info.usedBytes = (stats.blocks - stats.freeBlocks) * stats.fragmentSize;
    info.availableBytes = stats.availableBlocks * stats.fragmentSize;
    const std::uint64_t userVisible = info.usedBytes + info.availableBytes;
    info.usagePercent = userVisible == 0 ? 100.0 : 100.0 * double(info.usedBytes) / double(userVisible);
    return info;
}

std::optional<FilesystemStats> queryFilesystem(const std::string &path, std::string *error)
{
    struct statvfs vfs {};
    if (::statvfs(path.c_str(), &vfs) != 0) {
        if (error)
            *error = path + ": " + std::strerror(errno);
        return std::nullopt;
    }
    return FilesystemStats{vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize, vfs.f_blocks, vfs.f_bfree, vfs.f_bavail};
}

} // namespace aqtruncat::metrics
