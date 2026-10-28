#include "cputemperature.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace aqtruncat::metrics {
namespace {

constexpr double RediscoverySeconds = 300.0;
constexpr double MinimumCelsius = -40.0;
constexpr double MaximumCelsius = 150.0;

std::string readLine(const std::filesystem::path &path)
{
    std::ifstream file(path);
    std::string value;
    if (!file || !std::getline(file, value))
        return {};
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    value.erase(value.begin(), first);
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return value;
}

bool cpuDriver(const std::string &name)
{
    const std::string driver = lower(name);
    return driver == "coretemp" || driver == "k10temp" || driver == "zenpower" || driver == "cpu_thermal";
}

std::optional<double> readTemperature(const std::filesystem::path &path)
{
    const std::string text = readLine(path);
    if (text.empty())
        return std::nullopt;
    std::istringstream input(text);
    long long milli = 0;
    input >> milli;
    if (!input || input.peek() != std::char_traits<char>::eof())
        return std::nullopt;
    const double celsius = static_cast<double>(milli) / 1000.0;
    if (!std::isfinite(celsius) || celsius < MinimumCelsius || celsius > MaximumCelsius)
        return std::nullopt;
    return celsius;
}

int channelPriority(const std::string &driver, const std::string &label, int channel)
{
    const std::string name = lower(label);
    if (name.find("package") != std::string::npos || name.find("pkg") != std::string::npos)
        return 0;
    if (name == "tdie" || name.find("die temperature") != std::string::npos)
        return 1;
    if (name == "tctl")
        return 2;
    if (name == "cpu" || name.find("cpu temperature") != std::string::npos)
        return 3;
    if (name.find("core") != std::string::npos)
        return 4;

    // The channel order is part of these CPU drivers' hwmon ABI when labels
    // are absent; no other driver's unlabeled channels are considered.
    if (driver == "coretemp" && channel == 1)
        return 0;
    if ((driver == "k10temp" || driver == "zenpower") && channel == 2)
        return 1;
    if ((driver == "k10temp" || driver == "zenpower") && channel == 1)
        return 2;
    if (driver == "cpu_thermal" && channel == 1)
        return 3;
    return std::numeric_limits<int>::max();
}

std::string stableDeviceKey(const std::filesystem::path &device, const std::string &fallback)
{
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(device, error);
    return error ? fallback : canonical.string();
}

std::vector<CpuTemperatureSampler::Sensor> thermalFallbacks(const std::filesystem::path &thermal)
{
    std::error_code error;
    std::vector<CpuTemperatureSampler::Sensor> candidates;
    if (!std::filesystem::is_directory(thermal, error))
        return candidates;
    for (std::filesystem::directory_iterator it(thermal, error), end; !error && it != end; it.increment(error)) {
        const std::string zone = it->path().filename().string();
        if (zone.rfind("thermal_zone", 0) != 0)
            continue;
        const std::string type = lower(readLine(it->path() / "type"));
        int rank = std::numeric_limits<int>::max();
        if (type == "x86_pkg_temp")
            rank = 0;
        else if (type == "cpu-thermal" || type == "cpu_thermal" || type == "cpu-temp" || type == "cpu_temperature")
            rank = 1;
        if (rank == std::numeric_limits<int>::max())
            continue;
        candidates.push_back({(it->path() / "temp").string(), "thermal:" + type + ":" + zone, type, rank + 10});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return std::tie(a.priority, a.key) < std::tie(b.priority, b.key);
    });
    return candidates;
}

} // namespace

CpuTemperatureSampler::CpuTemperatureSampler(std::string sysRoot) : m_sysRoot(std::move(sysRoot)) {}

