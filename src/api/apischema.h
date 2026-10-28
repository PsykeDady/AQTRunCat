#pragma once

#include "app/custommetricsmodel.h"
#include "custommetrics/custommetric.h"
#include "metrics/systemmetrics.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>
#include <string>

namespace aqtruncat { class AppSettings; }

// F4.1 public API of the AQTRunCat backend (aqtruncatd): identity on the
// D-Bus session bus and the versioned JSON schema of every payload. The
// complete contract is docs/backend-dbus-api-cli.md; this file is
// its single implementation, shared by the backend (to JSON) and the Qt
// frontend (from JSON back to its own types).
namespace aqtruncat::api {

inline const QString ServiceName = QStringLiteral("io.github.psykedady.AQTRunCat");
inline const QString ObjectPath = QStringLiteral("/io/github/psykedady/AQTRunCat");
// The major API version is part of the interface name; additive changes
// keep it and raise SchemaVersion.
inline const QString InterfaceName = QStringLiteral("io.github.psykedady.AQTRunCat1");
inline constexpr int ApiVersion = 1;
inline constexpr int SchemaVersion = 9; // 2 (F5): uptime, GPUs, FPS, updates, kernel, distribution;
                                        // 3 (2.1.2): Custom Metrics "enabled", SetCustomMetricEnabled;
                                        // 4 (2.1.4): Custom Metrics "showBar";
                                        // 5 (2.1.5): kind "group", data.groupId, data.configuredEnabled;
                                        // 6 (2.1.6): ReloadCustomMetrics, Custom Metrics "warnings";
                                        // 7 (2.1.7): RefreshCustomMetric;
                                        // 8 (2.3.2): CPU temperature metric;
                                        // 9: shared CPU/GPU/FPS display visibility settings
inline const QString ErrorInvalidArgument = QStringLiteral("io.github.psykedady.AQTRunCat1.Error.InvalidArgument");
inline const QString ErrorNotFound = QStringLiteral("io.github.psykedady.AQTRunCat1.Error.NotFound");
inline const QString ErrorFailed = QStringLiteral("io.github.psykedady.AQTRunCat1.Error.Failed");

// Built-in metrics: one object per id of metricsbar::builtInMetrics()
// ("cpu", "memory", "swap", "network.download", …; "storage.home" only when
// Home is a filesystem of its own). Before the first sample every metric is
// "pending". homePath decides which storage entry is "storage.home".
QJsonArray builtInMetrics(const std::optional<metrics::SystemMetricsSnapshot> &snapshot, const std::string &homePath);

// A Custom Metrics source: category "integration" when it carries an
// official integration's provenance, else "custom".
QJsonObject customMetric(const custommetrics::Source &source);

// v2F1.6 (schema 6): the answer of ReloadCustomMetrics. added, changed,
// removed and errors are arrays of {id (the metric id), sourceId, name,
// message}; unchanged is a count. A refused reload is a D-Bus error instead.
QJsonObject reloadReport(const custommetrics::ReloadReport &report);
custommetrics::ReloadReport reloadReportFromJson(const QJsonObject &report);

// v2F1.7 (schema 7): the answer of RefreshCustomMetric: {id (the requested
// metric id), started, coalesced (arrays of metric ids)}. A refused request
// is a D-Bus error instead.
QJsonObject refreshResult(const QString &metricId, const custommetrics::RefreshResult &result);
custommetrics::RefreshResult refreshResultFromJson(const QJsonObject &result);

// Settings exposed by the API (and accepted by SetSetting).
QJsonObject settingsObject(const AppSettings &settings);

QString stateName(metrics::MetricState state);
QString stateName(custommetrics::State state);

// --- back to the frontend's types --------------------------------------------

// Rebuilds the system snapshot from the built-in metric objects (their
// "data" carries the raw section values). Missing metrics stay Pending.
metrics::SystemMetricsSnapshot systemSnapshotFromMetrics(const QJsonArray &metrics);

// Rebuilds the Custom Metrics sources (categories "custom" and
// "integration"), in API order.
QList<custommetrics::Source> sourcesFromMetrics(const QJsonArray &metrics);

} // namespace aqtruncat::api
