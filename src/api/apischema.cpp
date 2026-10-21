#include "apischema.h"

#include "app/appsettings.h"
#include "app/metricsbarids.h"
#include "metrics/compactformat.h"
#include "metrics/metricsformat.h"

#include <QFileInfo>
#include <QHash>

#include <algorithm>

namespace aqtruncat::api {
namespace {

using metrics::MetricState;
using metrics::MetricStatus;

QString q(const std::string &text) { return QString::fromStdString(text); }

QJsonValue optionalString(const std::optional<std::string> &text)
{
    return text ? QJsonValue(q(*text)) : QJsonValue();
}

QJsonObject sectionObject(const MetricStatus &status)
{
    return QJsonObject{{QStringLiteral("state"), stateName(status.state)}, {QStringLiteral("message"), q(status.message)}};
}

MetricStatus sectionStatus(const QJsonObject &metric)
{
    const QJsonObject section = metric.value(QStringLiteral("data")).toObject().value(QStringLiteral("section")).toObject();
    const QString state = section.value(QStringLiteral("state")).toString(metric.value(QStringLiteral("state")).toString());
    const std::string message = section.value(QStringLiteral("message")).toString().toStdString();
    if (state == QLatin1String("ok"))
        return MetricStatus::okStatus();
    if (state == QLatin1String("pending"))
        return MetricStatus::pending(message);
    if (state == QLatin1String("error"))
        return MetricStatus::error(message);
    return MetricStatus::unavailable(message);
}

// The common shape of every metric object (schema 1).
QJsonObject metricObject(const QString &id, const QString &category, const QString &kind, const QString &title)
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("category"), category},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("integration"), QJsonValue()},
        {QStringLiteral("title"), title},
        {QStringLiteral("state"), QStringLiteral("pending")},
        {QStringLiteral("message"), QString()},
        {QStringLiteral("value"), QJsonValue()},
        {QStringLiteral("unit"), QJsonValue()},
        {QStringLiteral("formattedValue"), QString()},
        {QStringLiteral("detail"), QString()},
        {QStringLiteral("compactValue"), QJsonValue()},
        {QStringLiteral("compactLabel"), QString()},
        {QStringLiteral("normalized"), QJsonValue()},
        {QStringLiteral("updatedAt"), QJsonValue()},
        {QStringLiteral("icon"), QJsonValue()},
        {QStringLiteral("items"), QJsonArray()},
        {QStringLiteral("data"), QJsonObject()},
    };
}

void setStatus(QJsonObject &metric, const MetricStatus &status)
{
    metric.insert(QStringLiteral("state"), stateName(status.state));
    metric.insert(QStringLiteral("message"), q(status.message));
}

void setPercent(QJsonObject &metric, double percent)
{
    metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
    metric.insert(QStringLiteral("message"), QString());
    metric.insert(QStringLiteral("value"), percent);
    metric.insert(QStringLiteral("unit"), QStringLiteral("percent"));
    metric.insert(QStringLiteral("formattedValue"), q(metrics::formatPercent(percent)));
    metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactPercent(percent)));
    metric.insert(QStringLiteral("normalized"), std::clamp(percent / 100.0, 0.0, 1.0));
}

void setRate(QJsonObject &metric, const MetricStatus &section, const std::optional<double> &rate)
{
    metric.insert(QStringLiteral("unit"), QStringLiteral("bytes-per-second"));
    if (!section.ok()) {
        setStatus(metric, section);
        return;
    }
    if (!rate) {
        // The section works but this rate needs another sample.
        setStatus(metric, MetricStatus::pending("waiting for the next sample"));
        return;
    }
    metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
    metric.insert(QStringLiteral("value"), *rate);
    metric.insert(QStringLiteral("formattedValue"), q(metrics::formatRate(*rate)));
    metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactRate(*rate)));
}

QString kindName(metrics::InterfaceKind kind)
{
    switch (kind) {
    case metrics::InterfaceKind::Loopback: return QStringLiteral("loopback");
    case metrics::InterfaceKind::Physical: return QStringLiteral("physical");
    case metrics::InterfaceKind::Virtual: return QStringLiteral("virtual");
    }
    return QStringLiteral("virtual");
}

