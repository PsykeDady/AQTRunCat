#pragma once

// Builds ZIP archives for the tests with libarchive's writer, including
// deliberately malicious ones (path traversal, links, special files).

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <archive.h>
#include <archive_entry.h>

#include <sys/stat.h>

namespace archivefixture {

struct Entry {
    QString path;
    QByteArray data;           // regular files
    mode_t type = AE_IFREG;    // AE_IFREG, AE_IFDIR, AE_IFLNK
    QString symlinkTarget;     // AE_IFLNK
};

inline QByteArray png(QSize size = {56, 36}, QColor color = Qt::black)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int y = size.height() / 4; y < size.height() * 3 / 4; ++y)
        for (int x = size.width() / 4; x < size.width() * 3 / 4; ++x)
            image.setPixelColor(x, y, color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// "<dir>/<name>-frame-N.png" entries, like a Runner Gallery archive.
inline QList<Entry> galleryEntries(const QString &name, int frames, QSize size = {56, 36}, bool withDirEntry = true)
{
    QList<Entry> entries;
    const QString dir = name + QStringLiteral("-frames/");
    if (withDirEntry)
        entries.append({dir, {}, AE_IFDIR, {}});
    for (int i = 0; i < frames; ++i)
        entries.append({dir + QStringLiteral("%1-frame-%2.png").arg(name).arg(i), png(size), AE_IFREG, {}});
    return entries;
}

// compression: "deflate" or "store".
inline bool writeZip(const QString &path, const QList<Entry> &entries, const char *compression = "deflate")
{
    archive *a = archive_write_new();
    archive_write_set_format_zip(a);
    archive_write_set_format_option(a, "zip", "compression", compression);
    if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }
    bool ok = true;
    for (const Entry &e : entries) {
        archive_entry *entry = archive_entry_new();
        archive_entry_set_pathname_utf8(entry, e.path.toUtf8().constData());
        archive_entry_set_filetype(entry, e.type);
        archive_entry_set_perm(entry, e.type == AE_IFDIR ? 0755 : 0644);
        if (e.type == AE_IFLNK)
            archive_entry_set_symlink_utf8(entry, e.symlinkTarget.toUtf8().constData());
        archive_entry_set_size(entry, e.type == AE_IFREG ? e.data.size() : 0);
        ok = ok && archive_write_header(a, entry) == ARCHIVE_OK;
        if (e.type == AE_IFREG && !e.data.isEmpty())
            ok = ok && archive_write_data(a, e.data.constData(), e.data.size()) == e.data.size();
        archive_entry_free(entry);
    }
    ok = archive_write_close(a) == ARCHIVE_OK && ok;
    archive_write_free(a);
    return ok;
}

// Entry names as stored in the archive: proves a malicious name really is in
// the file (and was not sanitised by the writer).
inline QStringList storedNames(const QString &path)
{
    QStringList names;
    archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    if (archive_read_open_filename(a, QFile::encodeName(path).constData(), 16384) == ARCHIVE_OK) {
        archive_entry *entry = nullptr;
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            names.append(QString::fromUtf8(archive_entry_pathname_utf8(entry)));
            archive_read_data_skip(a);
        }
    }
    archive_read_free(a);
    return names;
}

// Rewrites the Unix mode stored in the central directory record of `entryName`
// (e.g. to S_IFIFO | 0644), producing a "special file" entry no ZIP writer
// would create on purpose. Returns false if the record was not found.
inline bool patchUnixMode(const QString &path, const QString &entryName, quint32 mode)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    QByteArray bytes = file.readAll();
    const QByteArray name = entryName.toUtf8();
    for (qsizetype i = bytes.indexOf("PK\x01\x02"); i >= 0; i = bytes.indexOf("PK\x01\x02", i + 4)) {
        const auto u16 = [&bytes](qsizetype at) { return quint16(uchar(bytes[at]) | uchar(bytes[at + 1]) << 8); };
        const quint16 nameLength = u16(i + 28);
        if (bytes.mid(i + 46, nameLength) != name)
            continue;
        bytes[i + 5] = 3; // "version made by": Unix, so the mode is honoured
        const quint32 attributes = mode << 16;
        for (int b = 0; b < 4; ++b)
            bytes[i + 38 + b] = char((attributes >> (8 * b)) & 0xff);
        file.seek(0);
        return file.write(bytes) == bytes.size();
    }
    return false;
}

} // namespace archivefixture
