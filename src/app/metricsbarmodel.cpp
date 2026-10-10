#include "metricsbarmodel.h"

#include "customsymbol.h"
#include "metrics/compactformat.h"
#include "metrics/metricsformat.h"

#include <QFileInfo>

#include <algorithm>

using namespace aqtruncat::metricsbar;
using aqtruncat::metrics::MetricState;
using aqtruncat::metrics::MetricStatus;

namespace {

const QString CustomPrefix = QStringLiteral("custom:");

QString q(const std::string &text)
{
    return QString::fromStdString(text);
}

Status statusFor(MetricState state)
{
    switch (state) {
    case MetricState::Ok: return Status::Available;
    case MetricState::Pending: return Status::Pending;
    case MetricState::Unavailable: return Status::Unavailable;
    case MetricState::Error: return Status::Error;
    }
    return Status::Error;
}

Status statusFor(aqtruncat::custommetrics::State state)
{
    switch (state) {
    case aqtruncat::custommetrics::State::Available: return Status::Available;
    case aqtruncat::custommetrics::State::Pending: return Status::Pending;
    case aqtruncat::custommetrics::State::Unavailable: return Status::Unavailable;
    case aqtruncat::custommetrics::State::Error: return Status::Error;
    }
    return Status::Error;
}

// Applies a section status; an Ok section without the value is still waiting.
void applyStatus(Item &item, const MetricStatus &status)
{
    item.status = statusFor(status.state);
    item.statusMessage = q(status.message);
}

void setPercent(Item &item, double percent, const QString &suffix = {})
{
    item.status = Status::Available;
    item.statusMessage.clear();
    item.shortValue = q(aqtruncat::metrics::compactPercent(percent).value_or(std::string()));
    item.detail = q(aqtruncat::metrics::formatPercent(percent)) + suffix;
}

void setRate(Item &item, const std::optional<double> &rate)
{
    const auto compact = rate ? aqtruncat::metrics::compactRate(*rate) : std::nullopt;
    if (!compact) {
        item.status = Status::Pending;
        item.statusMessage = QStringLiteral("waiting for the next sample");
        return;
    }
    item.status = Status::Available;
    item.statusMessage.clear();
    item.shortValue = q(*compact);
    item.detail = q(aqtruncat::metrics::formatRate(*rate));
}

} // namespace

namespace aqtruncat::metricsbar {

const QList<BuiltInMetric> &builtInMetrics()
{
    static const QList<BuiltInMetric> metrics = {
        {QStringLiteral("cpu"), QStringLiteral("CPU"), QStringLiteral("CPU")},
        {QStringLiteral("memory"), QStringLiteral("Memory"), QStringLiteral("MEM")},
        {QStringLiteral("swap"), QStringLiteral("Swap"), QStringLiteral("SWAP")},
        {QStringLiteral("network.download"), QStringLiteral("Network download"), QStringLiteral("NET↓")},
        {QStringLiteral("network.upload"), QStringLiteral("Network upload"), QStringLiteral("NET↑")},
        {QStringLiteral("storage.root"), QStringLiteral("Root storage"), QStringLiteral("ROOT")},
        {QStringLiteral("storage.home"), QStringLiteral("Home storage"), QStringLiteral("HOME")},
        {QStringLiteral("disk.read"), QStringLiteral("Disk read"), QStringLiteral("DSK↓")},
        {QStringLiteral("disk.write"), QStringLiteral("Disk write"), QStringLiteral("DSK↑")},
    };
    return metrics;
}

bool isBuiltInId(const QString &id)
{
    const auto &metrics = builtInMetrics();
    return std::any_of(metrics.cbegin(), metrics.cend(), [&](const BuiltInMetric &m) { return m.id == id; });
}

QString customId(const QString &sourceId)
{
    return CustomPrefix + sourceId;
}

bool isCustomId(const QString &id)
{
    return id.startsWith(CustomPrefix) && id.size() > CustomPrefix.size();
}

QString sourceIdFromCustomId(const QString &id)
{
    return isCustomId(id) ? id.mid(CustomPrefix.size()) : QString();
}

bool isKnownId(const QString &id)
{
    return isBuiltInId(id) || isCustomId(id);
}

} // namespace aqtruncat::metricsbar

MetricsBarModel::MetricsBarModel(std::string homePath, QObject *parent)
    : QObject(parent)
    , m_homePath(std::move(homePath))
{
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &MetricsBarModel::rebuild);
}

void MetricsBarModel::setConfiguration(bool enabled, const QStringList &order)
{
    m_enabled = enabled;
    m_order = order.mid(0, MaxItems);
    scheduleRebuild();
}

void MetricsBarModel::updateSystem(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot)
{
    m_system = snapshot;
    scheduleRebuild();
}

void MetricsBarModel::updateCustom(const QList<aqtruncat::custommetrics::Source> &sources)
{
    m_sources = sources;
    scheduleRebuild();
}

void MetricsBarModel::scheduleRebuild()
{
    m_coalesce.start();
}