metrics::InterfaceKind interfaceKind(const QString &name)
{
    if (name == QLatin1String("loopback"))
        return metrics::InterfaceKind::Loopback;
    if (name == QLatin1String("physical"))
        return metrics::InterfaceKind::Physical;
    return metrics::InterfaceKind::Virtual;
}

QJsonValue optionalNumber(const std::optional<double> &value)
{
    return value ? QJsonValue(*value) : QJsonValue();
}

std::optional<double> numberOf(const QJsonValue &value)
{
    return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
}

QJsonObject networkData(const metrics::NetworkMetrics &network)
{
    QJsonArray interfaces;
    for (const auto &i : network.interfaces) {
        interfaces.append(QJsonObject{{QStringLiteral("name"), q(i.name)},
                                      {QStringLiteral("kind"), kindName(i.kind)},
                                      {QStringLiteral("counted"), i.counted},
                                      {QStringLiteral("downloadBytesPerSecond"), optionalNumber(i.downloadBytesPerSecond)},
                                      {QStringLiteral("uploadBytesPerSecond"), optionalNumber(i.uploadBytesPerSecond)},
                                      {QStringLiteral("counterReset"), i.counterReset}});
    }
    return QJsonObject{{QStringLiteral("section"), sectionObject(network.status)},
                       {QStringLiteral("countedInterfaces"), network.countedInterfaces},
                       {QStringLiteral("interfaces"), interfaces}};
}

QJsonObject iconObject(const custommetrics::Snapshot &snapshot)
{
    if (snapshot.icon) {
        return QJsonObject{
            {QStringLiteral("type"), snapshot.icon->type == custommetrics::IconType::File ? QStringLiteral("file")
                                                                                          : QStringLiteral("theme")},
            {QStringLiteral("value"), snapshot.icon->value},
            {QStringLiteral("resolved"), snapshot.icon->resolvedValue}};
    }
    if (snapshot.symbol)
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("symbol")}, {QStringLiteral("value"), *snapshot.symbol}};
    return {};
}

} // namespace

