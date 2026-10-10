#pragma once

#include <string>
#include <utility>

namespace aqtruncat::metrics {

// Whether a metric has a value. "Unknown" is never encoded as 0: a section
// carries its values only when its status is Ok.
enum class MetricState {
    Ok,          // values are valid
    Pending,     // a rate needs a second sample (first sample, no time elapsed)
    Unavailable, // the source does not exist here (missing file, no such device)
    Error,       // the source exists but could not be read or parsed
};

struct MetricStatus {
    MetricState state = MetricState::Unavailable;
    std::string message; // why it is not Ok

    bool ok() const { return state == MetricState::Ok; }

    static MetricStatus okStatus() { return {MetricState::Ok, {}}; }
    static MetricStatus pending(std::string why) { return {MetricState::Pending, std::move(why)}; }
    static MetricStatus unavailable(std::string why) { return {MetricState::Unavailable, std::move(why)}; }
    static MetricStatus error(std::string why) { return {MetricState::Error, std::move(why)}; }
};

} // namespace aqtruncat::metrics
