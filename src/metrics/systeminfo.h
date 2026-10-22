#pragma once

// F5.6 kernel and distribution information: two separate, informational
// sections (text, never percentages). Kernel values come from uname(2);
// the distribution from os-release(5), parsed as data and never executed,
// with lsb-release and distribution-specific release files as fallbacks.
// Anything unknown stays unknown (empty / Unavailable), never a failure.

#include "metricstatus.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aqtruncat::metrics {

struct KernelInfo {
    std::string sysname; // "Linux"
    std::string release; // "7.2.10-zen1-1-zen"
    std::string version; // "#1 ZEN SMP PREEMPT_DYNAMIC …"
    std::string machine; // "x86_64"
};

struct KernelMetrics {
    MetricStatus status;
    std::optional<KernelInfo> info;
};

struct DistributionInfo {
    std::string name;       // NAME ("Arch Linux")
    std::string prettyName; // PRETTY_NAME, else NAME + VERSION
    std::string id;         // ID ("arch")
    std::vector<std::string> idLike; // ID_LIKE, space separated
    std::string versionId;  // VERSION_ID ("44")
    std::string version;    // VERSION ("44 (Workstation Edition)")
    std::string codename;   // VERSION_CODENAME
    std::string buildId;    // BUILD_ID ("rolling")
    std::string variant;    // VARIANT
    std::string source;     // the file the values come from
};

struct DistributionMetrics {
    MetricStatus status;
    std::optional<DistributionInfo> info;
};

// KEY=VALUE assignments of os-release(5)/lsb-release, as data: optional
// single or double quotes, backslash escapes inside double quotes ("\"",
// "\\", "\$", "\`"), '#' comments. Malformed lines (no '=', invalid key,
// unterminated quote, unquoted spaces) are skipped and counted.
std::map<std::string, std::string> parseOsRelease(std::string_view text, int *malformedLines = nullptr);

// Kernel identity from uname(2); nullopt with a reason on failure.
std::optional<KernelInfo> queryKernel(std::string *error);

// The distribution below root ("" = the real "/"): /etc/os-release, then
// /usr/lib/os-release, then /etc/lsb-release, then /etc/arch-release,
// /etc/fedora-release, /etc/redhat-release, /etc/debian_version.
DistributionMetrics readDistribution(const std::string &root);

} // namespace aqtruncat::metrics
