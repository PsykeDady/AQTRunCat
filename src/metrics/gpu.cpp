#include "gpu.h"

#include "textparse.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>

namespace aqtruncat::metrics {

namespace {

namespace fs = std::filesystem;

// pci.ids is about 1.4 MiB; refuse anything far larger.
constexpr std::size_t MaxPciIdsBytes = 16 * 1024 * 1024;

std::optional<std::string> readTrimmed(const std::string &path)
{
    const auto text = readBoundedTextFile(path, 64 * 1024);
    if (!text)
        return std::nullopt;
    return std::string(trimView(*text));
}

std::optional<std::uint64_t> readUnsigned(const std::string &path)
{
    const auto text = readTrimmed(path);
    std::uint64_t value = 0;
    if (!text || !parseUnsigned(*text, value))
        return std::nullopt;
    return value;
}

std::optional<std::uint32_t> readHexId(const std::string &path)
{
    auto text = readTrimmed(path);
    if (!text)
        return std::nullopt;
    std::string_view view = *text;
    if (view.starts_with("0x") || view.starts_with("0X"))
        view.remove_prefix(2);
    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(view.data(), view.data() + view.size(), value, 16);
    if (ec != std::errc() || ptr != view.data() + view.size() || view.empty())
        return std::nullopt;
    return value;
}

bool exists(const std::string &path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

std::map<std::string, std::string> uevent(const std::string &path)
{
    std::map<std::string, std::string> values;
    if (const auto text = readBoundedTextFile(path, 64 * 1024)) {
        for (const std::string_view line : splitLines(*text)) {
            const std::size_t equals = line.find('=');
            if (equals != std::string_view::npos)
                values[std::string(line.substr(0, equals))] = std::string(trimView(line.substr(equals + 1)));
        }
    }
    return values;
}

std::string shortVendor(std::uint32_t vendor)
{
    switch (vendor) {
    case 0x1002: return "AMD";
    case 0x8086: return "Intel";
    case 0x10de: return "NVIDIA";
    case 0x1af4: return "Red Hat";
    case 0x15ad: return "VMware";
    case 0x1234: return "QEMU";
    case 0x80ee: return "VirtualBox";
    default: return {};
    }
}

std::string hex4(std::uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string text(4, '0');
    for (int i = 3; i >= 0; --i) {
        text[std::size_t(i)] = digits[value & 0xf];
        value >>= 4;
    }
    return text;
}

// Directories under parent, sorted by name.
std::vector<std::string> sortedEntries(const std::string &parent)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec))
        names.push_back(it->path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

bool isCardName(const std::string &name)
{
    if (!name.starts_with("card") || name.size() == 4)
        return false;
    return std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// The temperature sensor of the device's hwmon: the first label in
// `preferred`, else temp1. Millidegrees Celsius.
bool readHwmonTemperature(GpuDevice &gpu, const std::string &devicePath, std::initializer_list<const char *> preferred)
{
    for (const std::string &hwmon : sortedEntries(devicePath + "/hwmon")) {
        const std::string dir = devicePath + "/hwmon/" + hwmon;
        std::string chosen;
        std::string label;
        for (const char *wanted : preferred) {
            for (int i = 1; i <= 16 && chosen.empty(); ++i) {
                const std::string base = dir + "/temp" + std::to_string(i);
                if (readTrimmed(base + "_label").value_or(std::string()) == wanted && exists(base + "_input")) {
                    chosen = base + "_input";
                    label = wanted;
                }
            }
            if (!chosen.empty())
                break;
        }
        if (chosen.empty() && exists(dir + "/temp1_input")) {
            chosen = dir + "/temp1_input";
            label = readTrimmed(dir + "/temp1_label").value_or(std::string("temp1"));
        }
        if (chosen.empty())
            continue;
        const auto text = readTrimmed(chosen);
        std::int64_t milli = 0;
        if (text) {
            const auto [ptr, ec] = std::from_chars(text->data(), text->data() + text->size(), milli);
            if (ec == std::errc() && ptr == text->data() + text->size()) {
                gpu.temperatureCelsius = double(milli) / 1000.0;
                gpu.temperatureSensor = label;
                gpu.temperatureStatus = MetricStatus::okStatus();
                return true;
            }
        }
        gpu.temperatureStatus = MetricStatus::error(chosen + ": unreadable temperature");
        return true;
    }
    return false;
}

void markUnavailable(MetricStatus &status, const std::string &why)
{
    status = MetricStatus::unavailable(why);
}

// --- NVML (proprietary NVIDIA driver), loaded at run time ------------------

struct Nvml {
    using Device = void *;
    struct Utilization { unsigned int gpu; unsigned int memory; };
    struct Memory { unsigned long long total; unsigned long long free; unsigned long long used; };
    int (*init)() = nullptr;
    int (*byPciBusId)(const char *, Device *) = nullptr;
    int (*name)(Device, char *, unsigned int) = nullptr;
    int (*utilization)(Device, Utilization *) = nullptr;
    int (*memory)(Device, Memory *) = nullptr;
    int (*temperature)(Device, int, unsigned int *) = nullptr;
    std::string error;
    bool ready = false;
};

Nvml &loadNvml()
{
    static Nvml nvml;
    static std::once_flag once;
    std::call_once(once, [] {
        void *library = ::dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) {
            nvml.error = "NVML (libnvidia-ml.so.1, part of the NVIDIA driver) is not installed";
            return;
        }
        auto symbol = [library](const char *name) { return ::dlsym(library, name); };
        nvml.init = reinterpret_cast<int (*)()>(symbol("nvmlInit_v2"));
        nvml.byPciBusId = reinterpret_cast<int (*)(const char *, Nvml::Device *)>(symbol("nvmlDeviceGetHandleByPciBusId_v2"));
        nvml.name = reinterpret_cast<int (*)(Nvml::Device, char *, unsigned int)>(symbol("nvmlDeviceGetName"));
        nvml.utilization = reinterpret_cast<int (*)(Nvml::Device, Nvml::Utilization *)>(symbol("nvmlDeviceGetUtilizationRates"));
        nvml.memory = reinterpret_cast<int (*)(Nvml::Device, Nvml::Memory *)>(symbol("nvmlDeviceGetMemoryInfo"));
        nvml.temperature = reinterpret_cast<int (*)(Nvml::Device, int, unsigned int *)>(symbol("nvmlDeviceGetTemperature"));
        if (!nvml.init || !nvml.byPciBusId) {
            nvml.error = "NVML does not provide the expected functions";
            return;
        }
        const int result = nvml.init();
        if (result != 0) {
            nvml.error = "NVML initialisation failed (error " + std::to_string(result) + ")";
            return;
        }
        nvml.ready = true;
    });
    return nvml;
}

} // namespace

NvmlQuery systemNvml()
{
    return [](const std::string &slot, std::string *error) -> std::optional<NvmlReading> {
        Nvml &nvml = loadNvml();
        if (!nvml.ready) {
            if (error)
                *error = nvml.error;
            return std::nullopt;
        }
        // NVML's bus id has an 8-digit domain ("00000000:01:00.0").
        const std::string busId = slot.size() == 12 ? "0000" + slot : slot;
        Nvml::Device device = nullptr;
        if (nvml.byPciBusId(busId.c_str(), &device) != 0 || !device) {
            if (error)
                *error = "NVML does not know the device " + slot;
            return std::nullopt;
        }
        NvmlReading reading;
        char name[96] = {};
        if (nvml.name && nvml.name(device, name, sizeof name) == 0)
            reading.name = name;
        Nvml::Utilization utilization {};
        if (nvml.utilization && nvml.utilization(device, &utilization) == 0)
            reading.utilizationPercent = double(utilization.gpu);
        Nvml::Memory memory {};
        if (nvml.memory && nvml.memory(device, &memory) == 0) {
            reading.memoryUsedBytes = memory.used;
            reading.memoryTotalBytes = memory.total;
        }
        unsigned int temperature = 0;
        if (nvml.temperature && nvml.temperature(device, 0 /* NVML_TEMPERATURE_GPU */, &temperature) == 0)
            reading.temperatureCelsius = double(temperature);
        return reading;
    };
}

PciNames::PciNames(std::vector<std::string> paths)
    : m_paths(std::move(paths))
{
}

std::string PciNames::device(std::uint32_t vendor, std::uint32_t device)
{
    const std::uint64_t key = (std::uint64_t(vendor) << 32) | device;
    if (const auto it = m_cache.find(key); it != m_cache.end())
        return it->second;
    std::string name;
    const std::string vendorKey = hex4(vendor);
    const std::string deviceKey = hex4(device);
    for (const std::string &path : m_paths) {
        const auto text = readBoundedTextFile(path, MaxPciIdsBytes);
        if (!text)
            continue;
        bool inVendor = false;
        for (const std::string_view line : splitLines(*text)) {
            if (line.empty() || line.front() == '#')
                continue;
            if (line.front() != '\t') {
                if (inVendor)
                    break; // the next vendor: the device is not listed
                inVendor = line.starts_with(vendorKey) && line.size() > 4 && line[4] == ' ';
                continue;
            }
            if (inVendor && line.size() > 6 && line[1] != '\t' && line.substr(1, 4) == deviceKey && line[5] == ' ')
                name = std::string(trimView(line.substr(6)));
            if (!name.empty())
                break;
        }
        break; // the first readable database decides
    }
    m_cache[key] = name;
    return name;
}

GpuSampler::GpuSampler(std::string sysRoot, std::vector<std::string> pciIdsPaths, NvmlQuery nvml)
    : m_sysRoot(std::move(sysRoot))
    , m_names(std::move(pciIdsPaths))
    , m_nvml(std::move(nvml))
{
}

int selectGpu(std::vector<GpuDevice> &devices, const std::string &preference)
{
    int chosen = -1;
    if (!preference.empty() && preference != AutoGpu) {
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].id == preference)
                chosen = int(i);
        }
    }
    if (chosen < 0) {
        for (std::size_t i = 0; i < devices.size() && chosen < 0; ++i) {
            if (devices[i].integrated == false)
                chosen = int(i);
        }
    }
    if (chosen < 0 && !devices.empty())
        chosen = 0;
    for (std::size_t i = 0; i < devices.size(); ++i)
        devices[i].selected = int(i) == chosen;
    return chosen;
}

