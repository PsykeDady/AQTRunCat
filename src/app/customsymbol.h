#pragma once

#include <QString>

#include <optional>

// Presentation text for a RunCat Neo custom metric "symbol". Known SF Symbol
// names map to a Unicode glyph, non-ASCII text from Linux-native producers is
// used as is, anything else (unknown SF Symbol identifiers) yields an empty
// string rather than exposing implementation names. The model keeps the
// original value.
QString customSymbolText(const std::optional<QString> &symbol);
