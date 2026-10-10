#pragma once

#include <optional>

namespace aqtruncat {

// EMA weight used for CPU samples taken once per second (tau ≈ 2 s).
inline constexpr double CpuSmoothingAlpha = 0.4;

// Exponential moving average: s += alpha * (x - s).
//
// With one sample per interval T, the time constant is tau = -T / ln(1 - alpha);
// alpha = 0.4 at T = 1 s gives tau ≈ 2 s: a step reaches ~64% after 2 s and
// ~92% after 5 s. The first sample initialises the average directly, so the
// runner does not crawl up from zero at startup.
class Ema
{
public:
    explicit Ema(double alpha) : m_alpha(alpha) {}

    double add(double value)
    {
        m_value = m_value ? *m_value + m_alpha * (value - *m_value) : value;
        return *m_value;
    }

    std::optional<double> value() const { return m_value; }
    void reset() { m_value.reset(); }

private:
    double m_alpha;
    std::optional<double> m_value;
};

} // namespace aqtruncat
