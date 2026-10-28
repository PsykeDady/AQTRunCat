#include "apischema.h"

#include "app/appsettings.h"
#include "app/hoversummary.h"
#include "app/metricsbarids.h"
#include "metrics/compactformat.h"
#include "metrics/metricsformat.h"
#include "metrics/uptime.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QTimeZone>

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

// --- F5 -----------------------------------------------------------------

QString epochText(const std::optional<std::int64_t> &seconds)
{
    return seconds ? QDateTime::fromSecsSinceEpoch(*seconds, QTimeZone::UTC).toString(Qt::ISODate) : QString();
}

QJsonValue epochValue(const std::optional<std::int64_t> &seconds)
{
    return seconds ? QJsonValue(epochText(seconds)) : QJsonValue();
}

std::optional<std::int64_t> epochOf(const QJsonValue &value)
{
    const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODate);
    return time.isValid() ? std::optional<std::int64_t>(time.toSecsSinceEpoch()) : std::nullopt;
}

// "10:32" today, "Oct 16, 10:32" otherwise (local time, English month).
QString hexId(std::uint32_t id)
{
    return QStringLiteral("%1").arg(id, 4, 16, QLatin1Char('0'));
}

QJsonObject fieldObject(const MetricStatus &status)
{
    return sectionObject(status);
}

QJsonObject gpuItem(const QString &id, const QString &title, const MetricStatus &status, const QString &value,
                    const QString &detail, const QJsonValue &normalized, const std::optional<std::string> &compact,
                    const QString &label)
{
    const bool ok = status.ok();
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("state"), stateName(status.state)},
                       {QStringLiteral("formattedValue"), ok ? value : QStringLiteral("N/A")},
                       {QStringLiteral("detail"), ok ? detail : q(status.message)},
                       {QStringLiteral("normalized"), ok ? normalized : QJsonValue()},
                       {QStringLiteral("compactValue"), ok && compact ? QJsonValue(q(*compact)) : QJsonValue()},
                       {QStringLiteral("compactLabel"), label},
                       {QStringLiteral("warning"), false}};
}

