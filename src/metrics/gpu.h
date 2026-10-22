#pragma once

// F5.2 GPU discovery and telemetry. Devices come from DRM (/sys/class/drm/
// card<N>, one entry per device, deterministic order by id); telemetry from
// the driver's documented sysfs files, or NVML for the proprietary NVIDIA
// driver (loaded at run time, never a build or runtime dependency). Every
// field has its own availability: a missing value is never 0. A GPU in
// runtime suspend is not read at all, so that monitoring never wakes it.

#include "metricstatus.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aqtruncat::metrics {

struct GpuDevice {
    std::string id;     // PCI slot ("0000:00:02.0"); other buses: "<bus>:<device name>"
    std::string card;   // DRM node ("card1")
    std::string driver; // kernel driver ("i915", "amdgpu", "nvidia", …)
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string vendor; // short vendor name ("Intel", "AMD", "NVIDIA")
    std::string model;  // from the driver, NVML or pci.ids; never empty
    std::optional<bool> integrated; // unknown when the bus does not say
    bool bootVga = false;
    bool suspended = false; // runtime PM "suspended": telemetry not read
    bool selected = false;  // the GPU single-GPU consumers (Metrics Bar) follow

    MetricStatus utilizationStatus;
    std::optional<double> utilizationPercent;
    // "busy-percent" (amdgpu), "rc6-residency" (Intel: time out of the RC6
    // power-saving state), "nvml"; empty when there is no value.
    std::string utilizationMethod;

    MetricStatus memoryStatus;
    std::optional<std::uint64_t> memoryUsedBytes;
    std::optional<std::uint64_t> memoryTotalBytes;

    MetricStatus temperatureStatus;
    std::optional<double> temperatureCelsius;
    std::string temperatureSensor; // hwmon label ("edge") or "nvml"

    std::optional<double> frequencyMHz; // current core clock when the driver reports it
};

struct GpuMetrics {
    MetricStatus status; // Unavailable "no GPU found" when there is none
    std::vector<GpuDevice> devices;
};

// What NVML reports for one device; each value optional.
struct NvmlReading {
    std::string name;
    std::optional<double> utilizationPercent;
    std::optional<std::uint64_t> memoryUsedBytes;
    std::optional<std::uint64_t> memoryTotalBytes;
    std::optional<double> temperatureCelsius;
};

// Reads one NVIDIA device by PCI slot; nullopt with a reason when NVML is
// not available or does not know the device. Injectable for tests.
using NvmlQuery = std::function<std::optional<NvmlReading>(const std::string &pciSlot, std::string *error)>;

// The system's NVML through dlopen("libnvidia-ml.so.1"), initialised on the
// first call and kept loaded.
NvmlQuery systemNvml();

// GPU selection preference: "auto" (or empty) = the first discrete GPU,
// else the first GPU; otherwise a device id, falling back to auto when that
// device is absent.
inline constexpr const char *AutoGpu = "auto";

// Device and vendor names from a pci.ids database (hwdata); lookups are
// cached, the file is read only when a new id appears.
class PciNames
{
public:
    explicit PciNames(std::vector<std::string> paths);
    std::string device(std::uint32_t vendor, std::uint32_t device);

private:
    std::vector<std::string> m_paths;
    std::map<std::uint64_t, std::string> m_cache;
};

class GpuSampler
{
public:
    GpuSampler(std::string sysRoot, std::vector<std::string> pciIdsPaths, NvmlQuery nvml);

    GpuMetrics sample(double monotonicSeconds, const std::string &preference);

private:
    struct Residency {
        double time = 0.0;
        std::uint64_t idleMs = 0;
    };
    void readIntel(GpuDevice &gpu, const std::string &cardPath, const std::string &devicePath, double time);
    void readAmd(GpuDevice &gpu, const std::string &devicePath);
    void readNvidia(GpuDevice &gpu);

    std::string m_sysRoot;
    PciNames m_names;
    NvmlQuery m_nvml;
    std::map<std::string, Residency> m_residency; // per device id
};

// Marks the preferred device (see AutoGpu) as selected; returns its index or -1.
int selectGpu(std::vector<GpuDevice> &devices, const std::string &preference);

} // namespace aqtruncat::metrics
