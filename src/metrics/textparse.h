#pragma once

// Small helpers for /proc and /sys text. Internal to the metrics library.

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aqtruncat::metrics {

inline std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        lines.push_back(text.substr(pos, end - pos));
        pos = end + 1;
    }
    return lines;
}

inline std::vector<std::string_view> splitFields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t pos = 0;
    while (pos < line.size()) {
        const std::size_t start = line.find_first_not_of(" \t\r", pos);
        if (start == std::string_view::npos)
            break;
        std::size_t end = line.find_first_of(" \t\r", start);
        if (end == std::string_view::npos)
            end = line.size();
        fields.push_back(line.substr(start, end - start));
        pos = end;
    }
    return fields;
}

// Strips spaces, tabs and line breaks (sysfs values end with '\n').
inline std::string_view trimView(std::string_view s)
{
    const std::size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos)
        return {};
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

inline bool parseUnsigned(std::string_view text, std::uint64_t &value)
{
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && ptr == text.data() + text.size() && !text.empty();
}

// Whole file as a string; nullopt if it cannot be opened. errno-style reason in *error.
std::optional<std::string> readTextFile(const std::string &path, std::string *error = nullptr, bool *missing = nullptr);

// As readTextFile, but a file longer than maxBytes is an error ("too large"):
// for files that other programs write (os-release, logs), never read whole.
std::optional<std::string> readBoundedTextFile(const std::string &path, std::size_t maxBytes,
                                               std::string *error = nullptr, bool *missing = nullptr);

} // namespace aqtruncat::metrics
