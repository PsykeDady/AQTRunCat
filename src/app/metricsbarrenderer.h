#pragma once

#include "metricsbarmodel.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QString>

// Square tray-icon rendering of one Metrics Bar item for StatusNotifierItem
// hosts. Everything about the tray slot lives here, never in the model.
//
// Geometry is expressed on a nominal 22 × 22 logical canvas (the Plasma 6
// tray slot measured in the M7 spike). The image side is the canvas times
// max(2, device pixel ratio): hosts scale it smoothly into their slot, and the
// M7 spike showed no legibility gain from larger sources.
namespace aqtruncat::metricsbar {

inline constexpr qreal CanvasSide = 22.0;

// Font sizes in canvas units (pixels of the 22-unit slot). Below the minimum
// text is not legible in the tray, so the renderer shows an overflow marker
// instead of shrinking further.
inline constexpr qreal ValueMaxPixelSize = 13.0;
inline constexpr qreal ValueMinPixelSize = 7.0;
inline constexpr qreal CaptionMaxPixelSize = 9.0;
inline constexpr qreal CaptionMinPixelSize = 6.0;

// Compact indicators used when there is no value to show.
inline const QString PendingText = QStringLiteral("…");
inline const QString UnavailableText = QStringLiteral("—");
inline const QString ErrorText = QStringLiteral("!");
inline const QString OverflowText = QStringLiteral("###"); // value too wide: see the tooltip

// What an item looks like in a square slot, before fitting.
struct IconContent {
    QString caption; // small identity line above the value; may be empty
    QString text;    // value or indicator

    bool operator==(const IconContent &) const = default;
};

// Policy: the value is shown while it is current (Available) and also while
// a custom source is briefly Pending with a last known value; otherwise a
// status indicator. The identity is the item's short label.
IconContent iconContentFor(const Item &item);

struct RenderedIcon {
    QImage image;
    bool captionShown = false;
    bool overflow = false;      // text did not fit at the minimum size
    qreal valuePixelSize = 0.0; // canvas units
    qreal captionPixelSize = 0.0;
    QRectF valueRect;   // tight bounds of the drawn text, canvas units
    QRectF captionRect;
};

RenderedIcon renderIcon(const IconContent &content, const QFont &baseFont, const QColor &colour,
                        qreal devicePixelRatio);

} // namespace aqtruncat::metricsbar
