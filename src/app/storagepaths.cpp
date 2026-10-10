#include "storagepaths.h"

#include <sys/stat.h>

std::optional<std::uint64_t> filesystemId(const std::string &path)
{
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(info.st_dev);
}

std::vector<std::string> desktopStoragePaths(const std::string &homePath, FilesystemIdLookup lookup)
{
    std::vector<std::string> paths{"/"};
    if (homePath.empty())
        return paths;
    const auto root = lookup("/");
    const auto home = lookup(homePath);
    if (root && home && *root != *home)
        paths.push_back(homePath);
    return paths;
}