GpuMetrics GpuSampler::sample(double time, const std::string &preference)
{
    GpuMetrics result;
    const std::string drm = m_sysRoot + "/class/drm";
    std::vector<std::string> seen;
    for (const std::string &card : sortedEntries(drm)) {
        if (!isCardName(card))
            continue;
        const std::string cardPath = drm + "/" + card;
        const std::string devicePath = cardPath + "/device";
        if (!exists(devicePath))
            continue;
        const auto event = uevent(devicePath + "/uevent");
        GpuDevice gpu;
        gpu.card = card;
        gpu.driver = event.contains("DRIVER") ? event.at("DRIVER") : std::string();
        // Firmware framebuffers and virtual KMS are not GPUs.
        if (gpu.driver == "simpledrm" || gpu.driver == "simplefb" || gpu.driver == "efifb" || gpu.driver == "vkms")
            continue;
        if (const auto pciClass = readHexId(devicePath + "/class"); pciClass && (*pciClass >> 16) != 0x03)
            continue; // a PCI device that is not a display controller
        if (event.contains("PCI_SLOT_NAME")) {
            gpu.id = event.at("PCI_SLOT_NAME");
        } else {
            std::error_code ec;
            const fs::path target = fs::read_symlink(devicePath, ec);
            gpu.id = "platform:" + (ec ? card : target.filename().string());
        }
        if (std::find(seen.begin(), seen.end(), gpu.id) != seen.end())
            continue; // one device, several DRM cards
        seen.push_back(gpu.id);
        gpu.vendorId = readHexId(devicePath + "/vendor").value_or(0);
        gpu.deviceId = readHexId(devicePath + "/device").value_or(0);
        gpu.vendor = shortVendor(gpu.vendorId);
        gpu.bootVga = readUnsigned(devicePath + "/boot_vga").value_or(0) == 1;
        if (gpu.vendorId == 0x8086 && gpu.id.size() == 12 && gpu.id.substr(5, 2) == "00")
            gpu.integrated = true; // Intel graphics on the root bus is the processor's
        else if (gpu.vendorId == 0x8086 || gpu.vendorId == 0x10de)
            gpu.integrated = false;
        const std::string product = gpu.driver == "amdgpu" ? readTrimmed(devicePath + "/product_name").value_or(std::string())
                                                           : std::string();
        const std::string listed = gpu.vendorId ? m_names.device(gpu.vendorId, gpu.deviceId) : std::string();
        const std::string base = !product.empty() ? product : listed;
        if (!base.empty())
            gpu.model = gpu.vendor.empty() || base.starts_with(gpu.vendor) ? base : gpu.vendor + " " + base;
        else
            gpu.model = (gpu.vendor.empty() ? std::string("GPU") : gpu.vendor + " GPU") + " ["
                + hex4(gpu.vendorId) + ":" + hex4(gpu.deviceId) + "]";

        gpu.suspended = readTrimmed(devicePath + "/power/runtime_status").value_or(std::string()) == "suspended";
        if (gpu.suspended) {
            const std::string why = "the GPU is suspended (runtime power management); not woken up to read it";
            markUnavailable(gpu.utilizationStatus, why);
            markUnavailable(gpu.memoryStatus, why);
            markUnavailable(gpu.temperatureStatus, why);
            m_residency.erase(gpu.id);
        } else if (gpu.driver == "i915" || gpu.driver == "xe") {
            readIntel(gpu, cardPath, devicePath, time);
        } else if (gpu.driver == "amdgpu") {
            readAmd(gpu, devicePath);
        } else if (gpu.driver == "nvidia") {
            readNvidia(gpu);
        } else {
            const std::string why = "not reported by the " + (gpu.driver.empty() ? std::string("unknown") : gpu.driver) + " driver";
            markUnavailable(gpu.utilizationStatus, why);
            markUnavailable(gpu.memoryStatus, why);
            if (!readHwmonTemperature(gpu, devicePath, {"edge", "temp1"}))
                markUnavailable(gpu.temperatureStatus, why);
        }
        result.devices.push_back(std::move(gpu));
    }
    // Residency baselines of devices that disappeared.
    for (auto it = m_residency.begin(); it != m_residency.end();)
        it = std::find(seen.begin(), seen.end(), it->first) == seen.end() ? m_residency.erase(it) : std::next(it);

    std::sort(result.devices.begin(), result.devices.end(),
              [](const GpuDevice &a, const GpuDevice &b) { return a.id < b.id; });
    selectGpu(result.devices, preference);
    result.status = result.devices.empty() ? MetricStatus::unavailable("no GPU found") : MetricStatus::okStatus();
    return result;
}

