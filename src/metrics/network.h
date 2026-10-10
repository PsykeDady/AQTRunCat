#pragma once

// Network traffic from /proc/net/dev, interfaces classified through /sys.

#include "counterrates.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace aqtruncat::metrics {

// Parses /proc/net/dev: per interface, cumulative received (first) and
// transmitted (second) bytes.
std::optional<CounterSet> parseNetDev(std::string_view text, std::string *error = nullptr);

enum class InterfaceKind {
    Loopback, // lo
    Physical, // backed by a device: /sys/class/net/<name>/device exists (Ethernet, Wi-Fi, USB, WWAN)
    Virtual,  // everything else: VPN tunnels (tun, wg, ppp), bridges, docker, veth, …
};

enum class NetworkPolicyMode {
    Automatic,
    Manual,
};

// Automatic preserves the M3 hardware-backed policy. Manual counts exactly
// the named interfaces, including loopback/virtual ones when explicitly
// selected. Missing names remain configured and simply do not contribute.
struct NetworkSelection {
    NetworkPolicyMode mode = NetworkPolicyMode::Automatic;
    std::set<std::string> interfaces;

    bool operator==(const NetworkSelection &) const = default;
};

// Classifies an interface without assuming names ("eth0", "wlan0"…).
InterfaceKind classifyInterface(const std::string &name, const std::filesystem::path &sysClassNet);

// Aggregation policy: only Physical interfaces are counted. Traffic that goes
// through a VPN or a container crosses a physical interface too; counting the
// virtual interface as well would count it twice.
inline bool countsTowardsNetworkTotal(InterfaceKind kind)
{
    return kind == InterfaceKind::Physical;
}

const char *interfaceKindName(InterfaceKind kind);

} // namespace aqtruncat::metrics