QJsonObject gpuMetric(const metrics::GpuDevice &gpu)
{
    QJsonObject metric = metricObject(QStringLiteral("gpu:") + q(gpu.id), QStringLiteral("builtin"), QStringLiteral("system"),
                                      q(gpu.model));
    metric.insert(QStringLiteral("compactLabel"), QStringLiteral("GPU"));
    metric.insert(QStringLiteral("unit"), QStringLiteral("percent"));
    const bool intel = gpu.utilizationMethod == "rc6-residency";
    QJsonArray items;
    items.append(gpuItem(QStringLiteral("utilization"), intel ? QStringLiteral("Activity") : QStringLiteral("Utilization"),
                         gpu.utilizationStatus,
                         gpu.utilizationPercent ? q(metrics::formatPercent(*gpu.utilizationPercent)) : QString(),
                         intel ? QStringLiteral("time out of the RC6 power-saving state") : QString(),
                         gpu.utilizationPercent ? QJsonValue(std::clamp(*gpu.utilizationPercent / 100.0, 0.0, 1.0)) : QJsonValue(),
                         gpu.utilizationPercent ? metrics::compactPercent(*gpu.utilizationPercent) : std::nullopt,
                         QStringLiteral("GPU")));
    const bool memory = gpu.memoryUsedBytes && gpu.memoryTotalBytes && *gpu.memoryTotalBytes > 0;
    const double memoryPercent = memory ? 100.0 * double(*gpu.memoryUsedBytes) / double(*gpu.memoryTotalBytes) : 0.0;
    items.append(gpuItem(QStringLiteral("memory"), QStringLiteral("Memory"), gpu.memoryStatus,
                         memory ? q(metrics::formatBytes(*gpu.memoryUsedBytes)) + QStringLiteral(" / ")
                                      + q(metrics::formatBytes(*gpu.memoryTotalBytes))
                                : QString(),
                         memory ? q(metrics::formatPercent(memoryPercent)) + QStringLiteral(" used") : QString(),
                         memory ? QJsonValue(std::clamp(memoryPercent / 100.0, 0.0, 1.0)) : QJsonValue(),
                         memory ? metrics::compactPercent(memoryPercent) : std::nullopt, QStringLiteral("VRAM")));
    items.append(gpuItem(QStringLiteral("temperature"), QStringLiteral("Temperature"), gpu.temperatureStatus,
                         gpu.temperatureCelsius ? q(metrics::formatCelsius(*gpu.temperatureCelsius)) : QString(),
                         gpu.temperatureSensor.empty() || gpu.temperatureSensor == "nvml" ? QString()
                                                                                         : QStringLiteral("sensor ") + q(gpu.temperatureSensor),
                         QJsonValue(), gpu.temperatureCelsius ? metrics::compactCelsius(*gpu.temperatureCelsius) : std::nullopt,
                         QStringLiteral("GPU°")));
    metric.insert(QStringLiteral("items"), items);

    // One state for the whole device: ok when any field has a value.
    const std::vector<const MetricStatus *> fields{&gpu.utilizationStatus, &gpu.memoryStatus, &gpu.temperatureStatus};
    const bool anyOk = std::any_of(fields.begin(), fields.end(), [](const MetricStatus *f) { return f->ok(); });
    const bool anyPending = std::any_of(fields.begin(), fields.end(),
                                        [](const MetricStatus *f) { return f->state == MetricState::Pending; });
    if (anyOk) {
        metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
    } else if (anyPending) {
        setStatus(metric, MetricStatus::pending("waiting for the next sample"));
    } else {
        setStatus(metric, gpu.suspended ? gpu.utilizationStatus
                                        : MetricStatus::unavailable("no telemetry from the " + gpu.driver + " driver"));
    }
    if (gpu.utilizationStatus.ok() && gpu.utilizationPercent) {
        metric.insert(QStringLiteral("value"), *gpu.utilizationPercent);
        metric.insert(QStringLiteral("formattedValue"), q(metrics::formatPercent(*gpu.utilizationPercent)));
        // No "normalized" here: each item has its own bar (no duplicate).
        metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactPercent(*gpu.utilizationPercent)));
    } else if (anyOk) {
        metric.insert(QStringLiteral("formattedValue"), QStringLiteral("N/A"));
    }
    QStringList detail;
    if (!gpu.driver.empty())
        detail << q(gpu.driver);
    if (gpu.integrated)
        detail << (*gpu.integrated ? QStringLiteral("integrated") : QStringLiteral("discrete"));
    if (gpu.frequencyMHz && *gpu.frequencyMHz > 0)
        detail << QStringLiteral("%1 MHz").arg(qRound(*gpu.frequencyMHz));
    if (gpu.suspended)
        detail << QStringLiteral("suspended");
    if (gpu.selected)
        detail << QStringLiteral("Metrics Bar GPU");
    metric.insert(QStringLiteral("detail"), detail.join(QStringLiteral(" · ")));

    auto optionalUnsigned = [](const std::optional<std::uint64_t> &value) {
        return value ? QJsonValue(double(*value)) : QJsonValue();
    };
    QJsonObject utilization = fieldObject(gpu.utilizationStatus);
    utilization.insert(QStringLiteral("percent"), optionalNumber(gpu.utilizationPercent));
    utilization.insert(QStringLiteral("method"), q(gpu.utilizationMethod));
    QJsonObject memoryData = fieldObject(gpu.memoryStatus);
    memoryData.insert(QStringLiteral("usedBytes"), optionalUnsigned(gpu.memoryUsedBytes));
    memoryData.insert(QStringLiteral("totalBytes"), optionalUnsigned(gpu.memoryTotalBytes));
    QJsonObject temperature = fieldObject(gpu.temperatureStatus);
    temperature.insert(QStringLiteral("celsius"), optionalNumber(gpu.temperatureCelsius));
    temperature.insert(QStringLiteral("sensor"), q(gpu.temperatureSensor));
    metric.insert(QStringLiteral("data"), QJsonObject{
        {QStringLiteral("section"), sectionObject(MetricStatus::okStatus())},
        {QStringLiteral("device"), q(gpu.id)},
        {QStringLiteral("card"), q(gpu.card)},
        {QStringLiteral("driver"), q(gpu.driver)},
        {QStringLiteral("vendor"), q(gpu.vendor)},
        {QStringLiteral("vendorId"), hexId(gpu.vendorId)},
        {QStringLiteral("deviceId"), hexId(gpu.deviceId)},
        {QStringLiteral("model"), q(gpu.model)},
        {QStringLiteral("integrated"), gpu.integrated ? QJsonValue(*gpu.integrated) : QJsonValue()},
        {QStringLiteral("bootVga"), gpu.bootVga},
        {QStringLiteral("suspended"), gpu.suspended},
        {QStringLiteral("selected"), gpu.selected},
        {QStringLiteral("frequencyMHz"), optionalNumber(gpu.frequencyMHz)},
        {QStringLiteral("utilization"), utilization},
        {QStringLiteral("memory"), memoryData},
        {QStringLiteral("temperature"), temperature}});
    return metric;
}