void GpuSampler::readIntel(GpuDevice &gpu, const std::string &cardPath, const std::string &devicePath, double time)
{
    // Busy share = time outside the RC6 power-saving state (render GT).
    std::optional<std::uint64_t> idle;
    for (const std::string &path : {cardPath + "/gt/gt0/rc6_residency_ms", cardPath + "/power/rc6_residency_ms",
                                    devicePath + "/tile0/gt0/gtidle/idle_residency_ms"}) {
        if ((idle = readUnsigned(path)))
            break;
    }
    if (!idle) {
        markUnavailable(gpu.utilizationStatus, "the " + gpu.driver + " driver exposes no idle residency counter");
        m_residency.erase(gpu.id);
    } else {
        const auto previous = m_residency.find(gpu.id);
        if (previous == m_residency.end() || !(time > previous->second.time) || *idle < previous->second.idleMs) {
            gpu.utilizationStatus = MetricStatus::pending("GPU activity needs a second sample");
        } else {
            const double elapsedMs = (time - previous->second.time) * 1000.0;
            const double idleShare = double(*idle - previous->second.idleMs) / elapsedMs;
            gpu.utilizationPercent = std::clamp(100.0 * (1.0 - idleShare), 0.0, 100.0);
            gpu.utilizationMethod = "rc6-residency";
            gpu.utilizationStatus = MetricStatus::okStatus();
        }
        m_residency[gpu.id] = {time, *idle};
    }
    if (gpu.integrated == true)
        markUnavailable(gpu.memoryStatus, "integrated GPU: it uses shared system memory");
    else
        markUnavailable(gpu.memoryStatus, "not reported by the " + gpu.driver + " driver");
    if (!readHwmonTemperature(gpu, devicePath, {"pkg", "vram", "temp1"})) {
        markUnavailable(gpu.temperatureStatus, gpu.integrated == true
                            ? "integrated GPU: no sensor of its own (it is part of the CPU package)"
                            : "not reported by the " + gpu.driver + " driver");
    }
    for (const std::string &path : {cardPath + "/gt_act_freq_mhz", devicePath + "/tile0/gt0/freq0/act_freq"}) {
        if (const auto mhz = readUnsigned(path)) {
            gpu.frequencyMHz = double(*mhz);
            break;
        }
    }
}

