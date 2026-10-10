#pragma once

// Linux system metrics: one sampler reads every source once per sample and
// produces a typed, UI-independent snapshot. Consumers (CLI, Dashboard and
// Metrics Bar) only read snapshots. Values are raw (bytes, bytes/s,
// percent); formatting lives in metricsformat.h and in each frontend.

#include "core/cpustat.h"
#include "counterrates.h"
#include "diskio.h"
#include "memory.h"
#include "metricstatus.h"
#include "network.h"
#include "storage.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aqtruncat::metrics {

struct CpuMetrics {
    MetricStatus status;
    std::optional<double> usagePercent; // same semantics as the runner (core/cpustat.h), not smoothed
    int logicalCpus = 0;                // 0 if unknown
};

struct MemoryMetrics {
    MetricStatus status;
    std::optional<MemoryInfo> info;
};

struct InterfaceMetrics {
    std::string name;
    InterfaceKind kind = InterfaceKind::Virtual;
    bool counted = false;                       // part of the aggregate
    std::optional<double> downloadBytesPerSecond;
    std::optional<double> uploadBytesPerSecond;
    bool counterReset = false;
};

struct NetworkMetrics {
    MetricStatus status;
    std::optional<double> downloadBytesPerSecond; // sum over counted (physical) interfaces
    std::optional<double> uploadBytesPerSecond;
    int countedInterfaces = 0;
    std::vector<InterfaceMetrics> interfaces;     // every interface, for a future selection UI
};

struct StorageMetrics {
    MetricStatus status;
    std::string path;
    std::optional<StorageInfo> info;
};

struct DiskDeviceMetrics {
    std::string name;
    BlockDeviceKind kind = BlockDeviceKind::Other;
    bool counted = false;
    std::optional<double> readBytesPerSecond;
    std::optional<double> writeBytesPerSecond;
    bool counterReset = false;
};

struct DiskIoMetrics {
    MetricStatus status;
    std::optional<double> readBytesPerSecond;  // sum over counted (whole physical) disks
    std::optional<double> writeBytesPerSecond;
    int countedDevices = 0;
    std::vector<DiskDeviceMetrics> devices;
};

struct SystemMetricsSnapshot {
    double timeSeconds = 0.0; // monotonic time of the sample
    CpuMetrics cpu;
    MemoryMetrics memory;
    NetworkMetrics network;
    std::vector<StorageMetrics> storage; // one per configured path
    DiskIoMetrics diskIo;
};

struct MetricsSources {
    std::string procRoot = "/proc";
    std::string sysRoot = "/sys";
    std::vector<std::string> storagePaths{"/"};
    NetworkSelection networkSelection;
    std::function<std::optional<FilesystemStats>(const std::string &path, std::string *error)> statFilesystem = queryFilesystem;
};

class SystemMetricsSampler
{
public:
    explicit SystemMetricsSampler(MetricsSources sources = {});

    // Reads every source once. Rates (CPU, network, disk) are relative to the
    // previous call, so the first snapshot has them Pending.
    SystemMetricsSnapshot sample(double monotonicSeconds);

    // A policy transition starts a new network baseline. CPU, disk and other
    // sections keep their independent state.
    void setNetworkSelection(NetworkSelection selection);
    const NetworkSelection &networkSelection() const { return m_sources.networkSelection; }

    // Monotonic clock in seconds, for callers without their own.
    static double now();

private:
    CpuMetrics sampleCpu();
    MemoryMetrics sampleMemory() const;
    NetworkMetrics sampleNetwork(double time);
    DiskIoMetrics sampleDiskIo(double time);

    MetricsSources m_sources;
    CpuSampler m_cpu;
    CounterRateTracker m_network;
    CounterRateTracker m_disk;
};

} // namespace aqtruncat::metrics
