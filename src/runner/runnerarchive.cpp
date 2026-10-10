#include "runnerarchive.h"

#include "framenames.h"
#include "runnercatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <archive.h>
#include <archive_entry.h>

#include <memory>

namespace aqtruncat {

namespace {

constexpr int MaxPathLength = 512;

struct ArchiveDeleter {
    void operator()(archive *a) const { archive_read_free(a); }
};

ExtractionResult failure(RunnerError kind, QString message)
{
    ExtractionResult result;
    result.errorKind = kind;
    result.error = std::move(message);
    return result;
}

ArchiveEntryKind kindOf(archive_entry *entry)
{
    if (archive_entry_hardlink(entry))
        return ArchiveEntryKind::Hardlink;
    switch (archive_entry_filetype(entry)) {
    case AE_IFREG:
        return ArchiveEntryKind::Regular;
    case AE_IFDIR:
        return ArchiveEntryKind::Directory;
    case AE_IFLNK:
        return ArchiveEntryKind::Symlink;
    default:
        return ArchiveEntryKind::Other;
    }
}

QString libarchiveError(archive *a)
{
    const char *message = archive_error_string(a);
    return message ? QString::fromUtf8(message) : QStringLiteral("unknown error");
}

// Runner id from "beagle-frames.zip" → "beagle".
QString idFromArchiveName(const QString &archivePath)
{
    QString name = QFileInfo(archivePath).fileName();
    if (name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
        name.chop(4);
    return runnerIdForDirectoryName(name);
}

} // namespace

std::optional<QString> safeArchivePath(const QString &rawPath, QString *error)
{
    auto reject = [&](const QString &why) -> std::optional<QString> {
        *error = QStringLiteral("unsafe entry \"%1\": %2").arg(rawPath, why);
        return std::nullopt;
    };
    if (rawPath.isEmpty())
        return reject(QStringLiteral("empty name"));
    if (rawPath.size() > MaxPathLength)
        return reject(QStringLiteral("name too long"));
    for (const QChar c : rawPath)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return reject(QStringLiteral("control character in name"));
    if (rawPath.contains(u'\\'))
        return reject(QStringLiteral("backslash in name"));
    if (rawPath.startsWith(u'/'))
        return reject(QStringLiteral("absolute path"));
    if (rawPath.size() >= 2 && rawPath[1] == u':' && rawPath[0].isLetter())
        return reject(QStringLiteral("drive letter"));

    QString path = rawPath;
    if (path.endsWith(u'/'))
        path.chop(1); // directory entries
    const QStringList parts = path.split(u'/');
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String("."))
            return reject(QStringLiteral("empty or \".\" path component"));
        if (part == QLatin1String(".."))
            return reject(QStringLiteral("\"..\" path component"));
    }
    return path;
}

QString unsupportedEntryKind(ArchiveEntryKind kind)
{
    switch (kind) {
    case ArchiveEntryKind::Regular:
    case ArchiveEntryKind::Directory:
        return {};
    case ArchiveEntryKind::Symlink:
        return QStringLiteral("symbolic links are not allowed in runner archives");
    case ArchiveEntryKind::Hardlink:
        return QStringLiteral("hard links are not allowed in runner archives");
    case ArchiveEntryKind::Other:
        return QStringLiteral("special files (devices, FIFOs, sockets) are not allowed in runner archives");
    }
    return QStringLiteral("unknown entry type");
}

bool isArchiveJunk(const QString &relativePath)
{
    for (const QString &part : relativePath.split(u'/'))
        if (part.startsWith(u'.') || part == QLatin1String("__MACOSX"))
            return true;
    return false;
}

