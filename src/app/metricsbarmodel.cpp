#include "metricsbarmodel.h"

#include "customsymbol.h"
#include "hoversummary.h"
#include "metrics/compactformat.h"
#include "metrics/metricsformat.h"
#include "metrics/uptime.h"

#include <QFileInfo>

#include <algorithm>

using namespace aqtruncat::metricsbar;
using aqtruncat::metrics::MetricState;
using aqtruncat::metrics::MetricStatus;

namespace {


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
    // v2F1.2: a disabled source shows nothing; its selection is kept, so it
    // comes back in place when it is enabled again.
    m_sources.clear();
    for (const aqtruncat::custommetrics::Source &source : sources) {
        if (source.enabled)
            m_sources.append(source);
    }
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
            } else if (isCustomItemId(id)) {
                item = customItem(id);
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
        if (s.cpu.logicalCpus > 0)
            item.context = QStringLiteral("%1 logical CPUs").arg(s.cpu.logicalCpus);
    } else if (metric.id == QLatin1String("memory") || metric.id == QLatin1String("swap")) {
        applyStatus(item, s.memory.status);
        if (s.memory.status.ok() && s.memory.info) {
            const auto &m = *s.memory.info;
            if (metric.id == QLatin1String("memory")) {
                setPercent(item, m.usagePercent, QStringLiteral(" (%1 / %2)\n%3 available%4").arg(
                    q(aqtruncat::metrics::formatBytes(m.usedBytes)), q(aqtruncat::metrics::formatBytes(m.totalBytes)),
                    q(aqtruncat::metrics::formatBytes(m.availableBytes)),
                    m.availableIsEstimate ? QStringLiteral(" (estimated)") : QString()));
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
        QStringList counted;
        for (const auto &interface : s.network.interfaces) {
            if (interface.counted)
                counted.append(q(interface.name));
        }
        if (!counted.isEmpty())
            item.context = QStringLiteral("Interfaces: %1").arg(counted.join(QStringLiteral(", ")));
        if (s.network.status.ok())
            setRate(item, metric.id == QLatin1String("network.download") ? s.network.downloadBytesPerSecond
                                                                          : s.network.uploadBytesPerSecond);
    } else if (metric.id.startsWith(QLatin1String("disk."))) {
        applyStatus(item, s.diskIo.status);
        if (s.diskIo.countedDevices > 0)
            item.context = s.diskIo.countedDevices == 1 ? QStringLiteral("1 disk counted")
                                                        : QStringLiteral("%1 disks counted").arg(s.diskIo.countedDevices);
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
                setPercent(item, storage->info->usagePercent, QStringLiteral(" (%1 available)\n%2 / %3 used").arg(
                    q(aqtruncat::metrics::formatBytes(storage->info->availableBytes)),
                    q(aqtruncat::metrics::formatBytes(storage->info->usedBytes)),
                    q(aqtruncat::metrics::formatBytes(storage->info->totalBytes))));
            }
        }
    } else if (metric.id == QLatin1String("uptime")) {
        applyStatus(item, s.uptime.status);
        if (s.uptime.status.ok() && s.uptime.seconds) {
            item.shortValue = q(aqtruncat::metrics::compactDuration(*s.uptime.seconds));
            item.detail = q(aqtruncat::metrics::formatDuration(*s.uptime.seconds));
        }
    } else if (metric.id.startsWith(QLatin1String("gpu."))) {
        // The selected GPU (setting gpuDevice; resolved by the backend).
        const auto gpu = std::find_if(s.gpu.devices.cbegin(), s.gpu.devices.cend(), [](const auto &d) { return d.selected; });
        if (gpu == s.gpu.devices.cend()) {
            applyStatus(item, s.gpu.status.ok() ? MetricStatus::unavailable("no GPU selected") : s.gpu.status);
        } else {
            item.label = metric.label + QStringLiteral(" — ") + q(gpu->model);
            QStringList context;
            if (!gpu->driver.empty())
                context.append(q(gpu->driver));
            if (gpu->integrated)
                context.append(*gpu->integrated ? QStringLiteral("integrated") : QStringLiteral("discrete"));
            if (gpu->suspended)
                context.append(QStringLiteral("suspended"));
            item.context = context.join(QStringLiteral(" · "));
            if (metric.id == QLatin1String("gpu.utilization") && gpu->utilizationMethod == "rc6-residency")
                item.context += QStringLiteral("\nActivity: time out of the RC6 power-saving state");
            std::optional<std::string> compact;
            QString detail;
            if (metric.id == QLatin1String("gpu.utilization")) {
                applyStatus(item, gpu->utilizationStatus);
                if (gpu->utilizationStatus.ok() && gpu->utilizationPercent) {
                    compact = aqtruncat::metrics::compactPercent(*gpu->utilizationPercent);
                    detail = q(aqtruncat::metrics::formatPercent(*gpu->utilizationPercent));
                }
            } else if (metric.id == QLatin1String("gpu.memory")) {
                applyStatus(item, gpu->memoryStatus);
                if (gpu->memoryStatus.ok() && gpu->memoryUsedBytes && gpu->memoryTotalBytes && *gpu->memoryTotalBytes) {
                    const double percent = 100.0 * double(*gpu->memoryUsedBytes) / double(*gpu->memoryTotalBytes);
                    compact = aqtruncat::metrics::compactPercent(percent);
                    detail = QStringLiteral("%1 / %2").arg(q(aqtruncat::metrics::formatBytes(*gpu->memoryUsedBytes)),
                                                           q(aqtruncat::metrics::formatBytes(*gpu->memoryTotalBytes)));
                }
            } else {
                applyStatus(item, gpu->temperatureStatus);
                if (gpu->temperatureStatus.ok() && gpu->temperatureCelsius) {
                    compact = aqtruncat::metrics::compactCelsius(*gpu->temperatureCelsius);
                    detail = q(aqtruncat::metrics::formatCelsius(*gpu->temperatureCelsius));
                }
            }
            if (compact) {
                item.shortValue = q(*compact);
                item.detail = detail;
            }
        }
    } else if (metric.id == QLatin1String("fps")) {
        // Source OFF: no item, not a placeholder; the selection is kept so
        // FPS comes back by itself when a source is chosen again.
        if (aqtruncat::metrics::fpsHidden(s.fps))
            return std::nullopt;
        applyStatus(item, s.fps.status);
        if (s.fps.source == "mangohud")
            item.context = QStringLiteral("Source: MangoHud");
        if (s.fps.status.ok() && s.fps.fps) {
            item.shortValue = q(aqtruncat::metrics::compactWhole(*s.fps.fps).value_or(std::string()));
            item.detail = q(aqtruncat::metrics::formatFps(*s.fps.fps))
                + (s.fps.application.empty() ? QString() : QStringLiteral(" — ") + q(s.fps.application));
        }
    } else if (metric.id.startsWith(QLatin1String("updates."))) {
        const auto &updates = metric.id == QLatin1String("updates.packages") ? s.packageUpdates : s.flatpakUpdates;
        applyStatus(item, updates.status);
        QStringList context;
        if (metric.id == QLatin1String("updates.packages") && !updates.manager.empty())
            context.append(QStringLiteral("Package manager: %1").arg(q(updates.manager)));
        if (updates.lastSuccess)
            context.append(QStringLiteral("Last successful check: %1").arg(aqtruncat::hover::whenText(*updates.lastSuccess)));
        if (updates.checking)
            context.append(QStringLiteral("Checking now…"));
        if (updates.metadataStale)
            context.append(QStringLiteral("Package metadata is more than 7 days old"));
        item.context = context.join(u'\n');
        if (updates.status.ok() && updates.count) {
            item.shortValue = q(aqtruncat::metrics::compactCount(*updates.count).value_or(std::string()));
            item.detail = q(aqtruncat::metrics::formatUpdateCount(*updates.count));
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
    if (source->type == aqtruncat::custommetrics::SourceType::Snapshot && snapshot
        && (!snapshot->metricsBarValue || snapshot->metricsBarValue->trimmed().isEmpty()))
        return std::nullopt;

    Item item;
    item.id = id;
    item.origin = Origin::Custom;
    item.label = snapshot ? snapshot->title : QFileInfo(source->path).completeBaseName();
    if (snapshot) {
        item.shortLabel = customSymbolText(snapshot->symbol);
        item.icon = snapshot->icon;
        if (snapshot->metricsBarValue) {
            item.shortValue = *snapshot->metricsBarValue;
            item.detail = *snapshot->metricsBarValue;
        }
        item.lastUpdated = snapshot->lastUpdatedDate;
    }
    item.status = statusFor(source->state);
    item.statusMessage = source->message;
    if (item.status == Status::Error) {
        // v2F1.3: the icon shows "!", and the tooltip the reason, never the
        // last value as if it were current.
        item.shortValue.clear();
        item.detail.clear();
    }
    return item;
}

std::optional<Item> MetricsBarModel::customItem(const QString &id) const
{
    const QString sourceId = sourceIdFromCustomItemId(id);
    const QString itemId = itemIdFromCustomItemId(id);
    const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(),
                                     [&](const auto &s) { return s.id == sourceId; });
    if (source == m_sources.cend())
        return std::nullopt;

    Item item;
    item.id = id;
    item.origin = Origin::Custom;
    item.status = statusFor(source->state);
    item.statusMessage = source->message;
    const auto &snapshot = source->snapshot;
    const QString sourceTitle = snapshot ? snapshot->title : QFileInfo(source->path).completeBaseName();
    item.label = sourceTitle;
    if (snapshot) {
        item.shortLabel = customSymbolText(snapshot->symbol);
        item.icon = snapshot->icon;
        item.lastUpdated = snapshot->lastUpdatedDate;
    }
    // A command skeleton (before the first result) has no items yet: the
    // source state (Pending) speaks for the item.
    if (!snapshot || snapshot->metrics.isEmpty())
        return item;

    const auto metric = std::find_if(snapshot->metrics.cbegin(), snapshot->metrics.cend(),
                                     [&](const auto &m) { return m.id == itemId; });
    if (metric == snapshot->metrics.cend()) {
        if (item.status == Status::Available || item.status == Status::Pending) {
            item.status = Status::Unavailable;
            item.statusMessage = QStringLiteral("not reported by this source");
        }
        return item;
    }
    item.label = sourceTitle + QStringLiteral(" — ") + metric->title;
    if (!metric->metricsBarLabel.isEmpty()) {
        // A text caption tells several items of one source apart; the
        // shared source icon could not.
        item.shortLabel = metric->metricsBarLabel;
        item.icon.reset();
    }
    item.detail = metric->detail.isEmpty() ? metric->formattedValue
                                           : metric->formattedValue + QStringLiteral(" — ") + metric->detail;
    if (isSelectableItem(*metric)) {
        item.shortValue = *metric->metricsBarValue;
    } else if (item.status == Status::Available || item.status == Status::Pending) {
        // The producer has no compact value for it right now (e.g. data not
        // reported): never show an empty or invented value.
        item.status = Status::Unavailable;
        item.statusMessage = metric->formattedValue;
    }
    if (item.status == Status::Error) {
        item.shortValue.clear();
        item.detail.clear();
    }
    return item;
}
