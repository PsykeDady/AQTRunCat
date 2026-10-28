#pragma once

#include "metricstatus.h"

#include <optional>
#include <string>
#include <vector>

namespace aqtruncat::metrics {

struct CpuTemperatureMetrics {
    MetricStatus status;
    std::optional<double> celsius;
    std::string sensor;
};

// Reads one identifiable CPU sensor from Linux sysfs. Discovery is cached for
// five minutes; a failed read forces immediate rediscovery.
class CpuTemperatureSampler
{
public:
    explicit CpuTemperatureSampler(std::string sysRoot = "/sys");
    CpuTemperatureMetrics sample(double monotonicSeconds);

    // Public only so the translation-unit's thermal-zone fallback can share
    // the same small candidate representation; callers should use sample().
    struct Sensor {
        std::string path;
        std::string key;
        std::string label;
        int priority = 0;
    };

private:
    std::vector<Sensor> discover() const;
    std::string m_sysRoot;
    std::optional<Sensor> m_selected;
    std::vector<Sensor> m_candidates;
    double m_lastDiscovery = -1.0;
    bool m_hasDiscovered = false;
};

} // namespace aqtruncat::metrics
