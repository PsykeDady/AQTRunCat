#pragma once

#include <QStringList>

namespace aqtruncat {

// Natural ("human") ordering: digit runs compare by numeric value, so
// "frame-2" < "frame-10" and "1.png" < "2.png" < "10.png". Text runs compare
// case-insensitively. Ties (e.g. "1" vs "01", "A" vs "a") are broken by a
// plain comparison, so the order is total and locale independent.
// RunCat Neo orders imported frames the same way (localizedStandardCompare).
bool naturalLess(const QString &a, const QString &b);

// Picks the frame files among the names of a directory, in display order:
// PNG files (case-insensitive extension), excluding hidden files and the
// Runner Gallery "preview.png". Everything else is ignored.
QStringList frameFileNames(const QStringList &directoryEntries);

// Trailing number of a frame file name ("beagle-frame-12.png" → 12), or -1.
long long trailingFrameNumber(const QString &fileName);

} // namespace aqtruncat