QString fpsCondition(metrics::FpsCondition condition)
{
    switch (condition) {
    case metrics::FpsCondition::Measured: return QStringLiteral("measured");
    case metrics::FpsCondition::Stale: return QStringLiteral("stale");
    case metrics::FpsCondition::Inactive: return QStringLiteral("inactive");
    case metrics::FpsCondition::NotConfigured: return QStringLiteral("not-configured");
    case metrics::FpsCondition::Missing: return QStringLiteral("missing");
    case metrics::FpsCondition::Disabled: return QStringLiteral("disabled");
    case metrics::FpsCondition::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("unsupported");
}

metrics::FpsCondition fpsConditionOf(const QString &name)
{
    using metrics::FpsCondition;
    static const QHash<QString, FpsCondition> names{
        {QStringLiteral("measured"), FpsCondition::Measured}, {QStringLiteral("stale"), FpsCondition::Stale},
        {QStringLiteral("inactive"), FpsCondition::Inactive}, {QStringLiteral("not-configured"), FpsCondition::NotConfigured},
        {QStringLiteral("missing"), FpsCondition::Missing}, {QStringLiteral("disabled"), FpsCondition::Disabled}};
    return names.value(name, FpsCondition::Unsupported);
}

void fillFps(QJsonObject &metric, QJsonObject &data, const metrics::FpsMetrics &fps)
{
    metric.insert(QStringLiteral("unit"), QStringLiteral("fps"));
    data.insert(QStringLiteral("source"), q(fps.source));
    data.insert(QStringLiteral("condition"), fpsCondition(fps.condition));
    data.insert(QStringLiteral("fps"), optionalNumber(fps.fps));
    data.insert(QStringLiteral("frametimeMs"), optionalNumber(fps.frametimeMs));
    data.insert(QStringLiteral("lastFps"), optionalNumber(fps.lastFps));
    data.insert(QStringLiteral("ageSeconds"), optionalNumber(fps.ageSeconds));
    data.insert(QStringLiteral("application"), q(fps.application));
    if (!fps.status.ok() || !fps.fps) {
        setStatus(metric, fps.status);
        if (fps.condition == metrics::FpsCondition::Stale && fps.lastFps)
            metric.insert(QStringLiteral("detail"), QStringLiteral("Last reading: %1%2")
                                                        .arg(q(metrics::formatFps(*fps.lastFps)),
                                                             fps.application.empty() ? QString() : QStringLiteral(" (") + q(fps.application) + u')'));
        return;
    }
    metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
    metric.insert(QStringLiteral("value"), *fps.fps);
    metric.insert(QStringLiteral("formattedValue"), q(metrics::formatFps(*fps.fps)));
    metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactWhole(*fps.fps)));
    QStringList detail{QStringLiteral("MangoHud")};
    if (!fps.application.empty())
        detail << q(fps.application);
    if (fps.frametimeMs)
        detail << QStringLiteral("%1 ms frame time").arg(*fps.frametimeMs, 0, 'f', 1);
    metric.insert(QStringLiteral("detail"), detail.join(QStringLiteral(" · ")));
}

void fillUpdates(QJsonObject &metric, QJsonObject &data, const metrics::UpdatesMetrics &updates)
{
    metric.insert(QStringLiteral("unit"), QStringLiteral("count"));
    data.insert(QStringLiteral("manager"), q(updates.manager));
    data.insert(QStringLiteral("count"), updates.count ? QJsonValue(*updates.count) : QJsonValue());
    data.insert(QStringLiteral("checking"), updates.checking);
    data.insert(QStringLiteral("lastSuccess"), epochValue(updates.lastSuccess));
    data.insert(QStringLiteral("metadataTime"), epochValue(updates.metadataTime));
    data.insert(QStringLiteral("metadataStale"), updates.metadataStale);
    data.insert(QStringLiteral("remote"), updates.remote);
    data.insert(QStringLiteral("note"), q(updates.note));
    data.insert(QStringLiteral("ignored"), updates.ignored);
    if (updates.manager == "flatpak") {
        data.insert(QStringLiteral("apps"), updates.apps);
        data.insert(QStringLiteral("runtimes"), updates.runtimes);
        QJsonArray installations;
        for (const metrics::FlatpakInstallationCount &entry : updates.installations) {
            installations.append(QJsonObject{{QStringLiteral("name"), q(entry.name)},
                                             {QStringLiteral("state"), stateName(entry.status.state)},
                                             {QStringLiteral("message"), q(entry.status.message)},
                                             {QStringLiteral("apps"), entry.apps},
                                             {QStringLiteral("runtimes"), entry.runtimes}});
        }
        data.insert(QStringLiteral("installations"), installations);
    }
    QStringList detail;
    if (!updates.manager.empty() && updates.manager != "flatpak")
        detail << q(updates.manager);
    if (updates.manager == "flatpak" && updates.count && *updates.count > 0)
        detail << QStringLiteral("%1 %2, %3 %4").arg(updates.apps).arg(updates.apps == 1 ? QStringLiteral("app") : QStringLiteral("apps"))
                      .arg(updates.runtimes).arg(updates.runtimes == 1 ? QStringLiteral("runtime") : QStringLiteral("runtimes"));
    if (updates.ignored > 0)
        detail << QStringLiteral("%1 ignored").arg(updates.ignored);
    if (updates.lastSuccess)
        detail << QStringLiteral("checked %1").arg(aqtruncat::hover::whenText(*updates.lastSuccess));
    if (updates.metadataTime && !updates.remote)
        detail << QStringLiteral("metadata from %1%2").arg(aqtruncat::hover::whenText(*updates.metadataTime),
                                                         updates.metadataStale ? QStringLiteral(" (out of date)") : QString());
    if (updates.checking)
        detail << QStringLiteral("checking…");
    if (!updates.note.empty())
        detail << q(updates.note);
    metric.insert(QStringLiteral("detail"), detail.join(QStringLiteral(" · ")));
    if (!updates.status.ok() || !updates.count) {
        setStatus(metric, updates.status);
        return;
    }
    metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
    metric.insert(QStringLiteral("value"), *updates.count);
    metric.insert(QStringLiteral("formattedValue"), q(metrics::formatUpdateCount(*updates.count)));
    metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactCount(*updates.count)));
}

