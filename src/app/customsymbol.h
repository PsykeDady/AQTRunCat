#pragma once

#include "custommetrics/custommetric.h"

#include <QIcon>
#include <QString>

#include <optional>

// Presentation text for a RunCat Neo custom metric "symbol". Known SF Symbol
// names map to a Unicode glyph, non-ASCII text from Linux-native producers is
// used as is, anything else (unknown SF Symbol identifiers) yields an empty
// string rather than exposing implementation names. The model keeps the
// original value.
QString customSymbolText(const std::optional<QString> &symbol);

// Resolves v2 theme/file icons for presentation. A configured but missing or
// invalid icon returns the application fallback rather than invalidating the
// metric. No icon is returned when none was configured.
QIcon customSymbolIcon(const std::optional<aqtruncat::custommetrics::IconReference> &icon);
