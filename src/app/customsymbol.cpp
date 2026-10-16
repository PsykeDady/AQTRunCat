#include "customsymbol.h"

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QStyle>

QString customSymbolText(const std::optional<QString> &symbol)
{
    if (!symbol || symbol->isEmpty())
        return {};
    if (*symbol == QLatin1String("staroflife"))
        return QStringLiteral("✣");
    if (*symbol == QLatin1String("bitcoinsign") || *symbol == QLatin1String("bitcoinsign.circle.fill"))
        return QStringLiteral("₿");
    if (*symbol == QLatin1String("chart.bar.horizontal.page.fill"))
        return QStringLiteral("▤");
    for (const QChar character : *symbol)
        if (character.unicode() > 0x7f)
            return *symbol;
    return {};
}

QIcon customSymbolIcon(const std::optional<aqtruncat::custommetrics::IconReference> &icon)
{
    if (!icon)
        return {};
    QIcon result;
    if (icon->type == aqtruncat::custommetrics::IconType::Theme) {
        result = QIcon::fromTheme(icon->resolvedValue);
    } else {
        const QFileInfo info(icon->resolvedValue);
        QImageReader reader(icon->resolvedValue);
        if (info.isFile() && info.isReadable() && reader.canRead())
            result = QIcon(icon->resolvedValue);
    }
    if (!result.isNull())
        return result;
    return QApplication::style()->standardIcon(QStyle::SP_FileDialogInfoView);
}