metrics::UpdatesMetrics updatesOf(const QJsonObject &metric, const MetricStatus &status)
{
    const QJsonObject data = metric.value(QStringLiteral("data")).toObject();
    metrics::UpdatesMetrics updates;
    updates.status = status;
    updates.manager = data.value(QStringLiteral("manager")).toString().toStdString();
    if (status.ok() && data.value(QStringLiteral("count")).isDouble())
        updates.count = data.value(QStringLiteral("count")).toInt();
    updates.checking = data.value(QStringLiteral("checking")).toBool();
    updates.lastSuccess = epochOf(data.value(QStringLiteral("lastSuccess")));
    updates.metadataTime = epochOf(data.value(QStringLiteral("metadataTime")));
    updates.metadataStale = data.value(QStringLiteral("metadataStale")).toBool();
    updates.remote = data.value(QStringLiteral("remote")).toBool();
    updates.note = data.value(QStringLiteral("note")).toString().toStdString();
    updates.ignored = data.value(QStringLiteral("ignored")).toInt();
    updates.apps = data.value(QStringLiteral("apps")).toInt();
    updates.runtimes = data.value(QStringLiteral("runtimes")).toInt();
    for (const QJsonValue &value : data.value(QStringLiteral("installations")).toArray()) {
        const QJsonObject entry = value.toObject();
        metrics::FlatpakInstallationCount count;
        count.name = entry.value(QStringLiteral("name")).toString().toStdString();
        const QString state = entry.value(QStringLiteral("state")).toString();
        const std::string message = entry.value(QStringLiteral("message")).toString().toStdString();
        count.status = state == QLatin1String("ok") ? MetricStatus::okStatus() : MetricStatus::error(message);
        count.apps = entry.value(QStringLiteral("apps")).toInt();
        count.runtimes = entry.value(QStringLiteral("runtimes")).toInt();
        updates.installations.push_back(count);
    }
    return updates;
}

MetricStatus fieldStatus(const QJsonObject &field)
{
    const QString state = field.value(QStringLiteral("state")).toString();
    const std::string message = field.value(QStringLiteral("message")).toString().toStdString();
    if (state == QLatin1String("ok"))
        return MetricStatus::okStatus();
    if (state == QLatin1String("pending"))
        return MetricStatus::pending(message);
    if (state == QLatin1String("error"))
        return MetricStatus::error(message);
    return MetricStatus::unavailable(message);
}

