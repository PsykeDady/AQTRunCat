#include "systemmetrics.h"

#include "textparse.h"

#include <chrono>
#include <cstdlib>
#include <map>

namespace aqtruncat::metrics {

namespace {

// Unavailable when the file does not exist here, Error when it exists but
// cannot be read.
MetricStatus readFailure(const std::string &path, bool missing, const std::string &error)
{
    return missing ? MetricStatus::unavailable(path + " not available") : MetricStatus::error(error);
}

std::string environment(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

UpdatesMetrics waitingForCheck()
{
    UpdatesMetrics updates;
    updates.status = MetricStatus::pending("waiting for the first update check");
    return updates;
}

} // namespace

FpsSources systemFpsSources()
{
    FpsSources sources;
    sources.home = environment("HOME");
    const std::string config = environment("XDG_CONFIG_HOME");
    sources.configDir = (config.empty() || config.front() != '/' ? sources.home + "/.config" : config) + "/MangoHud";
    const std::string data = environment("XDG_DATA_HOME");
    sources.layerDirs = {"/usr/share/vulkan/implicit_layer.d", "/usr/local/share/vulkan/implicit_layer.d",
                         "/etc/vulkan/implicit_layer.d",
                         (data.empty() || data.front() != '/' ? sources.home + "/.local/share" : data)
                             + "/vulkan/implicit_layer.d"};
    const std::string path = environment("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t colon = path.find(':', start);
        const std::string entry = path.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!entry.empty() && entry.front() == '/')
            sources.binaryDirs.push_back(entry);
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    sources.wallClock = [] {
        using namespace std::chrono;
        return duration<double>(system_clock::now().time_since_epoch()).count();
    };
    return sources;
}

std::vector<std::string> systemPciIdsPaths()
{
    return {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids", "/usr/share/pci.ids"};
}

SystemMetricsSampler::SystemMetricsSampler(MetricsSources sources)
    : m_sources(std::move(sources))
    , m_gpu(m_sources.sysRoot, m_sources.pciIdsPaths, m_sources.nvml)
    , m_fps(m_sources.fpsSources)
{
}

double SystemMetricsSampler::now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

SystemMetricsSnapshot SystemMetricsSampler::sample(double monotonicSeconds)
{
    SystemMetricsSnapshot snapshot;
    snapshot.timeSeconds = monotonicSeconds;
    snapshot.cpu = sampleCpu();
    snapshot.memory = sampleMemory();
    snapshot.network = sampleNetwork(monotonicSeconds);
    for (const std::string &path : m_sources.storagePaths) {
        StorageMetrics storage;
        storage.path = path;
        std::string error;
        if (const auto stats = m_sources.statFilesystem(path, &error)) {
            storage.info = computeStorage(path, *stats, &error);
            storage.status = storage.info ? MetricStatus::okStatus() : MetricStatus::error(error);
        } else {
            storage.status = MetricStatus::unavailable(error);
        }
        snapshot.storage.push_back(std::move(storage));
    }
    snapshot.diskIo = sampleDiskIo(monotonicSeconds);
    snapshot.uptime = sampleUptime();
    snapshot.gpu = m_gpu.sample(monotonicSeconds, m_sources.gpuPreference);
    snapshot.fps = sampleFps();
    if (!m_infoTime || monotonicSeconds - *m_infoTime >= InfoRefreshSeconds || monotonicSeconds < *m_infoTime) {
        m_infoTime = monotonicSeconds;
        std::string error;
        m_kernel.info = m_sources.kernel ? m_sources.kernel(&error) : std::nullopt;
        m_kernel.status = m_kernel.info ? MetricStatus::okStatus()
                                        : MetricStatus::unavailable(error.empty() ? "kernel information unavailable" : error);
        m_distribution = readDistribution(m_sources.osRoot);
    }
    snapshot.kernel = m_kernel;
    snapshot.distribution = m_distribution;
    snapshot.packageUpdates = waitingForCheck();
    snapshot.flatpakUpdates = waitingForCheck();
    return snapshot;
}

UptimeMetrics SystemMetricsSampler::sampleUptime() const
{
    UptimeMetrics uptime;
    const std::string path = m_sources.procRoot + "/uptime";
    std::string error;
    bool missing = false;
    const auto text = readTextFile(path, &error, &missing);
    if (!text) {
        uptime.status = readFailure(path, missing, error);
        return uptime;
    }
    uptime.seconds = parseProcUptime(*text, &error);
    uptime.status = uptime.seconds ? MetricStatus::okStatus() : MetricStatus::error(path + ": " + error);
    return uptime;
}

void SystemMetricsSampler::setNetworkSelection(NetworkSelection selection)
{
    if (selection == m_sources.networkSelection)
        return;
    m_sources.networkSelection = std::move(selection);
    m_network.reset();
}

CpuMetrics SystemMetricsSampler::sampleCpu()
{
    CpuMetrics cpu;
    const std::string path = m_sources.procRoot + "/stat";
    std::string error;
    bool missing = false;
    const auto text = readTextFile(path, &error, &missing);
    if (!text) {
        cpu.status = readFailure(path, missing, error);
        return cpu;
    }
    cpu.logicalCpus = countLogicalCpus(*text);
    const auto times = parseProcStat(*text, &error);
    if (!times) {
        cpu.status = MetricStatus::error(path + ": " + error);
        return cpu;
    }
    // The runner's calculation (core/cpustat): one implementation for both.
    if (const auto delta = m_cpu.add(*times)) {
        cpu.usagePercent = delta->percent;
        cpu.status = MetricStatus::okStatus();
    } else {
        cpu.status = MetricStatus::pending("CPU usage needs a second sample");
    }
    return cpu;
}

MemoryMetrics SystemMetricsSampler::sampleMemory() const
{
    MemoryMetrics memory;
    const std::string path = m_sources.procRoot + "/meminfo";
    std::string error;
    bool missing = false;
    const auto text = readTextFile(path, &error, &missing);
    if (!text) {
        memory.status = readFailure(path, missing, error);
        return memory;
    }
    memory.info = parseMeminfo(*text, &error);
    memory.status = memory.info ? MetricStatus::okStatus() : MetricStatus::error(path + ": " + error);
    return memory;
}

NetworkMetrics SystemMetricsSampler::sampleNetwork(double time)
{
    NetworkMetrics network;
    const std::string path = m_sources.procRoot + "/net/dev";
    std::string error;
    bool missing = false;
    const auto text = readTextFile(path, &error, &missing);
    if (!text) {
        network.status = readFailure(path, missing, error);
        return network;
    }
    const auto counters = parseNetDev(*text, &error);
    if (!counters) {
        network.status = MetricStatus::error(path + ": " + error);
        return network;
    }
    const std::string sysClassNet = m_sources.sysRoot + "/class/net";
    std::map<std::string, InterfaceKind> kinds;
    for (const auto &[name, unused] : *counters)
        kinds[name] = classifyInterface(name, sysClassNet);
    const AggregateRates rates = m_network.update(*counters, time, [this, &kinds](const std::string &name) {
        if (m_sources.networkSelection.mode == NetworkPolicyMode::Manual)
            return m_sources.networkSelection.interfaces.contains(name);
        return countsTowardsNetworkTotal(kinds.at(name));
    });

    network.status = rates.status;
    if (network.status.state == MetricState::Unavailable)
        network.status.message = m_sources.networkSelection.mode == NetworkPolicyMode::Manual
            ? "no selected network interface is present"
            : "no physical network interface";
    network.downloadBytesPerSecond = rates.firstPerSecond;
    network.uploadBytesPerSecond = rates.secondPerSecond;
    network.countedInterfaces = rates.countedSources;
    for (const SourceRate &source : rates.sources)
        network.interfaces.push_back({source.name, kinds.at(source.name), source.counted, source.firstPerSecond,
                                      source.secondPerSecond, source.counterReset});
    return network;
}

DiskIoMetrics SystemMetricsSampler::sampleDiskIo(double time)
{
    DiskIoMetrics disk;
    const std::string path = m_sources.procRoot + "/diskstats";
    std::string error;
    bool missing = false;
    const auto text = readTextFile(path, &error, &missing);
    if (!text) {
        disk.status = readFailure(path, missing, error);
        return disk;
    }
    const auto counters = parseDiskstats(*text, &error);
    if (!counters) {
        disk.status = MetricStatus::error(path + ": " + error);
        return disk;
    }
    const std::string sysBlock = m_sources.sysRoot + "/block";
    std::map<std::string, BlockDeviceKind> kinds;
    for (const auto &[name, unused] : *counters)
        kinds[name] = classifyBlockDevice(name, sysBlock);
    const AggregateRates rates = m_disk.update(*counters, time, [&kinds](const std::string &name) {
        return countsTowardsDiskTotal(kinds.at(name));
    });

    disk.status = rates.status;
    if (disk.status.state == MetricState::Unavailable)
        disk.status.message = "no physical disk";
    disk.readBytesPerSecond = rates.firstPerSecond;
    disk.writeBytesPerSecond = rates.secondPerSecond;
    disk.countedDevices = rates.countedSources;
    for (const SourceRate &source : rates.sources)
        disk.devices.push_back({source.name, kinds.at(source.name), source.counted, source.firstPerSecond,
                                source.secondPerSecond, source.counterReset});
    return disk;
}

} // namespace aqtruncat::metrics
