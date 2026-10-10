#include "customsymbol.h"

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