metrics::GpuDevice gpuOf(const QJsonObject &metric)
{
    const QJsonObject data = metric.value(QStringLiteral("data")).toObject();
    metrics::GpuDevice gpu;
    gpu.id = data.value(QStringLiteral("device")).toString().toStdString();
    gpu.card = data.value(QStringLiteral("card")).toString().toStdString();
    gpu.driver = data.value(QStringLiteral("driver")).toString().toStdString();
    gpu.vendor = data.value(QStringLiteral("vendor")).toString().toStdString();
    gpu.vendorId = data.value(QStringLiteral("vendorId")).toString().toUInt(nullptr, 16);
    gpu.deviceId = data.value(QStringLiteral("deviceId")).toString().toUInt(nullptr, 16);
    gpu.model = data.value(QStringLiteral("model")).toString().toStdString();
    if (data.value(QStringLiteral("integrated")).isBool())
        gpu.integrated = data.value(QStringLiteral("integrated")).toBool();
    gpu.bootVga = data.value(QStringLiteral("bootVga")).toBool();
    gpu.suspended = data.value(QStringLiteral("suspended")).toBool();
    gpu.selected = data.value(QStringLiteral("selected")).toBool();
    gpu.frequencyMHz = numberOf(data.value(QStringLiteral("frequencyMHz")));
    const QJsonObject utilization = data.value(QStringLiteral("utilization")).toObject();
    gpu.utilizationStatus = fieldStatus(utilization);
    gpu.utilizationPercent = numberOf(utilization.value(QStringLiteral("percent")));
    gpu.utilizationMethod = utilization.value(QStringLiteral("method")).toString().toStdString();
    const QJsonObject memory = data.value(QStringLiteral("memory")).toObject();
    gpu.memoryStatus = fieldStatus(memory);
    if (const auto used = numberOf(memory.value(QStringLiteral("usedBytes"))))
        gpu.memoryUsedBytes = std::uint64_t(std::max(0.0, *used));
    if (const auto total = numberOf(memory.value(QStringLiteral("totalBytes"))))
        gpu.memoryTotalBytes = std::uint64_t(std::max(0.0, *total));
    const QJsonObject temperature = data.value(QStringLiteral("temperature")).toObject();
    gpu.temperatureStatus = fieldStatus(temperature);
    gpu.temperatureCelsius = numberOf(temperature.value(QStringLiteral("celsius")));
    gpu.temperatureSensor = temperature.value(QStringLiteral("sensor")).toString().toStdString();
    return gpu;
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
        // GPUs: one object per device ("gpu:<device id>"), where the bar
        // lists its three selected-GPU entries.
        if (id == QLatin1String("gpu.memory") || id == QLatin1String("gpu.temperature"))
            continue;
        if (id == QLatin1String("gpu.utilization")) {
            if (snapshot) {
                for (const metrics::GpuDevice &gpu : snapshot->gpu.devices)
                    result.append(gpuMetric(gpu));
            }
            continue;
        }
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
        } else if (id == QLatin1String("cpu.temperature")) {
            const auto &temperature = s.cpuTemperature;
            metric.insert(QStringLiteral("unit"), QStringLiteral("celsius"));
            data = {{QStringLiteral("section"), sectionObject(temperature.status)},
                    {QStringLiteral("sensor"), q(temperature.sensor)}};
            if (temperature.status.ok() && temperature.celsius) {
                metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
                metric.insert(QStringLiteral("value"), *temperature.celsius);
                metric.insert(QStringLiteral("formattedValue"), q(metrics::formatCelsiusPrecise(*temperature.celsius)));
                metric.insert(QStringLiteral("compactValue"), optionalString(metrics::compactCelsius(*temperature.celsius)));
                // Keep the kernel sensor label in structured data for diagnostics,
                // but don't surface labels such as "Package id 0" as user-facing detail.
                metric.insert(QStringLiteral("detail"), QString());
            } else {
                setStatus(metric, temperature.status);
            }
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
                    metric.insert(QStringLiteral("detail"), q(metrics::formatMemoryDetail(m)));
                } else {
                    data.insert(QStringLiteral("present"), m.swapPresent);
                    data.insert(QStringLiteral("usedBytes"), double(m.swapUsedBytes));
                    data.insert(QStringLiteral("totalBytes"), double(m.swapTotalBytes));
                    if (m.swapPresent) {
                        setPercent(metric, m.swapUsagePercent);
                        metric.insert(QStringLiteral("detail"), q(metrics::formatSwapDetail(m)));
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
        } else if (id == QLatin1String("uptime")) {
            data = {{QStringLiteral("section"), sectionObject(s.uptime.status)},
                    {QStringLiteral("seconds"), optionalNumber(s.uptime.seconds)}};
            metric.insert(QStringLiteral("unit"), QStringLiteral("seconds"));
            if (s.uptime.status.ok() && s.uptime.seconds) {
                metric.insert(QStringLiteral("state"), QStringLiteral("ok"));
                metric.insert(QStringLiteral("value"), std::floor(*s.uptime.seconds));
                metric.insert(QStringLiteral("formattedValue"), q(metrics::formatDuration(*s.uptime.seconds)));
                metric.insert(QStringLiteral("compactValue"), q(metrics::compactDuration(*s.uptime.seconds)));
                metric.insert(QStringLiteral("detail"), QStringLiteral("since boot"));
            } else {
                setStatus(metric, s.uptime.status);
            }
        } else if (id == QLatin1String("fps")) {
            data = {{QStringLiteral("section"), sectionObject(s.fps.status)}};
            fillFps(metric, data, s.fps);
        } else if (id == QLatin1String("updates.packages") || id == QLatin1String("updates.flatpak")) {
            const metrics::UpdatesMetrics &updates = id == QLatin1String("updates.packages") ? s.packageUpdates : s.flatpakUpdates;
            data = {{QStringLiteral("section"), sectionObject(updates.status)}};
            fillUpdates(metric, data, updates);
        }
        metric.insert(QStringLiteral("data"), data);
        result.append(metric);
    }

    // Informational text: no value, no unit, no Metrics Bar entry.
    QJsonObject kernel = metricObject(QStringLiteral("kernel"), QStringLiteral("builtin"), QStringLiteral("system"),
                                      QStringLiteral("Kernel"));
    QJsonObject distribution = metricObject(QStringLiteral("distribution"), QStringLiteral("builtin"),
                                            QStringLiteral("system"), QStringLiteral("Distribution"));
    if (!snapshot) {
        for (QJsonObject *metric : {&kernel, &distribution})
            metric->insert(QStringLiteral("message"), QStringLiteral("waiting for the first sample"));
    } else {
        const metrics::KernelMetrics &k = snapshot->kernel;
        QJsonObject kernelData{{QStringLiteral("section"), sectionObject(k.status)}};
        if (k.status.ok() && k.info) {
            kernelData.insert(QStringLiteral("sysname"), q(k.info->sysname));
            kernelData.insert(QStringLiteral("release"), q(k.info->release));
            kernelData.insert(QStringLiteral("version"), q(k.info->version));
            kernelData.insert(QStringLiteral("machine"), q(k.info->machine));
            kernel.insert(QStringLiteral("state"), QStringLiteral("ok"));
            kernel.insert(QStringLiteral("formattedValue"), q(k.info->sysname + " " + k.info->release));
            kernel.insert(QStringLiteral("detail"), q(k.info->machine));
        } else {
            setStatus(kernel, k.status);
        }
        kernel.insert(QStringLiteral("data"), kernelData);

        const metrics::DistributionMetrics &d = snapshot->distribution;
        QJsonObject distributionData{{QStringLiteral("section"), sectionObject(d.status)}};
        if (d.status.ok() && d.info) {
            const metrics::DistributionInfo &info = *d.info;
            QJsonArray idLike;
            for (const std::string &like : info.idLike)
                idLike.append(q(like));
            distributionData.insert(QStringLiteral("name"), q(info.name));
            distributionData.insert(QStringLiteral("prettyName"), q(info.prettyName));
            distributionData.insert(QStringLiteral("id"), q(info.id));
            distributionData.insert(QStringLiteral("idLike"), idLike);
            distributionData.insert(QStringLiteral("versionId"), q(info.versionId));
            distributionData.insert(QStringLiteral("version"), q(info.version));
            distributionData.insert(QStringLiteral("codename"), q(info.codename));
            distributionData.insert(QStringLiteral("buildId"), q(info.buildId));
            distributionData.insert(QStringLiteral("variant"), q(info.variant));
            distributionData.insert(QStringLiteral("source"), q(info.source));
            distribution.insert(QStringLiteral("state"), QStringLiteral("ok"));
            distribution.insert(QStringLiteral("formattedValue"), q(info.prettyName.empty() ? info.id : info.prettyName));
            QStringList detail;
            if (!info.versionId.empty() && info.prettyName.find(info.versionId) == std::string::npos)
                detail << q(info.versionId);
            if (!info.buildId.empty())
                detail << q(info.buildId);
            if (!info.codename.empty() && info.prettyName.find(info.codename) == std::string::npos)
                detail << q(info.codename);
            if (!info.id.empty())
                detail << QStringLiteral("ID %1").arg(q(info.id));
            distribution.insert(QStringLiteral("detail"), detail.join(QStringLiteral(" · ")));
        } else {
            setStatus(distribution, d.status);
        }
        distribution.insert(QStringLiteral("data"), distributionData);
    }
    result.append(kernel);
    result.append(distribution);
    return result;
}