void MetricsBarModel::rebuild()
{
    QList<Item> next;
    if (m_enabled) {
        for (const QString &id : std::as_const(m_order)) {
            std::optional<Item> item;
            if (isCustomId(id)) {
                item = custom(id);
            } else {
                const auto &metrics = builtInMetrics();
                const auto metric = std::find_if(metrics.cbegin(), metrics.cend(),
                                                 [&](const BuiltInMetric &m) { return m.id == id; });
                if (metric != metrics.cend())
                    item = builtIn(*metric);
                // Unknown ids (a future version's metric) are ignored, not dropped.
            }
            if (item)
                next.append(std::move(*item));
        }
    }
    if (next == m_items)
        return;
    m_items = std::move(next);
    emit itemsChanged(m_items);
}

std::optional<Item> MetricsBarModel::builtIn(const BuiltInMetric &metric) const
{
    Item item;
    item.id = metric.id;
    item.origin = Origin::BuiltIn;
    item.label = metric.label;
    item.shortLabel = metric.shortLabel;
    if (!m_system) {
        item.status = Status::Pending;
        item.statusMessage = QStringLiteral("waiting for the first sample");
        // Home is only known to exist once a snapshot lists it.
        return metric.id == QLatin1String("storage.home") ? std::nullopt : std::optional<Item>(item);
    }
    const auto &s = *m_system;

    if (metric.id == QLatin1String("cpu")) {
        applyStatus(item, s.cpu.status);
        if (s.cpu.status.ok() && s.cpu.usagePercent)
            setPercent(item, *s.cpu.usagePercent);
    } else if (metric.id == QLatin1String("memory") || metric.id == QLatin1String("swap")) {
        applyStatus(item, s.memory.status);
        if (s.memory.status.ok() && s.memory.info) {
            const auto &m = *s.memory.info;
            if (metric.id == QLatin1String("memory")) {
                setPercent(item, m.usagePercent, QStringLiteral(" (%1 / %2)").arg(
                    q(aqtruncat::metrics::formatBytes(m.usedBytes)), q(aqtruncat::metrics::formatBytes(m.totalBytes))));
            } else if (m.swapPresent) {
                setPercent(item, m.swapUsagePercent, QStringLiteral(" (%1 / %2)").arg(
                    q(aqtruncat::metrics::formatBytes(m.swapUsedBytes)), q(aqtruncat::metrics::formatBytes(m.swapTotalBytes))));
            } else {
                item.status = Status::Unavailable;
                item.statusMessage = QStringLiteral("no swap is configured");
            }
        }
    } else if (metric.id.startsWith(QLatin1String("network."))) {
        applyStatus(item, s.network.status);
        if (s.network.status.ok())
            setRate(item, metric.id == QLatin1String("network.download") ? s.network.downloadBytesPerSecond
                                                                          : s.network.uploadBytesPerSecond);
    } else if (metric.id.startsWith(QLatin1String("disk."))) {
        applyStatus(item, s.diskIo.status);
        if (s.diskIo.status.ok())
            setRate(item, metric.id == QLatin1String("disk.read") ? s.diskIo.readBytesPerSecond
                                                                   : s.diskIo.writeBytesPerSecond);
    } else if (metric.id.startsWith(QLatin1String("storage."))) {
        const bool home = metric.id == QLatin1String("storage.home");
        // Home is a separate entry only when it is on its own filesystem;
        // otherwise it would repeat Root, so it is not offered at all.
        if (home && (m_homePath.empty() || m_homePath == "/"))
            return std::nullopt;
        const std::string path = home ? m_homePath : std::string("/");
        const auto storage = std::find_if(s.storage.cbegin(), s.storage.cend(),
                                          [&](const auto &entry) { return entry.path == path; });
        if (storage == s.storage.cend()) {
            if (home)
                return std::nullopt;
            item.status = Status::Unavailable;
            item.statusMessage = QStringLiteral("root filesystem is not sampled");
        } else {
            applyStatus(item, storage->status);
            if (storage->status.ok() && storage->info) {
                setPercent(item, storage->info->usagePercent, QStringLiteral(" (%1 available)").arg(
                    q(aqtruncat::metrics::formatBytes(storage->info->availableBytes))));
            }
        }
    }
    if (item.status == Status::Available && item.shortValue.isEmpty()) {
        item.status = Status::Pending;
        item.statusMessage = QStringLiteral("waiting for the next sample");
    }
    return item;
}

std::optional<Item> MetricsBarModel::custom(const QString &id) const
{
    const QString sourceId = sourceIdFromCustomId(id);
    const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(),
                                     [&](const auto &s) { return s.id == sourceId; });
    // A source that is gone (discovered file deleted, registration removed)
    // has no entry; the selection stays persisted for when it comes back.
    if (source == m_sources.cend())
        return std::nullopt;
    const auto &snapshot = source->snapshot;
    // The producer decides what the bar shows. Without a metricsBarValue the
    // source is not eligible: no value is derived from formattedValue.
    if (snapshot && (!snapshot->metricsBarValue || snapshot->metricsBarValue->trimmed().isEmpty()))
        return std::nullopt;

    Item item;
    item.id = id;
    item.origin = Origin::Custom;
    item.label = snapshot ? snapshot->title : QFileInfo(source->path).completeBaseName();
    if (snapshot) {
        item.shortLabel = customSymbolText(snapshot->symbol);
        item.shortValue = *snapshot->metricsBarValue;
        item.detail = *snapshot->metricsBarValue;
        item.lastUpdated = snapshot->lastUpdatedDate;
    }
    item.status = statusFor(source->state);
    item.statusMessage = source->message;
    return item;
}
