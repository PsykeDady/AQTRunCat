#include "hoversummary.h"

#include "metrics/metricsformat.h"

#include <QDateTime>
#include <QLocale>

namespace aqtruncat::hover {

namespace {

QString q(const std::string &text)
{
    return QString::fromStdString(text);
}

// What a missing value is called (same words as logic.js stateText).
QString stateText(metrics::MetricState state)
{
    switch (state) {
    case metrics::MetricState::Pending: return QStringLiteral("waiting for data");
    case metrics::MetricState::Unavailable: return QStringLiteral("unavailable");
    case metrics::MetricState::Error: return QStringLiteral("error");
    case metrics::MetricState::Ok: break;
    }
    // Ok without its value: the next sample brings it.
    return QStringLiteral("waiting for data");
}

QString line(const QString &label, const QString &value)
{
    return label + QStringLiteral(": ") + value;
}

} // namespace

QStringList summaryLines(Connection connection, const metrics::SystemMetricsSnapshot *snapshot)
{
    if (connection == Connection::Unavailable)
        return {UnavailableText};
    if (connection == Connection::Connecting || !snapshot)
        return {ConnectingText};

    QStringList lines;
    const metrics::CpuMetrics &cpu = snapshot->cpu;
    lines.append(line(QStringLiteral("CPU"), cpu.status.ok() && cpu.usagePercent
                                                 ? q(metrics::formatPercent(*cpu.usagePercent))
                                                 : stateText(cpu.status.state)));

    const metrics::MemoryMetrics &memory = snapshot->memory;
    if (memory.status.ok() && memory.info) {
        const metrics::MemoryInfo &m = *memory.info;
        lines.append(line(QStringLiteral("RAM"), q(metrics::formatPercent(m.usagePercent)) + QStringLiteral(" · ")
                                                     + q(metrics::formatMemoryDetail(m))));
        // No swap configured is not a missing value: no line at all.
        if (m.swapPresent) {
            lines.append(line(QStringLiteral("Swap"), q(metrics::formatPercent(m.swapUsagePercent)) + QStringLiteral(" · ")
                                                          + q(metrics::formatSwapDetail(m))));
        }
    } else {
        const QString state = stateText(memory.status.ok() ? metrics::MetricState::Pending : memory.status.state);
        lines.append(line(QStringLiteral("RAM"), state));
        lines.append(line(QStringLiteral("Swap"), state));
    }
    return lines;
}

QString runnerToolTip(const QString &runnerName, const QStringList &lines)
{
    QStringList rows{runnerName.isEmpty() ? QStringLiteral("AQTRunCat") : QStringLiteral("AQTRunCat — ") + runnerName};
    rows += lines;
    return rows.join(u'\n');
}

QString whenText(std::int64_t epochSeconds)
{
    const QDateTime time = QDateTime::fromSecsSinceEpoch(epochSeconds).toLocalTime();
    const QLocale c = QLocale::c();
    return time.date() == QDate::currentDate() ? c.toString(time, QStringLiteral("HH:mm"))
                                               : c.toString(time, QStringLiteral("MMM d, HH:mm"));
}

} // namespace aqtruncat::hover
