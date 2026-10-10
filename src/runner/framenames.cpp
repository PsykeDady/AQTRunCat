#include "framenames.h"

#include <QRegularExpression>

#include <algorithm>

namespace aqtruncat {

namespace {

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

// Compares two digit runs by value without converting (no overflow).
int compareNumbers(QStringView a, QStringView b)
{
    auto stripZeros = [](QStringView s) {
        qsizetype i = 0;
        while (i + 1 < s.size() && s[i] == u'0')
            ++i;
        return s.mid(i);
    };
    a = stripZeros(a);
    b = stripZeros(b);
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    return a.compare(b);
}

} // namespace

bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool digitA = isDigit(a[i]);
        const bool digitB = isDigit(b[j]);
        qsizetype endA = i, endB = j;
        while (endA < a.size() && isDigit(a[endA]) == digitA)
            ++endA;
        while (endB < b.size() && isDigit(b[endB]) == digitB)
            ++endB;
        const QStringView runA = QStringView(a).mid(i, endA - i);
        const QStringView runB = QStringView(b).mid(j, endB - j);

        int cmp = 0;
        if (digitA && digitB)
            cmp = compareNumbers(runA, runB);
        else
            cmp = runA.compare(runB, Qt::CaseInsensitive);
        if (cmp != 0)
            return cmp < 0;
        i = endA;
        j = endB;
    }
    if ((i < a.size()) != (j < b.size()))
        return i >= a.size(); // the shorter one (a prefix) comes first
    return a < b;             // equal naturally: make the order total
}

QStringList frameFileNames(const QStringList &directoryEntries)
{
    QStringList frames;
    for (const QString &name : directoryEntries) {
        if (name.startsWith(u'.'))
            continue; // .DS_Store, ._frame-0.png (AppleDouble), editor files
        if (!name.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            continue;
        if (name.compare(QLatin1String("preview.png"), Qt::CaseInsensitive) == 0)
            continue; // Runner Gallery animated preview, not a frame
        frames.append(name);
    }
    std::sort(frames.begin(), frames.end(), naturalLess);
    return frames;
}

long long trailingFrameNumber(const QString &fileName)
{
    static const QRegularExpression pattern(QStringLiteral("(\\d{1,15})\\.png$"),
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(fileName);
    return match.hasMatch() ? match.captured(1).toLongLong() : -1;
}

} // namespace aqtruncat
