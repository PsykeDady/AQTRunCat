#pragma once

// Safe extraction of runner ZIP archives (Runner Gallery "<name>-frames.zip").
// Archives are untrusted input: every entry is checked before anything is
// written, only regular files and directories are created, and only inside
// the given destination directory. Validation of the runner itself is the
// loader's job; installation is the installer's.

#include "runnererror.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace aqtruncat {

struct ArchiveLimits {
    qint64 maxArchiveBytes = 64LL << 20; // size of the .zip file
    qint64 maxEntries = 1024;            // files + directories
    qint64 maxFileBytes = 8LL << 20;     // one extracted file
    qint64 maxTotalBytes = 64LL << 20;   // all extracted files together
};

enum class ArchiveEntryKind { Regular, Directory, Symlink, Hardlink, Other };

// Normalised relative path of an archive entry ("dir/frame-0.png", trailing
// '/' removed), or nullopt with *error set when the path could escape the
// destination or is otherwise unacceptable: absolute paths, "..", "." or empty
// components, backslashes, drive letters, control characters, overlong paths.
std::optional<QString> safeArchivePath(const QString &rawPath, QString *error);

// Error for entry kinds a runner archive must not contain (links, devices,
// FIFOs, sockets…), or an empty string for regular files and directories.
QString unsupportedEntryKind(ArchiveEntryKind kind);

// True for entries that are packaging junk and are skipped, not extracted:
// "__MACOSX/…" and any hidden component (".DS_Store", "._frame-0.png").
bool isArchiveJunk(const QString &relativePath);

struct ExtractedRunner {
    QString runnerRoot;  // directory holding the runner (destination or its single top-level folder)
    QString suggestedId; // from that folder name or from the archive file name, "-frames" removed
    int files = 0;
    qint64 bytes = 0;
    QStringList skipped; // junk entries that were not extracted
};

struct ExtractionResult {
    std::optional<ExtractedRunner> runner;
    QString error;
    RunnerError errorKind = RunnerError::None;
};

// Extracts `archivePath` (ZIP only) into `destination`, which must exist and
// should be empty (normally a QTemporaryDir).
ExtractionResult extractRunnerArchive(const QString &archivePath, const QString &destination,
                                      const ArchiveLimits &limits = {});

} // namespace aqtruncat
