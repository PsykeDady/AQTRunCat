#pragma once

// Helpers that build runner directories on disk for the tests.

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStringList>

namespace fixture {

// Writes a PNG whose pixel (0,0) encodes `marker` in its red channel, so a
// test can tell which file a loaded frame came from.
inline bool writeFrame(const QString &path, QSize size = {56, 36}, QColor color = Qt::black, int marker = 0)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = size.height() / 4; y < size.height() * 3 / 4; ++y)
        for (int x = size.width() / 4; x < size.width() * 3 / 4; ++x)
            image.setPixelColor(x, y, color);
    image.setPixelColor(0, 0, QColor(marker, marker, marker, 255));
    return image.save(path, "PNG");
}

inline int markerOf(const QImage &frame)
{
    return qRed(frame.convertToFormat(QImage::Format_ARGB32).pixel(0, 0));
}

inline void writeFrames(const QDir &dir, const QStringList &names, QSize size = {56, 36}, QColor color = Qt::black)
{
    dir.mkpath(QStringLiteral("."));
    for (const QString &name : names)
        writeFrame(dir.filePath(name), size, color);
}

inline bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

inline QStringList galleryNames(const QString &runner, int count)
{
    QStringList names;
    for (int i = 0; i < count; ++i)
        names.append(QStringLiteral("%1-frame-%2.png").arg(runner).arg(i));
    return names;
}

} // namespace fixture