void GpuSampler::readAmd(GpuDevice &gpu, const std::string &devicePath)
{
    if (const auto busy = readUnsigned(devicePath + "/gpu_busy_percent")) {
        gpu.utilizationPercent = double(std::min<std::uint64_t>(*busy, 100));
        gpu.utilizationMethod = "busy-percent";
        gpu.utilizationStatus = MetricStatus::okStatus();
    } else {
        markUnavailable(gpu.utilizationStatus, "not reported by the amdgpu driver");
    }
    const auto used = readUnsigned(devicePath + "/mem_info_vram_used");
    const auto total = readUnsigned(devicePath + "/mem_info_vram_total");
    if (used && total && *total > 0) {
        gpu.memoryUsedBytes = used;
        gpu.memoryTotalBytes = total;
        gpu.memoryStatus = MetricStatus::okStatus();
    } else {
        markUnavailable(gpu.memoryStatus, "not reported by the amdgpu driver");
    }
    if (!readHwmonTemperature(gpu, devicePath, {"edge", "junction"}))
        markUnavailable(gpu.temperatureStatus, "not reported by the amdgpu driver");
    // Current core clock: the line marked '*' of pp_dpm_sclk ("1: 1800Mhz *").
    if (const auto text = readBoundedTextFile(devicePath + "/pp_dpm_sclk", 64 * 1024)) {
        for (const std::string_view line : splitLines(*text)) {
            const auto fields = splitFields(line);
            if (fields.size() < 3 || fields.back() != "*")
                continue;
            std::string_view mhz = fields[1];
            const std::size_t unit = mhz.find_first_not_of("0123456789");
            std::uint64_t value = 0;
            if (unit != std::string_view::npos && parseUnsigned(mhz.substr(0, unit), value))
                gpu.frequencyMHz = double(value);
        }
    }
}