QJsonObject customMetric(const custommetrics::Source &source)
{
    const bool integration = !source.integration.isEmpty();
    const QString kind = source.type == custommetrics::SourceType::Command ? QStringLiteral("command")
        : source.type == custommetrics::SourceType::Group                  ? QStringLiteral("group") // schema 5
                                                                           : QStringLiteral("snapshot");
    const QString title = source.snapshot ? source.snapshot->title : QFileInfo(source.path).completeBaseName();
    QJsonObject metric = metricObject(metricsbar::customId(source.id),
                                      integration ? QStringLiteral("integration") : QStringLiteral("custom"),
                                      kind, title);
    metric.insert(QStringLiteral("integration"), integration ? QJsonValue(source.integration) : QJsonValue());
    metric.insert(QStringLiteral("enabled"), source.enabled); // schema 3
    metric.insert(QStringLiteral("showBar"), !source.snapshot || source.snapshot->showBar); // schema 4
    metric.insert(QStringLiteral("state"), stateName(source.state));
    metric.insert(QStringLiteral("message"), source.message);
    metric.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(source.warnings)); // schema 6
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
        {QStringLiteral("hasSnapshot"), source.snapshot.has_value()},
        // schema 5: the group a child belongs to, and its own "enabled"
        {QStringLiteral("groupId"), source.parentId},
        {QStringLiteral("configuredEnabled"), source.configuredEnabled}});
    return metric;
}

