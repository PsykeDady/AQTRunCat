#pragma once

#include "custommetrics/custommetric.h"
#include "metrics/systemmetrics.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <optional>
#include <string>

namespace aqtruncat::metricsbar {

// At most this many Metrics Bar items (tray icons) exist at once, built-in
// and custom together. The runner's own tray icon is separate and not counted.
inline constexpr int MaxItems = 3;

enum class Status {
    Pending,     // no value yet (first sample, file being retried)
    Available,
    Unavailable, // the source does not exist here (no swap, missing file)
    Error,       // the source exists but could not be read or parsed
};

enum class Origin {
    BuiltIn,
    Custom,
};

// One Metrics Bar entry, described semantically. It says nothing about how
// big a tray slot is: renderers decide how much of it fits.
struct Item {
    QString id;          // stable: "cpu", "network.download", "custom:<canonical path>"
    Origin origin = Origin::BuiltIn;
    QString label;       // full name ("Network download", custom title)
    QString shortLabel;  // short identity ("NET↓"); custom: symbol glyph or empty
    std::optional<aqtruncat::custommetrics::IconReference> icon; // v2 custom symbol
    QString shortValue;  // compact value ("1.2M"); custom: metricsBarValue as given
    QString detail;      // full value ("1.2 MiB/s"); empty when there is none
    Status status = Status::Pending;
    QString statusMessage;
    QDateTime lastUpdated; // custom sources only

    bool operator==(const Item &) const = default;
};

struct BuiltInMetric {
    QString id;
    QString label;
    QString shortLabel;
};

// Every built-in id the bar understands, in the default presentation order.
const QList<BuiltInMetric> &builtInMetrics();
bool isBuiltInId(const QString &id);

// Custom ids derive from the canonical source/configuration identity, never from the
// (producer-controlled, changing) title.
QString customId(const QString &sourceId);
bool isCustomId(const QString &id);
QString sourceIdFromCustomId(const QString &id);
// One item of a multi-value source: "custom-item:<item id>:<source id>". The
// item id (custommetrics::isValidItemId) never contains ':', so any source id
// can follow it unambiguously.
QString customItemId(const QString &sourceId, const QString &itemId);
bool isCustomItemId(const QString &id);
QString sourceIdFromCustomItemId(const QString &id);
QString itemIdFromCustomItemId(const QString &id);
// The items of a snapshot that can be selected on their own: an id and a
// nonempty metricsBarValue.
bool isSelectableItem(const aqtruncat::custommetrics::Item &item);
// A built-in id or a well-formed custom id: what a selection may contain.
bool isKnownId(const QString &id);

} // namespace aqtruncat::metricsbar

// Turns the shared system snapshot, shared M6/F1 custom sources and the user's
// selection into the ordered list of bar items. Owns no sampler and no timer
// of its own beyond a zero-delay coalescing timer: several inputs changing in
// one event-loop cycle produce one itemsChanged.
class MetricsBarModel : public QObject
{
    Q_OBJECT

public:
    explicit MetricsBarModel(std::string homePath, QObject *parent = nullptr);

    const QList<aqtruncat::metricsbar::Item> &items() const { return m_items; }

public slots:
    // Only the first MaxItems ids are considered, whatever the caller passes.
    void setConfiguration(bool enabled, const QStringList &order);
    void updateSystem(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);
    void updateCustom(const QList<aqtruncat::custommetrics::Source> &sources);

signals:
    // Emitted only when the visible list or any item actually changed.
    void itemsChanged(const QList<aqtruncat::metricsbar::Item> &items);

private:
    void scheduleRebuild();
    void rebuild();
    std::optional<aqtruncat::metricsbar::Item> builtIn(const aqtruncat::metricsbar::BuiltInMetric &metric) const;
    std::optional<aqtruncat::metricsbar::Item> custom(const QString &id) const;
    std::optional<aqtruncat::metricsbar::Item> customItem(const QString &id) const;

    std::string m_homePath;
    bool m_enabled = false;
    QStringList m_order;
    std::optional<aqtruncat::metrics::SystemMetricsSnapshot> m_system;
    QList<aqtruncat::custommetrics::Source> m_sources;
    QList<aqtruncat::metricsbar::Item> m_items;
    QTimer m_coalesce;
};
