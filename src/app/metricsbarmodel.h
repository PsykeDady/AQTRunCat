#pragma once

#include "custommetrics/custommetric.h"
#include "metricsbarids.h"
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
    QString detail;      // full value ("1.2 MiB/s"); empty when there is none. May hold
                         // more lines that change with the value ("8.3 GiB available")
    QString context;     // F6: slowly changing hover details, one per line
                         // ("8 logical CPUs", "checked 14:05"); empty when none
    Status status = Status::Pending;
    QString statusMessage;
    QDateTime lastUpdated; // custom sources only

    bool operator==(const Item &) const = default;
};

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
