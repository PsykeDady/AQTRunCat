#include "metricsbarids.h"

#include <algorithm>

namespace {

const QString CustomPrefix = QStringLiteral("custom:");
const QString CustomItemPrefix = QStringLiteral("custom-item:");

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
        // F5. The three GPU entries follow the selected GPU (setting
        // gpuDevice); kernel and distribution are text and not offered.
        {QStringLiteral("uptime"), QStringLiteral("Uptime"), QStringLiteral("UP")},
        {QStringLiteral("gpu.utilization"), QStringLiteral("GPU utilization"), QStringLiteral("GPU")},
        {QStringLiteral("gpu.memory"), QStringLiteral("GPU memory"), QStringLiteral("VRAM")},
        {QStringLiteral("gpu.temperature"), QStringLiteral("GPU temperature"), QStringLiteral("GPU°")},
        {QStringLiteral("fps"), QStringLiteral("FPS"), QStringLiteral("FPS")},
        {QStringLiteral("updates.packages"), QStringLiteral("Package updates"), QStringLiteral("UPD")},
        {QStringLiteral("updates.flatpak"), QStringLiteral("Flatpak updates"), QStringLiteral("FLAT")},
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

QString customItemId(const QString &sourceId, const QString &itemId)
{
    return CustomItemPrefix + itemId + u':' + sourceId;
}

bool isCustomItemId(const QString &id)
{
    if (!id.startsWith(CustomItemPrefix))
        return false;
    const qsizetype separator = id.indexOf(u':', CustomItemPrefix.size());
    return separator > 0 && separator + 1 < id.size()
        && aqtruncat::custommetrics::isValidItemId(id.mid(CustomItemPrefix.size(), separator - CustomItemPrefix.size()));
}

QString sourceIdFromCustomItemId(const QString &id)
{
    return isCustomItemId(id) ? id.mid(id.indexOf(u':', CustomItemPrefix.size()) + 1) : QString();
}

QString itemIdFromCustomItemId(const QString &id)
{
    if (!isCustomItemId(id))
        return {};
    const qsizetype separator = id.indexOf(u':', CustomItemPrefix.size());
    return id.mid(CustomItemPrefix.size(), separator - CustomItemPrefix.size());
}

bool isSelectableItem(const aqtruncat::custommetrics::Item &item)
{
    return !item.id.isEmpty() && item.metricsBarValue && !item.metricsBarValue->trimmed().isEmpty();
}

bool isKnownId(const QString &id)
{
    return isBuiltInId(id) || isCustomId(id) || isCustomItemId(id);
}

} // namespace aqtruncat::metricsbar
