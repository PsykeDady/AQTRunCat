#pragma once

// Linux system metrics: one sampler reads every source once per sample and
// produces a typed, UI-independent snapshot. Consumers (CLI, Dashboard and
// Metrics Bar) only read snapshots. Values are raw (bytes, bytes/s,
// percent); formatting lives in metricsformat.h and in each frontend.

#include "core/cpustat.h"
#include "counterrates.h"
#include "diskio.h"
#include "fps.h"
#include "gpu.h"
#include "memory.h"
#include "metricstatus.h"
#include "network.h"
#include "packageupdates.h"
#include "storage.h"
#include "systeminfo.h"
#include "uptime.h"

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
    // F5. Uptime, GPU and FPS are read with every sample; kernel and
    // distribution only every InfoRefreshSeconds. The two update counts are
    // not sampled here: the backend's update checks fill them in.
    UptimeMetrics uptime;
    GpuMetrics gpu;
    FpsMetrics fps;
    KernelMetrics kernel;
    DistributionMetrics distribution;
    UpdatesMetrics packageUpdates;
    UpdatesMetrics flatpakUpdates;
};

// The real system's defaults for the F5 sources (from HOME, XDG_CONFIG_HOME,
// XDG_DATA_HOME and PATH).
FpsSources systemFpsSources();
std::vector<std::string> systemPciIdsPaths();

struct MetricsSources {
    std::string procRoot = "/proc";
    std::string sysRoot = "/sys";
    std::vector<std::string> storagePaths{"/"};
    NetworkSelection networkSelection;
    std::function<std::optional<FilesystemStats>(const std::string &path, std::string *error)> statFilesystem = queryFilesystem;
    // F5
    std::string osRoot;                 // prefix of /etc and /usr/lib ("" = the real root)
    std::function<std::optional<KernelInfo>(std::string *error)> kernel = queryKernel;
    std::vector<std::string> pciIdsPaths = systemPciIdsPaths();
    NvmlQuery nvml = systemNvml();
    std::string gpuPreference = AutoGpu;
    std::string fpsSource = FpsSourceMangoHud;
    FpsSources fpsSources = systemFpsSources();
};

// Kernel and distribution rarely change: re-read every ten minutes.
inline constexpr double InfoRefreshSeconds = 600.0;

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
    // F5 settings: which GPU single-GPU consumers follow, and the FPS source.
    void setGpuPreference(std::string preference) { m_sources.gpuPreference = std::move(preference); }
    void setFpsSource(std::string source) { m_sources.fpsSource = std::move(source); }
    // FPS alone, from the current source (no file access when it is off),
    // so that a source change shows without waiting for the next sample.
    FpsMetrics sampleFps() { return m_fps.sample(m_sources.fpsSource); }

    // Monotonic clock in seconds, for callers without their own.
    static double now();

private:
    CpuMetrics sampleCpu();
    MemoryMetrics sampleMemory() const;
    NetworkMetrics sampleNetwork(double time);
    DiskIoMetrics sampleDiskIo(double time);
    UptimeMetrics sampleUptime() const;

    MetricsSources m_sources;
    CpuSampler m_cpu;
    CounterRateTracker m_network;
    CounterRateTracker m_disk;
    GpuSampler m_gpu;
    MangoHudFpsReader m_fps;
    std::optional<double> m_infoTime;
    KernelMetrics m_kernel;
    DistributionMetrics m_distribution;
};

} // namespace aqtruncat::metrics
