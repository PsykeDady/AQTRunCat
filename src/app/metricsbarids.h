#pragma once

#include "custommetrics/custommetric.h"

#include <QList>
#include <QString>

// Metrics Bar item identities, shared by the persisted settings
// (AppSettings, QtCore) and the tray's Metrics Bar model. No GUI here.
namespace aqtruncat::metricsbar {

// At most this many Metrics Bar items (tray icons) exist at once, built-in
// and custom together. The runner's own tray icon is separate and not counted.
inline constexpr int MaxItems = 3;

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