void GpuSampler::readNvidia(GpuDevice &gpu)
{
    std::string error;
    const auto reading = m_nvml ? m_nvml(gpu.id, &error) : std::nullopt;
    if (!reading) {
        const std::string why = error.empty() ? std::string("NVML is not available") : error;
        markUnavailable(gpu.utilizationStatus, why);
        markUnavailable(gpu.memoryStatus, why);
        markUnavailable(gpu.temperatureStatus, why);
        return;
    }
    if (!reading->name.empty())
        gpu.model = reading->name.starts_with("NVIDIA") ? reading->name : "NVIDIA " + reading->name;
    const std::string missing = "not reported by NVML for this device";
    if (reading->utilizationPercent) {
        gpu.utilizationPercent = std::clamp(*reading->utilizationPercent, 0.0, 100.0);
        gpu.utilizationMethod = "nvml";
        gpu.utilizationStatus = MetricStatus::okStatus();
    } else {
        markUnavailable(gpu.utilizationStatus, missing);
    }
    if (reading->memoryUsedBytes && reading->memoryTotalBytes && *reading->memoryTotalBytes > 0) {
        gpu.memoryUsedBytes = reading->memoryUsedBytes;
        gpu.memoryTotalBytes = reading->memoryTotalBytes;
        gpu.memoryStatus = MetricStatus::okStatus();
    } else {
        markUnavailable(gpu.memoryStatus, missing);
    }
    if (reading->temperatureCelsius) {
        gpu.temperatureCelsius = reading->temperatureCelsius;
        gpu.temperatureSensor = "nvml";
        gpu.temperatureStatus = MetricStatus::okStatus();
    } else {
        markUnavailable(gpu.temperatureStatus, missing);
    }
}

} // namespace aqtruncat::metrics