std::vector<CpuTemperatureSampler::Sensor> CpuTemperatureSampler::discover() const
{
    namespace fs = std::filesystem;
    const fs::path hwmon = fs::path(m_sysRoot) / "class/hwmon";
    std::error_code error;
    std::vector<Sensor> result;
    if (fs::is_directory(hwmon, error)) {
        for (fs::directory_iterator it(hwmon, error), end; !error && it != end; it.increment(error)) {
            const fs::path directory = it->path();
            const std::string driver = lower(readLine(directory / "name"));
            if (!cpuDriver(driver))
                continue;
            const std::string deviceKey = stableDeviceKey(directory / "device", directory.filename().string());
            std::error_code childError;
            for (fs::directory_iterator child(directory, childError), childEnd; !childError && child != childEnd;
                 child.increment(childError)) {
                const std::string filename = child->path().filename().string();
                if (filename.rfind("temp", 0) != 0 || filename.size() < 11 || filename.substr(filename.size() - 6) != "_input")
                    continue;
                const std::string digits = filename.substr(4, filename.size() - 10);
                if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); }))
                    continue;
                int channel = 0;
                const auto [end, parseError] = std::from_chars(digits.data(), digits.data() + digits.size(), channel);
                if (parseError != std::errc() || end != digits.data() + digits.size())
                    continue;
                const std::string label = readLine(directory / ("temp" + digits + "_label"));
                const int priority = channelPriority(driver, label, channel);
                if (priority == std::numeric_limits<int>::max())
                    continue;
                const std::string identity = label.empty() ? ("temp" + digits) : lower(label);
                result.push_back({child->path().string(), "hwmon:" + deviceKey + ":" + driver + ":" + identity,
                                  label.empty() ? driver : label, priority});
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const Sensor &a, const Sensor &b) {
        return std::tie(a.priority, a.key) < std::tie(b.priority, b.key);
    });
    const auto thermal = thermalFallbacks(fs::path(m_sysRoot) / "class/thermal");
    result.insert(result.end(), thermal.cbegin(), thermal.cend());
    std::sort(result.begin(), result.end(), [](const Sensor &a, const Sensor &b) {
        return std::tie(a.priority, a.key) < std::tie(b.priority, b.key);
    });
    return result;
}

CpuTemperatureMetrics CpuTemperatureSampler::sample(double monotonicSeconds)
{
    const bool rediscover = !m_hasDiscovered || monotonicSeconds < m_lastDiscovery
        || monotonicSeconds - m_lastDiscovery >= RediscoverySeconds;
    if (rediscover) {
        m_candidates = discover();
        if (m_selected) {
            const auto same = std::find_if(m_candidates.begin(), m_candidates.end(), [&](const Sensor &s) { return s.key == m_selected->key; });
            if (same != m_candidates.end() && m_candidates.front().priority >= same->priority)
                m_selected = *same;
            else
                m_selected = m_candidates.empty() ? std::nullopt : std::optional<Sensor>(m_candidates.front());
        } else if (!m_candidates.empty()) {
            m_selected = m_candidates.front();
        }
        m_lastDiscovery = monotonicSeconds;
        m_hasDiscovered = true;
    }

    if (m_selected) {
        if (const auto value = readTemperature(m_selected->path))
            return {MetricStatus::okStatus(), value, m_selected->label};
    }

    // A selected channel may disappear, become unreadable, or contain an
    // invalid value. Rediscover immediately and try the other identified CPU
    // candidates before reporting a failure.
    if (m_selected) {
        m_selected.reset();
        m_candidates = discover();
        m_lastDiscovery = monotonicSeconds;
        m_hasDiscovered = true;
    }
    for (const Sensor &sensor : m_candidates) {
        if (const auto value = readTemperature(sensor.path)) {
            m_selected = sensor;
            return {MetricStatus::okStatus(), value, sensor.label};
        }
    }
    return m_candidates.empty()
        ? CpuTemperatureMetrics{MetricStatus::unavailable("no identifiable CPU temperature sensor found"), std::nullopt, {}}
        : CpuTemperatureMetrics{MetricStatus::error("identified CPU temperature sensor could not be read"), std::nullopt, {}};
}

} // namespace aqtruncat::metrics
