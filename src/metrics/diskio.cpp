#include "diskio.h"

#include "textparse.h"

#include <system_error>

namespace aqtruncat::metrics {

std::optional<CounterSet> parseDiskstats(std::string_view text, std::string *error)
{
    // major minor name reads merged sectors_read ms writes merged sectors_written ms …
    CounterSet counters;
    for (const std::string_view line : splitLines(text)) {
        const std::vector<std::string_view> fields = splitFields(line);
        if (fields.empty())
            continue;
        std::uint64_t read = 0, written = 0;
        if (fields.size() < 10 || !parseUnsigned(fields[5], read) || !parseUnsigned(fields[9], written)) {
            if (error)
                *error = "malformed /proc/diskstats line: \"" + std::string(line) + "\"";
            return std::nullopt;
        }
        counters[std::string(fields[2])] = CounterPair{read * DiskstatsSectorBytes, written * DiskstatsSectorBytes};
    }
    return counters;
}

BlockDeviceKind classifyBlockDevice(const std::string &name, const std::filesystem::path &sysBlock)
{
    // Partitions are not listed directly under /sys/block, so they fail here too.
    std::error_code ec;
    const std::filesystem::path dir = sysBlock / name;
    if (!std::filesystem::exists(dir / "device", ec))
        return BlockDeviceKind::Other;
    if (const auto hidden = readTextFile((dir / "hidden").string()); hidden && trimView(*hidden) == "1")
        return BlockDeviceKind::Other; // e.g. per-path NVMe multipath devices
    return BlockDeviceKind::PhysicalDisk;
}

} // namespace aqtruncat::metrics
