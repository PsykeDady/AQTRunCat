#include "systemmetrics.h"

#include "textparse.h"

#include <chrono>
#include <map>

namespace aqtruncat::metrics {

namespace {

// Unavailable when the file does not exist here, Error when it exists but
// cannot be read.
MetricStatus readFailure(const std::string &path, bool missing, const std::string &error)
{
    return missing ? MetricStatus::unavailable(path + " not available") : MetricStatus::error(error);
}

} // namespace

SystemMetricsSampler::SystemMetricsSampler(MetricsSources sources)
    : m_sources(std::move(sources))
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
    return snapshot;
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
