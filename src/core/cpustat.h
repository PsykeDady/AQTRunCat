#pragma once

// Linux CPU utilisation from the aggregate "cpu" line of /proc/stat.
//
// Semantics (see docs/M1-cpu-runner.md):
//   active = user + nice + system + irq + softirq + steal
//   idle   = idle + iowait
//   total  = active + idle
// guest and guest_nice are ignored because the kernel already accounts them
// inside user and nice; adding them again would count that time twice.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aqtruncat {

// Cumulative jiffies of the aggregate "cpu" line. Fields missing on old
// kernels stay zero.
struct CpuTimes {
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t system = 0;
    std::uint64_t idle = 0;
    std::uint64_t iowait = 0;
    std::uint64_t irq = 0;
    std::uint64_t softirq = 0;
    std::uint64_t steal = 0;
    std::uint64_t guest = 0;
    std::uint64_t guestNice = 0;

    bool operator==(const CpuTimes &) const = default;
};

// Parses the aggregate "cpu" line out of /proc/stat content (the whole file
// or just that line). Requires at least user, nice, system and idle.
// On failure returns nullopt and, if given, describes the problem in *error.
std::optional<CpuTimes> parseProcStat(std::string_view text, std::string *error = nullptr);

// Number of per-CPU lines ("cpu0", "cpu1", …) in /proc/stat content: the
// logical CPUs the kernel accounts for. 0 if there are none.
int countLogicalCpus(std::string_view text);

// Busy percentage between two successive samples.
struct CpuDelta {
    double percent = 0.0;        // 0..100
    bool counterRegressed = false; // some field went backwards and was ignored
};

// Returns nullopt when the samples carry no information (no time elapsed).
// A field that went backwards (e.g. the known iowait regression, CPU
// hotplug) contributes zero instead of a huge unsigned difference.
std::optional<CpuDelta> cpuBusyBetween(const CpuTimes &previous, const CpuTimes &current);

// Keeps the previous sample and turns a stream of samples into percentages.
class CpuSampler
{
public:
    // nullopt for the first sample and for samples with no elapsed time.
    std::optional<CpuDelta> add(const CpuTimes &sample);
    void reset() { m_previous.reset(); }

private:
    std::optional<CpuTimes> m_previous;
};

} // namespace aqtruncat
