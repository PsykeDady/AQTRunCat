#include "metricsbarrenderer.h"

#include <QFontMetricsF>
#include <QPainter>

#include <cmath>
#include <optional>

namespace aqtruncat::metricsbar {

namespace {

constexpr qreal Margin = 0.5;       // horizontal breathing room on each side
constexpr qreal CaptionBand = 8.5;  // top band for the identity line
constexpr qreal SizeStep = 0.5;

// QFont pixel sizes are integers: text is laid out at 4× the canvas size and
// scaled back by the painter, which gives quarter-unit size precision.
constexpr qreal LayoutScale = 4.0;

QFont sized(const QFont &base, qreal canvasPixelSize, bool bold)
{
    QFont font = base;
    font.setPixelSize(qMax(1, qRound(canvasPixelSize * LayoutScale)));
    font.setBold(bold);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

struct Fit {
    qreal pixelSize = 0.0;
    QRectF bounds; // tight bounds at pixelSize, origin at the baseline start
};

// Largest size in [minSize, maxSize] (step 0.5) whose tight bounds fit the
// box; nullopt when even the minimum does not fit.
std::optional<Fit> fit(const QString &text, const QFont &base, bool bold, qreal maxSize, qreal minSize,
                       qreal width, qreal height)
{
    for (qreal size = maxSize; size >= minSize - 1e-9; size -= SizeStep) {
        const QFont font = sized(base, size, bold);
        const QFontMetricsF metrics(font);
        const QRectF tight = metrics.tightBoundingRect(text);
        const qreal scale = size / font.pixelSize();
        const QRectF bounds(tight.x() * scale, tight.y() * scale, tight.width() * scale, tight.height() * scale);
        if (bounds.width() <= width && bounds.height() <= height)
            return Fit{size, bounds};
    }
    return std::nullopt;
}

// Draws text with its tight bounds centred in band; returns the drawn bounds.
QRectF drawCentred(QPainter &painter, const QString &text, const QFont &base, bool bold, const Fit &f,
                   const QRectF &band)
{
    const QFont font = sized(base, f.pixelSize, bold);
    const qreal scale = f.pixelSize / font.pixelSize();
    const QPointF origin(band.center().x() - f.bounds.width() / 2.0 - f.bounds.x(),
                         band.center().y() - f.bounds.height() / 2.0 - f.bounds.y());
    painter.save();
    painter.translate(origin);
    painter.scale(scale, scale);
    painter.setFont(font);
    painter.drawText(QPointF(0, 0), text);
    painter.restore();
    return f.bounds.translated(origin);
}

} // namespace

IconContent iconContentFor(const Item &item)
{
    IconContent content;
    content.caption = item.shortLabel;
    const bool hasValue = !item.shortValue.isEmpty();
    switch (item.status) {
    case Status::Available:
        content.text = hasValue ? item.shortValue : PendingText;
        break;
    case Status::Pending:
        // Built-in values are never stale-but-pending (the model clears
        // them); a custom source retrying a file keeps its last value.
        content.text = hasValue && item.origin == Origin::Custom ? item.shortValue : PendingText;
        break;
    case Status::Unavailable:
        content.text = UnavailableText;
        break;
    case Status::Error:
        content.text = ErrorText;
        break;
    }
    return content;
}

RenderedIcon renderIcon(const IconContent &content, const QFont &baseFont, const QColor &colour,
                        qreal devicePixelRatio)
{
    RenderedIcon result;
    const qreal dpr = std::isfinite(devicePixelRatio) && devicePixelRatio > 0 ? devicePixelRatio : 1.0;
    // At least 2× the canvas: the host scales the image into its slot, and a
    // larger source would only add D-Bus payload to every update.
    const qreal oversample = qMax(2.0, dpr);
    const int side = int(std::ceil(CanvasSide * oversample));
    result.image = QImage(side, side, QImage::Format_ARGB32_Premultiplied);
    result.image.fill(Qt::transparent);

    const qreal width = CanvasSide - 2.0 * Margin;
    std::optional<Fit> caption;
    if (!content.caption.isEmpty())
        caption = fit(content.caption, baseFont, false, CaptionMaxPixelSize, CaptionMinPixelSize, width, CaptionBand);
    const QRectF valueBand = caption ? QRectF(0, CaptionBand, CanvasSide, CanvasSide - CaptionBand)
                                     : QRectF(0, 0, CanvasSide, CanvasSide);

    QString text = content.text.isEmpty() ? PendingText : content.text;
    std::optional<Fit> value = fit(text, baseFont, true, ValueMaxPixelSize, ValueMinPixelSize,
                                   width, valueBand.height());
    if (!value) {
        // Never shrink into illegibility or cut the value: mark the overflow
        // and leave the complete value to the tooltip.
        result.overflow = true;
        text = OverflowText;
        value = fit(text, baseFont, true, ValueMaxPixelSize, 1.0, width, valueBand.height());
    }

    QPainter painter(&result.image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    painter.scale(oversample, oversample);
    painter.setPen(colour);
    if (caption) {
        result.captionShown = true;
        result.captionPixelSize = caption->pixelSize;
        result.captionRect = drawCentred(painter, content.caption, baseFont, false, *caption,
                                         QRectF(0, 0, CanvasSide, CaptionBand));
    }
    if (value) {
        result.valuePixelSize = value->pixelSize;
        result.valueRect = drawCentred(painter, text, baseFont, true, *value, valueBand);
    }
    return result;
}

} // namespace aqtruncat::metricsbar