namespace {
QJsonArray reloadEntries(const QList<custommetrics::ReloadEntry> &entries)
{
    QJsonArray array;
    for (const custommetrics::ReloadEntry &entry : entries) {
        array.append(QJsonObject{{QStringLiteral("id"), metricsbar::customId(entry.id)},
                                 {QStringLiteral("sourceId"), entry.id},
                                 {QStringLiteral("name"), entry.name},
                                 {QStringLiteral("message"), entry.message}});
    }
    return array;
}

QList<custommetrics::ReloadEntry> reloadEntriesFromJson(const QJsonValue &value)
{
    QList<custommetrics::ReloadEntry> entries;
    for (const QJsonValue &entry : value.toArray()) {
        const QJsonObject object = entry.toObject();
        entries.append({object.value(QStringLiteral("sourceId")).toString(),
                        object.value(QStringLiteral("name")).toString(),
                        object.value(QStringLiteral("message")).toString()});
    }
    return entries;
}
}

QJsonObject refreshResult(const QString &metricId, const custommetrics::RefreshResult &result)
{
    const auto ids = [](const QStringList &sources) {
        QJsonArray array;
        for (const QString &source : sources)
            array.append(metricsbar::customId(source));
        return array;
    };
    return QJsonObject{{QStringLiteral("id"), metricId},
                       {QStringLiteral("started"), ids(result.started)},
                       {QStringLiteral("coalesced"), ids(result.coalesced)}};
}

custommetrics::RefreshResult refreshResultFromJson(const QJsonObject &object)
{
    const auto sources = [](const QJsonValue &value) {
        QStringList list;
        for (const QJsonValue &id : value.toArray())
            list.append(metricsbar::sourceIdFromCustomId(id.toString()));
        return list;
    };
    custommetrics::RefreshResult result;
    result.status = custommetrics::RefreshStatus::Accepted;
    result.started = sources(object.value(QStringLiteral("started")));
    result.coalesced = sources(object.value(QStringLiteral("coalesced")));
    return result;
}

QJsonObject reloadReport(const custommetrics::ReloadReport &report)
{
    return QJsonObject{{QStringLiteral("added"), reloadEntries(report.added)},
                       {QStringLiteral("changed"), reloadEntries(report.changed)},
                       {QStringLiteral("removed"), reloadEntries(report.removed)},
                       {QStringLiteral("unchanged"), report.unchanged},
                       {QStringLiteral("errors"), reloadEntries(report.errors)},
                       {QStringLiteral("warnings"), reloadEntries(report.warnings)}};
}

