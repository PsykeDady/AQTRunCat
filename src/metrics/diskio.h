#pragma once

// Disk activity (I/O) from /proc/diskstats, devices classified through /sys.
// Not to be confused with storage usage (filesystem capacity, storage.h).

#include "counterrates.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aqtruncat::metrics {

// /proc/diskstats always counts 512-byte sectors, whatever the device's
// logical block size (see the kernel's Documentation/admin-guide/iostats).
inline constexpr std::uint64_t DiskstatsSectorBytes = 512;

// Parses /proc/diskstats: per device, cumulative bytes read (first) and
// written (second).
std::optional<CounterSet> parseDiskstats(std::string_view text, std::string *error = nullptr);

enum class BlockDeviceKind {
    PhysicalDisk, // /sys/block/<name>/device exists and the disk is not hidden
    Other,        // partitions, device-mapper (LVM, LUKS), md RAID, loop, zram, hidden NVMe paths, …
};

BlockDeviceKind classifyBlockDevice(const std::string &name, const std::filesystem::path &sysBlock);

// Aggregation policy: only whole physical disks. Every byte that reaches
// storage hardware is counted once, at the disk; partitions and stacked
// virtual devices (dm-crypt, LVM, RAID) would count the same I/O again, and
// zram/loop are not disk hardware.
inline bool countsTowardsDiskTotal(BlockDeviceKind kind)
{
    return kind == BlockDeviceKind::PhysicalDisk;
}

} // namespace aqtruncat::metrics
