#include "textparse.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace aqtruncat::metrics {

std::optional<std::string> readTextFile(const std::string &path, std::string *error, bool *missing)
{
    // /proc and /sys files report size 0: read until EOF.
    std::FILE *file = std::fopen(path.c_str(), "re");
    if (!file) {
        if (missing)
            *missing = errno == ENOENT;
        if (error)
            *error = path + ": " + std::strerror(errno);
        return std::nullopt;
    }
    std::string content;
    char buffer[8192];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
        content.append(buffer, n);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) {
        if (error)
            *error = path + ": read error";
        return std::nullopt;
    }
    return content;
}

} // namespace aqtruncat::metrics
