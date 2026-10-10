#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using FilesystemIdLookup = std::function<std::optional<std::uint64_t>(const std::string &)>;

// Linux filesystem identity from stat(2)'s st_dev.
std::optional<std::uint64_t> filesystemId(const std::string &path);

// Root is always sampled. Home is added only when both paths can be resolved
// and stat(2) proves that it belongs to a different filesystem.
std::vector<std::string> desktopStoragePaths(const std::string &homePath,
                                             FilesystemIdLookup lookup = filesystemId);