custommetrics::ReloadReport reloadReportFromJson(const QJsonObject &object)
{
    custommetrics::ReloadReport report;
    report.ok = true;
    report.added = reloadEntriesFromJson(object.value(QStringLiteral("added")));
    report.changed = reloadEntriesFromJson(object.value(QStringLiteral("changed")));
    report.removed = reloadEntriesFromJson(object.value(QStringLiteral("removed")));
    report.errors = reloadEntriesFromJson(object.value(QStringLiteral("errors")));
    report.warnings = reloadEntriesFromJson(object.value(QStringLiteral("warnings")));
    report.unchanged = object.value(QStringLiteral("unchanged")).toInt();
    return report;
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
        {QStringLiteral("gpuDevice"), settings.gpuDevice()},
        {QStringLiteral("fpsSource"), settings.fpsSource()},
        {QStringLiteral("updatesIntervalMinutes"), settings.updatesIntervalMinutes()},
        {QStringLiteral("updatesRemoteRefresh"), settings.updatesRemoteRefresh()},
        {QStringLiteral("showCpuTemperature"), settings.showCpuTemperature()},
        {QStringLiteral("showGpuMemory"), settings.showGpuMemory()},
        {QStringLiteral("showGpuTemperature"), settings.showGpuTemperature()},
        {QStringLiteral("showFps"), settings.showFps()},
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

    s.cpuTemperature.status = status("cpu.temperature");
    if (s.cpuTemperature.status.ok()) {
        s.cpuTemperature.celsius = numberOf(byId.value(QStringLiteral("cpu.temperature")).value(QStringLiteral("value")));
        s.cpuTemperature.sensor = data("cpu.temperature").value(QStringLiteral("sensor")).toString().toStdString();
        if (!s.cpuTemperature.celsius)
            s.cpuTemperature.status = MetricStatus::error("CPU temperature payload has no numeric value");
    }

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

    // F5 (schema 2; an older backend has none of these: they stay pending).
    s.uptime.status = status("uptime");
    if (s.uptime.status.ok())
        s.uptime.seconds = numberOf(data("uptime").value(QStringLiteral("seconds")));

    for (const QJsonValue &value : array) {
        const QJsonObject metric = value.toObject();
        if (metric.value(QStringLiteral("category")) == QLatin1String("builtin")
            && metric.value(QStringLiteral("id")).toString().startsWith(QLatin1String("gpu:")))
            s.gpu.devices.push_back(gpuOf(metric));
    }
    s.gpu.status = !s.gpu.devices.empty() ? MetricStatus::okStatus()
        : byId.contains(QStringLiteral("uptime")) && byId.value(QStringLiteral("uptime")).value(QStringLiteral("state")) != QLatin1String("pending")
            ? MetricStatus::unavailable("no GPU found")
            : MetricStatus::pending("waiting for the first sample");

    s.fps.status = status("fps");
    const QJsonObject fps = data("fps");
    s.fps.source = fps.value(QStringLiteral("source")).toString().toStdString();
    s.fps.condition = fpsConditionOf(fps.value(QStringLiteral("condition")).toString());
    s.fps.fps = s.fps.status.ok() ? numberOf(fps.value(QStringLiteral("fps"))) : std::nullopt;
    s.fps.frametimeMs = numberOf(fps.value(QStringLiteral("frametimeMs")));
    s.fps.lastFps = numberOf(fps.value(QStringLiteral("lastFps")));
    s.fps.ageSeconds = numberOf(fps.value(QStringLiteral("ageSeconds")));
    s.fps.application = fps.value(QStringLiteral("application")).toString().toStdString();

    s.packageUpdates = updatesOf(byId.value(QStringLiteral("updates.packages")), status("updates.packages"));
    s.flatpakUpdates = updatesOf(byId.value(QStringLiteral("updates.flatpak")), status("updates.flatpak"));

    s.kernel.status = status("kernel");
    if (s.kernel.status.ok()) {
        const QJsonObject kernel = data("kernel");
        s.kernel.info = metrics::KernelInfo{kernel.value(QStringLiteral("sysname")).toString().toStdString(),
                                            kernel.value(QStringLiteral("release")).toString().toStdString(),
                                            kernel.value(QStringLiteral("version")).toString().toStdString(),
                                            kernel.value(QStringLiteral("machine")).toString().toStdString()};
    }
    s.distribution.status = status("distribution");
    if (s.distribution.status.ok()) {
        const QJsonObject d = data("distribution");
        metrics::DistributionInfo info;
        auto text = [&d](const char *key) { return d.value(QLatin1String(key)).toString().toStdString(); };
        info.name = text("name");
        info.prettyName = text("prettyName");
        info.id = text("id");
        for (const QJsonValue &like : d.value(QStringLiteral("idLike")).toArray())
            info.idLike.push_back(like.toString().toStdString());
        info.versionId = text("versionId");
        info.version = text("version");
        info.codename = text("codename");
        info.buildId = text("buildId");
        info.variant = text("variant");
        info.source = text("source");
        s.distribution.info = info;
    }
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
        source.enabled = metric.value(QStringLiteral("enabled")).toBool(true); // absent before schema 3
        const QString kind = metric.value(QStringLiteral("kind")).toString();
        source.type = kind == QLatin1String("command") ? custommetrics::SourceType::Command
            : kind == QLatin1String("group")           ? custommetrics::SourceType::Group
                                                       : custommetrics::SourceType::Snapshot;
        source.parentId = data.value(QStringLiteral("groupId")).toString();
        source.configuredEnabled = data.value(QStringLiteral("configuredEnabled")).toBool(source.enabled);
        const QString state = metric.value(QStringLiteral("state")).toString();
        source.state = state == QLatin1String("ok")            ? custommetrics::State::Available
            : state == QLatin1String("pending")                ? custommetrics::State::Pending
            : state == QLatin1String("error")                  ? custommetrics::State::Error
                                                               : custommetrics::State::Unavailable;
        source.message = metric.value(QStringLiteral("message")).toString();
        for (const QJsonValue &warning : metric.value(QStringLiteral("warnings")).toArray())
            source.warnings.append(warning.toString());
        if (data.value(QStringLiteral("hasSnapshot")).toBool()) {
            custommetrics::Snapshot snapshot;
            snapshot.title = metric.value(QStringLiteral("title")).toString();
            snapshot.showBar = metric.value(QStringLiteral("showBar")).toBool(true); // absent before schema 4
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