ExtractionResult extractRunnerArchive(const QString &archivePath, const QString &destination,
                                      const ArchiveLimits &limits)
{
    const QFileInfo info(archivePath);
    if (!info.exists())
        return failure(RunnerError::NotFound, QStringLiteral("file not found: %1").arg(archivePath));
    if (!info.isFile())
        return failure(RunnerError::Invalid, QStringLiteral("not a regular file: %1").arg(archivePath));
    if (info.size() > limits.maxArchiveBytes)
        return failure(RunnerError::Invalid, QStringLiteral("archive is %1 bytes, more than the limit of %2")
                                                 .arg(info.size()).arg(limits.maxArchiveBytes));
    const QDir dest(destination);
    if (!dest.exists())
        return failure(RunnerError::Filesystem, QStringLiteral("extraction directory missing: %1").arg(destination));

    std::unique_ptr<archive, ArchiveDeleter> reader(archive_read_new());
    // ZIP only, read through its central directory.
    archive_read_support_format_zip_seekable(reader.get());
    if (archive_read_open_filename(reader.get(), QFile::encodeName(archivePath).constData(), 64 * 1024) != ARCHIVE_OK)
        return failure(RunnerError::Invalid,
                       QStringLiteral("not a readable ZIP archive: %1").arg(libarchiveError(reader.get())));

    ExtractedRunner extracted;
    qint64 entries = 0;
    QByteArray buffer(64 * 1024, Qt::Uninitialized);

    while (true) {
        archive_entry *entry = nullptr;
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF)
            break;
        if (status < ARCHIVE_WARN)
            return failure(RunnerError::Invalid, QStringLiteral("corrupt archive: %1").arg(libarchiveError(reader.get())));
        if (++entries > limits.maxEntries)
            return failure(RunnerError::Invalid, QStringLiteral("more than %1 entries").arg(limits.maxEntries));

        const char *utf8 = archive_entry_pathname_utf8(entry);
        const char *native = utf8 ? utf8 : archive_entry_pathname(entry);
        if (!native)
            return failure(RunnerError::Invalid, QStringLiteral("entry %1 has an unreadable name").arg(entries));
        const QString rawPath = utf8 ? QString::fromUtf8(utf8) : QFile::decodeName(native);

        QString error;
        const std::optional<QString> relative = safeArchivePath(rawPath, &error);
        if (!relative)
            return failure(RunnerError::Invalid, error);
        const ArchiveEntryKind kind = kindOf(entry);
        if (const QString why = unsupportedEntryKind(kind); !why.isEmpty())
            return failure(RunnerError::Invalid, QStringLiteral("\"%1\": %2").arg(rawPath, why));

        if (isArchiveJunk(*relative)) {
            extracted.skipped.append(*relative);
            archive_read_data_skip(reader.get());
            continue;
        }

        const QString target = dest.filePath(*relative); // safe: no "..", not absolute
        if (kind == ArchiveEntryKind::Directory) {
            if (!dest.mkpath(*relative))
                return failure(RunnerError::Filesystem, QStringLiteral("cannot create %1").arg(target));
            continue;
        }

        if (archive_entry_size_is_set(entry) && archive_entry_size(entry) > limits.maxFileBytes)
            return failure(RunnerError::Invalid, QStringLiteral("\"%1\" is larger than %2 bytes").arg(rawPath).arg(limits.maxFileBytes));
        const QString parent = QFileInfo(*relative).path();
        if (parent != QLatin1String(".") && !dest.mkpath(parent))
            return failure(RunnerError::Filesystem, QStringLiteral("cannot create %1").arg(dest.filePath(parent)));
        QFile file(target);
        if (file.exists())
            return failure(RunnerError::Invalid, QStringLiteral("\"%1\" appears more than once").arg(rawPath));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return failure(RunnerError::Filesystem, QStringLiteral("cannot write %1: %2").arg(target, file.errorString()));

        qint64 fileBytes = 0;
        while (true) {
            const la_ssize_t read = archive_read_data(reader.get(), buffer.data(), buffer.size());
            if (read == 0)
                break;
            if (read < 0)
                return failure(RunnerError::Invalid, QStringLiteral("corrupt data in \"%1\": %2").arg(rawPath, libarchiveError(reader.get())));
            fileBytes += read;
            extracted.bytes += read;
            // Checked on the real decompressed size, not only the declared one.
            if (fileBytes > limits.maxFileBytes)
                return failure(RunnerError::Invalid, QStringLiteral("\"%1\" is larger than %2 bytes").arg(rawPath).arg(limits.maxFileBytes));
            if (extracted.bytes > limits.maxTotalBytes)
                return failure(RunnerError::Invalid, QStringLiteral("archive expands to more than %1 bytes").arg(limits.maxTotalBytes));
            if (file.write(buffer.constData(), read) != read)
                return failure(RunnerError::Filesystem, QStringLiteral("cannot write %1: %2").arg(target, file.errorString()));
        }
        ++extracted.files;
    }

    if (extracted.files == 0)
        return failure(RunnerError::Invalid, QStringLiteral("the archive contains no files"));

    // Locate the runner: frames at the top level, or one top-level folder
    // (Runner Gallery archives contain a single "<name>-frames/" folder).
    const QStringList topFiles = dest.entryList(QDir::Files);
    const QStringList topDirs = dest.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (frameFileNames(topFiles).isEmpty() && topFiles.isEmpty() && topDirs.size() == 1) {
        extracted.runnerRoot = dest.filePath(topDirs.first());
        extracted.suggestedId = runnerIdForDirectoryName(topDirs.first());
    } else {
        // Frames at the top level, or several entries: the loader decides
        // whether this is exactly one runner (and rejects ambiguous layouts).
        extracted.runnerRoot = dest.absolutePath();
        extracted.suggestedId = idFromArchiveName(archivePath);
    }

    ExtractionResult result;
    result.runner = std::move(extracted);
    return result;
}

} // namespace aqtruncat
