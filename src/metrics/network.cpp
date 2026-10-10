#include "network.h"

#include "textparse.h"

#include <system_error>

namespace aqtruncat::metrics {

std::optional<CounterSet> parseNetDev(std::string_view text, std::string *error)
{
    // "  wlan0: 5075793998 8453804 0 … 1526975316 6346400 …"
    // 8 receive fields, then 8 transmit fields; bytes are fields 0 and 8.
    CounterSet counters;
    for (const std::string_view line : splitLines(text)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue; // the two header lines
        const std::string_view name = trimView(line.substr(0, colon));
        if (name.empty() || name.find('|') != std::string_view::npos)
            continue;
        const std::vector<std::string_view> fields = splitFields(line.substr(colon + 1));
        std::uint64_t rx = 0, tx = 0;
        if (fields.size() < 16 || !parseUnsigned(fields[0], rx) || !parseUnsigned(fields[8], tx)) {
            if (error)
                *error = "malformed /proc/net/dev line for " + std::string(name);
            return std::nullopt;
        }
        counters[std::string(name)] = CounterPair{rx, tx};
    }
    return counters;
}

InterfaceKind classifyInterface(const std::string &name, const std::filesystem::path &sysClassNet)
{
    if (name == "lo")
        return InterfaceKind::Loopback;
    std::error_code ec;
    return std::filesystem::exists(sysClassNet / name / "device", ec) ? InterfaceKind::Physical : InterfaceKind::Virtual;
}

const char *interfaceKindName(InterfaceKind kind)
{
    switch (kind) {
    case InterfaceKind::Loopback:
        return "loopback";
    case InterfaceKind::Physical:
        return "physical";
    case InterfaceKind::Virtual:
        return "virtual";
    }
    return "unknown";
}

} // namespace aqtruncat::metrics
