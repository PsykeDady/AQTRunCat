#pragma once

// Rates from cumulative byte counters (network interfaces, block devices):
//   rate = delta_bytes / actual_elapsed_seconds
// The elapsed time is the real time between the two samples, never an
// assumed sampling interval.

#include "metricstatus.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace aqtruncat::metrics {

// Two cumulative byte counters of one source (rx/tx, read/written).
struct CounterPair {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
};

using CounterSet = std::map<std::string, CounterPair>; // by name, sorted

struct SourceRate {
    std::string name;
    bool counted = false;            // included in the aggregate (policy decided by the caller)
    std::optional<double> firstPerSecond;  // bytes/s; empty until two samples exist
    std::optional<double> secondPerSecond;
    bool counterReset = false;       // a counter went backwards: no rate this interval
};

struct AggregateRates {
    MetricStatus status;
    std::optional<double> firstPerSecond;  // sum over counted sources with a rate
    std::optional<double> secondPerSecond;
    int countedSources = 0;          // counted sources present now
    std::vector<SourceRate> sources; // every source, counted or not
};

class CounterRateTracker
{
public:
    // `counted(name)` decides which sources enter the aggregate.
    // `nowSeconds` comes from a monotonic clock.
    //
    // - first sample, or no time elapsed: status Pending, no rates
    // - a source seen for the first time (appearing): no rate yet
    // - a source gone since the previous sample: simply absent
    // - a counter that went backwards (reset, wrap, re-created device): no rate
    //   for that source this interval, never a huge bogus value
    template<typename CountedPredicate>
    AggregateRates update(const CounterSet &counters, double nowSeconds, CountedPredicate counted);

    void reset()
    {
        m_previous.reset();
        m_previousTime = 0.0;
    }

private:
    std::optional<CounterSet> m_previous;
    double m_previousTime = 0.0;
};

// --- implementation ------------------------------------------------------

template<typename CountedPredicate>
AggregateRates CounterRateTracker::update(const CounterSet &counters, double nowSeconds, CountedPredicate counted)
{
    AggregateRates result;
    const bool havePrevious = m_previous.has_value();
    const double elapsed = nowSeconds - m_previousTime;
    const bool elapsedValid = havePrevious && std::isfinite(elapsed) && elapsed > 0.0;

    double first = 0.0, second = 0.0;
    int withRate = 0;
    for (const auto &[name, now] : counters) {
        SourceRate rate;
        rate.name = name;
        rate.counted = counted(name);
        if (rate.counted)
            ++result.countedSources;
        if (elapsedValid) {
            const auto before = m_previous->find(name);
            if (before != m_previous->end()) {
                if (now.first < before->second.first || now.second < before->second.second) {
                    rate.counterReset = true;
                } else {
                    rate.firstPerSecond = double(now.first - before->second.first) / elapsed;
                    rate.secondPerSecond = double(now.second - before->second.second) / elapsed;
                    if (rate.counted) {
                        first += *rate.firstPerSecond;
                        second += *rate.secondPerSecond;
                        ++withRate;
                    }
                }
            }
        }
        result.sources.push_back(std::move(rate));
    }

    if (!havePrevious) {
        result.status = MetricStatus::pending("first sample: rates need a second one");
    } else if (!elapsedValid) {
        result.status = MetricStatus::pending("no time elapsed since the previous sample");
        return result; // keep the previous baseline
    } else if (result.countedSources == 0) {
        result.status = MetricStatus::unavailable("no source matches the aggregation policy");
    } else if (withRate == 0) {
        result.status = MetricStatus::pending("counters were reset or sources just appeared");
    } else {
        result.status = MetricStatus::okStatus();
        result.firstPerSecond = first;
        result.secondPerSecond = second;
    }
    m_previous = counters;
    m_previousTime = nowSeconds;
    return result;
}

} // namespace aqtruncat::metrics