QString stateName(metrics::MetricState state)
{
    switch (state) {
    case MetricState::Ok: return QStringLiteral("ok");
    case MetricState::Pending: return QStringLiteral("pending");
    case MetricState::Unavailable: return QStringLiteral("unavailable");
    case MetricState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QString stateName(custommetrics::State state)
{
    switch (state) {
    case custommetrics::State::Available: return QStringLiteral("ok");
    case custommetrics::State::Pending: return QStringLiteral("pending");
    case custommetrics::State::Unavailable: return QStringLiteral("unavailable");
    case custommetrics::State::Error: return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

QJsonArray builtInMetrics(const std::optional<metrics::SystemMetricsSnapshot> &snapshot, const std::string &homePath)
{
    QJsonArray result;
    const bool separateHome = !homePath.empty() && homePath != "/" && snapshot
        && std::any_of(snapshot->storage.cbegin(), snapshot->storage.cend(),
                       [&](const auto &s) { return s.path == homePath; });
    for (const metricsbar::BuiltInMetric &builtIn : metricsbar::builtInMetrics()) {
        const QString &id = builtIn.id;
        if (id == QLatin1String("storage.home") && !separateHome)
            continue; // Home on the root filesystem: no separate entry
        QJsonObject metric = metricObject(id, QStringLiteral("builtin"), QStringLiteral("system"), builtIn.label);
        metric.insert(QStringLiteral("compactLabel"), builtIn.shortLabel);
        if (!snapshot) {
            metric.insert(QStringLiteral("message"), QStringLiteral("waiting for the first sample"));
            result.append(metric);
            continue;
        }
        const metrics::SystemMetricsSnapshot &s = *snapshot;
        QJsonObject data;
        if (id == QLatin1String("cpu")) {
            data = {{QStringLiteral("section"), sectionObject(s.cpu.status)}, {QStringLiteral("logicalCpus"), s.cpu.logicalCpus}};
            if (s.cpu.status.ok() && s.cpu.usagePercent)
                setPercent(metric, *s.cpu.usagePercent);
            else
                setStatus(metric, s.cpu.status.ok() ? MetricStatus::pending("waiting for the next sample") : s.cpu.status);
        } else if (id == QLatin1String("memory") || id == QLatin1String("swap")) {
            const bool memory = id == QLatin1String("memory");
            data = {{QStringLiteral("section"), sectionObject(s.memory.status)}};
            if (s.memory.status.ok() && s.memory.info) {
                const metrics::MemoryInfo &m = *s.memory.info;
                if (memory) {
                    data.insert(QStringLiteral("usedBytes"), double(m.usedBytes));
                    data.insert(QStringLiteral("totalBytes"), double(m.totalBytes));
                    data.insert(QStringLiteral("availableBytes"), double(m.availableBytes));
                    data.insert(QStringLiteral("availableIsEstimate"), m.availableIsEstimate);
                    setPercent(metric, m.usagePercent);
                    metric.insert(QStringLiteral("detail"), q(metrics::formatBytes(m.usedBytes)) + QStringLiteral(" / ")
                                                                + q(metrics::formatBytes(m.totalBytes)) + QStringLiteral(" used")
                                                                + (m.availableIsEstimate ? QStringLiteral(" (estimated)") : QString()));
                } else {
                    data.insert(QStringLiteral("present"), m.swapPresent);
                    data.insert(QStringLiteral("usedBytes"), double(m.swapUsedBytes));
                    data.insert(QStringLiteral("totalBytes"), double(m.swapTotalBytes));
                    if (m.swapPresent) {
                        setPercent(metric, m.swapUsagePercent);
                        metric.insert(QStringLiteral("detail"), q(metrics::formatBytes(m.swapUsedBytes)) + QStringLiteral(" / ")
                                                                    + q(metrics::formatBytes(m.swapTotalBytes)));
                    } else {
                        setStatus(metric, MetricStatus::unavailable("no swap is configured"));
                    }
                }
            } else {
                setStatus(metric, s.memory.status);
            }
        } else if (id.startsWith(QLatin1String("network."))) {
            data = networkData(s.network);
            setRate(metric, s.network.status, id == QLatin1String("network.download") ? s.network.downloadBytesPerSecond
                                                                                     : s.network.uploadBytesPerSecond);
        } else if (id.startsWith(QLatin1String("disk."))) {
            data = {{QStringLiteral("section"), sectionObject(s.diskIo.status)},
                    {QStringLiteral("countedDevices"), s.diskIo.countedDevices}};
            setRate(metric, s.diskIo.status, id == QLatin1String("disk.read") ? s.diskIo.readBytesPerSecond
                                                                              : s.diskIo.writeBytesPerSecond);
        } else if (id.startsWith(QLatin1String("storage."))) {
            const std::string path = id == QLatin1String("storage.home") ? homePath : std::string("/");
            const auto storage = std::find_if(s.storage.cbegin(), s.storage.cend(),
                                              [&](const auto &entry) { return entry.path == path; });
            data = {{QStringLiteral("path"), q(path)}};
            if (storage == s.storage.cend()) {
                const MetricStatus missing = MetricStatus::unavailable("filesystem is not sampled");
                data.insert(QStringLiteral("section"), sectionObject(missing));
                setStatus(metric, missing);
            } else {
                data.insert(QStringLiteral("section"), sectionObject(storage->status));
                if (storage->status.ok() && storage->info) {
                    data.insert(QStringLiteral("usedBytes"), double(storage->info->usedBytes));
                    data.insert(QStringLiteral("totalBytes"), double(storage->info->totalBytes));
                    data.insert(QStringLiteral("availableBytes"), double(storage->info->availableBytes));
                    setPercent(metric, storage->info->usagePercent);
                    metric.insert(QStringLiteral("detail"),
                                  q(metrics::formatBytes(storage->info->availableBytes)) + QStringLiteral(" available"));
                } else {
                    setStatus(metric, storage->status);
                }
            }
        }
        metric.insert(QStringLiteral("data"), data);
        result.append(metric);
    }
    return result;
}

QJsonObject customMetric(const custommetrics::Source &source)
{
    const bool integration = !source.integration.isEmpty();
    const bool command = source.type == custommetrics::SourceType::Command;
    const QString title = source.snapshot ? source.snapshot->title : QFileInfo(source.path).completeBaseName();
    QJsonObject metric = metricObject(metricsbar::customId(source.id),
                                      integration ? QStringLiteral("integration") : QStringLiteral("custom"),
                                      command ? QStringLiteral("command") : QStringLiteral("snapshot"), title);
    metric.insert(QStringLiteral("integration"), integration ? QJsonValue(source.integration) : QJsonValue());
    metric.insert(QStringLiteral("state"), stateName(source.state));
    metric.insert(QStringLiteral("message"), source.message);
    QJsonArray items;
    if (source.snapshot) {
        const custommetrics::Snapshot &snapshot = *source.snapshot;
        metric.insert(QStringLiteral("icon"), iconObject(snapshot).isEmpty() ? QJsonValue() : QJsonValue(iconObject(snapshot)));
        if (snapshot.metricsBarValue)
            metric.insert(QStringLiteral("compactValue"), *snapshot.metricsBarValue);
        if (!snapshot.metrics.isEmpty())
            metric.insert(QStringLiteral("formattedValue"), snapshot.metricsBarValue.value_or(snapshot.metrics.first().formattedValue));
        if (snapshot.lastUpdatedDate.isValid())
            metric.insert(QStringLiteral("updatedAt"), snapshot.lastUpdatedDate.toUTC().toString(Qt::ISODateWithMs));
        for (const custommetrics::Item &item : snapshot.metrics) {
            items.append(QJsonObject{
                {QStringLiteral("id"), item.id.isEmpty() ? QJsonValue() : QJsonValue(item.id)},
                {QStringLiteral("title"), item.title},
                {QStringLiteral("formattedValue"), item.formattedValue},
                {QStringLiteral("detail"), item.detail},
                {QStringLiteral("normalized"), item.normalizedValue ? QJsonValue(*item.normalizedValue) : QJsonValue()},
                {QStringLiteral("compactValue"), item.metricsBarValue ? QJsonValue(*item.metricsBarValue) : QJsonValue()},
                {QStringLiteral("compactLabel"), item.metricsBarLabel},
                {QStringLiteral("warning"), item.warning}});
        }
    }
    metric.insert(QStringLiteral("items"), items);
    metric.insert(QStringLiteral("data"), QJsonObject{
        {QStringLiteral("sourceId"), source.id},
        {QStringLiteral("path"), source.path},
        {QStringLiteral("explicitPath"), source.explicitPath},
        {QStringLiteral("explicit"), source.explicitlyRegistered},
        {QStringLiteral("discovered"), source.discovered},
        {QStringLiteral("configuredId"), source.configuredId},
        {QStringLiteral("hasSnapshot"), source.snapshot.has_value()}});
    return metric;
}

QJsonObject settingsObject(const AppSettings &settings)
{
    return QJsonObject{
        {QStringLiteral("runnerId"), settings.runnerId()},
        {QStringLiteral("speedMode"), AppSettings::speedModeName(settings.speedMode())},
        {QStringLiteral("networkMode"), AppSettings::networkModeName(settings.networkMode())},
        {QStringLiteral("networkInterfaces"), QJsonArray::fromStringList(settings.networkInterfaces())},
        {QStringLiteral("dashboardRefreshSeconds"), settings.dashboardRefreshSeconds()},
        {QStringLiteral("autostart"), settings.autostartEnabled()},
        {QStringLiteral("metricsBarEnabled"), settings.metricsBarEnabled()},
        {QStringLiteral("metricsBarItems"), QJsonArray::fromStringList(settings.metricsBarItems())},
        {QStringLiteral("customMetricFiles"), QJsonArray::fromStringList(settings.customMetricFiles())},
    };
}

metrics::SystemMetricsSnapshot systemSnapshotFromMetrics(const QJsonArray &array)
{
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : array) {
        const QJsonObject metric = value.toObject();
        if (metric.value(QStringLiteral("category")) == QLatin1String("builtin"))
            byId.insert(metric.value(QStringLiteral("id")).toString(), metric);
    }
    metrics::SystemMetricsSnapshot s;
    auto data = [&](const char *id) { return byId.value(QLatin1String(id)).value(QStringLiteral("data")).toObject(); };
    auto status = [&](const char *id) {
        return byId.contains(QLatin1String(id)) ? sectionStatus(byId.value(QLatin1String(id)))
                                                : MetricStatus::pending("waiting for the first sample");
    };
    auto bytes = [](const QJsonObject &object, const char *key) {
        return std::uint64_t(std::max(0.0, object.value(QLatin1String(key)).toDouble()));
    };

    s.cpu.status = status("cpu");
    s.cpu.usagePercent = numberOf(byId.value(QStringLiteral("cpu")).value(QStringLiteral("value")));
    s.cpu.logicalCpus = data("cpu").value(QStringLiteral("logicalCpus")).toInt();

    s.memory.status = status("memory");
    const QJsonObject memory = data("memory");
    if (s.memory.status.ok() && memory.contains(QStringLiteral("totalBytes"))) {
        metrics::MemoryInfo info;
        info.usedBytes = bytes(memory, "usedBytes");
        info.totalBytes = bytes(memory, "totalBytes");
        info.availableBytes = bytes(memory, "availableBytes");
        info.availableIsEstimate = memory.value(QStringLiteral("availableIsEstimate")).toBool();
        info.usagePercent = byId.value(QStringLiteral("memory")).value(QStringLiteral("value")).toDouble();
        const QJsonObject swap = data("swap");
        info.swapPresent = swap.value(QStringLiteral("present")).toBool();
        info.swapUsedBytes = bytes(swap, "usedBytes");
        info.swapTotalBytes = bytes(swap, "totalBytes");
        info.swapUsagePercent = byId.value(QStringLiteral("swap")).value(QStringLiteral("value")).toDouble();
        s.memory.info = info;
    }

    s.network.status = status("network.download");
    const QJsonObject network = data("network.download");
    s.network.countedInterfaces = network.value(QStringLiteral("countedInterfaces")).toInt();
    s.network.downloadBytesPerSecond = numberOf(byId.value(QStringLiteral("network.download")).value(QStringLiteral("value")));
    s.network.uploadBytesPerSecond = numberOf(byId.value(QStringLiteral("network.upload")).value(QStringLiteral("value")));
    for (const QJsonValue &value : network.value(QStringLiteral("interfaces")).toArray()) {
        const QJsonObject i = value.toObject();
        metrics::InterfaceMetrics entry;
        entry.name = i.value(QStringLiteral("name")).toString().toStdString();
        entry.kind = interfaceKind(i.value(QStringLiteral("kind")).toString());
        entry.counted = i.value(QStringLiteral("counted")).toBool();
        entry.downloadBytesPerSecond = numberOf(i.value(QStringLiteral("downloadBytesPerSecond")));
        entry.uploadBytesPerSecond = numberOf(i.value(QStringLiteral("uploadBytesPerSecond")));
        entry.counterReset = i.value(QStringLiteral("counterReset")).toBool();
        s.network.interfaces.push_back(std::move(entry));
    }

    for (const char *id : {"storage.root", "storage.home"}) {
        if (!byId.contains(QLatin1String(id)))
            continue;
        const QJsonObject storage = data(id);
        metrics::StorageMetrics entry;
        entry.path = storage.value(QStringLiteral("path")).toString().toStdString();
        entry.status = status(id);
        if (entry.status.ok() && storage.contains(QStringLiteral("totalBytes"))) {
            metrics::StorageInfo info;
            info.path = entry.path;
            info.usedBytes = bytes(storage, "usedBytes");
            info.totalBytes = bytes(storage, "totalBytes");
            info.availableBytes = bytes(storage, "availableBytes");
            info.usagePercent = byId.value(QLatin1String(id)).value(QStringLiteral("value")).toDouble();
            entry.info = info;
        }
        s.storage.push_back(std::move(entry));
    }

    s.diskIo.status = status("disk.read");
    s.diskIo.countedDevices = data("disk.read").value(QStringLiteral("countedDevices")).toInt();
    s.diskIo.readBytesPerSecond = numberOf(byId.value(QStringLiteral("disk.read")).value(QStringLiteral("value")));
    s.diskIo.writeBytesPerSecond = numberOf(byId.value(QStringLiteral("disk.write")).value(QStringLiteral("value")));
    return s;
}

QList<custommetrics::Source> sourcesFromMetrics(const QJsonArray &array)
{
    QList<custommetrics::Source> sources;
    for (const QJsonValue &value : array) {
        const QJsonObject metric = value.toObject();
        const QString category = metric.value(QStringLiteral("category")).toString();
        if (category != QLatin1String("custom") && category != QLatin1String("integration"))
            continue;
        const QJsonObject data = metric.value(QStringLiteral("data")).toObject();
        custommetrics::Source source;
        source.id = data.value(QStringLiteral("sourceId")).toString();
        source.path = data.value(QStringLiteral("path")).toString();
        source.explicitPath = data.value(QStringLiteral("explicitPath")).toString();
        source.explicitlyRegistered = data.value(QStringLiteral("explicit")).toBool();
        source.discovered = data.value(QStringLiteral("discovered")).toBool();
        source.configuredId = data.value(QStringLiteral("configuredId")).toString();
        source.integration = metric.value(QStringLiteral("integration")).toString();
        source.type = metric.value(QStringLiteral("kind")) == QLatin1String("command") ? custommetrics::SourceType::Command
                                                                                     : custommetrics::SourceType::Snapshot;
        const QString state = metric.value(QStringLiteral("state")).toString();
        source.state = state == QLatin1String("ok")            ? custommetrics::State::Available
            : state == QLatin1String("pending")                ? custommetrics::State::Pending
            : state == QLatin1String("error")                  ? custommetrics::State::Error
                                                               : custommetrics::State::Unavailable;
        source.message = metric.value(QStringLiteral("message")).toString();
        if (data.value(QStringLiteral("hasSnapshot")).toBool()) {
            custommetrics::Snapshot snapshot;
            snapshot.title = metric.value(QStringLiteral("title")).toString();
            const QJsonObject icon = metric.value(QStringLiteral("icon")).toObject();
            const QString iconType = icon.value(QStringLiteral("type")).toString();
            if (iconType == QLatin1String("symbol")) {
                snapshot.symbol = icon.value(QStringLiteral("value")).toString();
            } else if (iconType == QLatin1String("theme") || iconType == QLatin1String("file")) {
                custommetrics::IconReference reference;
                reference.type = iconType == QLatin1String("file") ? custommetrics::IconType::File
                                                                   : custommetrics::IconType::Theme;
                reference.value = icon.value(QStringLiteral("value")).toString();
                reference.resolvedValue = icon.value(QStringLiteral("resolved")).toString();
                snapshot.icon = reference;
                if (reference.type == custommetrics::IconType::Theme)
                    snapshot.symbol = reference.value; // same as the F1 parser
            }
            const QJsonValue compact = metric.value(QStringLiteral("compactValue"));
            if (compact.isString())
                snapshot.metricsBarValue = compact.toString();
            snapshot.lastUpdatedDate = QDateTime::fromString(metric.value(QStringLiteral("updatedAt")).toString(),
                                                             Qt::ISODateWithMs);
            for (const QJsonValue &itemValue : metric.value(QStringLiteral("items")).toArray()) {
                const QJsonObject i = itemValue.toObject();
                custommetrics::Item item;
                item.id = i.value(QStringLiteral("id")).toString();
                item.title = i.value(QStringLiteral("title")).toString();
                item.formattedValue = i.value(QStringLiteral("formattedValue")).toString();
                item.detail = i.value(QStringLiteral("detail")).toString();
                item.normalizedValue = numberOf(i.value(QStringLiteral("normalized")));
                if (i.value(QStringLiteral("compactValue")).isString())
                    item.metricsBarValue = i.value(QStringLiteral("compactValue")).toString();
                item.metricsBarLabel = i.value(QStringLiteral("compactLabel")).toString();
                item.warning = i.value(QStringLiteral("warning")).toBool();
                snapshot.metrics.append(item);
            }
            source.snapshot = snapshot;
        }
        sources.append(source);
    }
    return sources;
}

} // namespace aqtruncat::api
