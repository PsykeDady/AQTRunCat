#include "runnercommands.h"

namespace aqtruncat::cli {

namespace {

QString describe(const Runner &r)
{
    return QStringLiteral("%1, %2 frames, %3x%4, %5")
        .arg(r.displayName)
        .arg(r.frames.size())
        .arg(r.frameSize.width())
        .arg(r.frameSize.height())
        .arg(r.monochrome ? QStringLiteral("monochrome") : QStringLiteral("color"));
}

} // namespace

int exitCodeFor(RunnerError error)
{
    switch (error) {
    case RunnerError::None:
        return ExitOk;
    case RunnerError::NotFound:
        return ExitNotFound;
    case RunnerError::Invalid:
        return ExitInvalid;
    case RunnerError::Conflict:
        return ExitConflict;
    case RunnerError::Filesystem:
        return ExitFailure;
    }
    return ExitFailure;
}

QString formatRunnerList(const QList<ListedRunner> &runners, const QString &defaultId)
{
    QString out = QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                      .arg(QStringLiteral("ID"), -18)
                      .arg(QStringLiteral("NAME"), -18)
                      .arg(QStringLiteral("SOURCE"), -7)
                      .arg(QStringLiteral("FRAMES"), 6)
                      .arg(QStringLiteral("SIZE"), -7)
                      .arg(QStringLiteral("MODE"));
    for (const ListedRunner &listed : runners) {
        const RunnerEntry &entry = listed.entry;
        const QString id = entry.id + (entry.id == defaultId ? QStringLiteral(" *") : QString());
        if (!listed.result.isValid()) {
            out += QStringLiteral("%1  INVALID: %2 (%3)\n").arg(id, -18).arg(listed.result.error, entry.path);
            continue;
        }
        const Runner &r = *listed.result.runner;
        out += QStringLiteral("%1  %2  %3  %4  %5  %6\n")
                   .arg(id, -18)
                   .arg(r.displayName, -18)
                   .arg(sourceName(r.source), -7)
                   .arg(r.frames.size(), 6)
                   .arg(QStringLiteral("%1x%2").arg(r.frameSize.width()).arg(r.frameSize.height()), -7)
                   .arg(r.monochrome ? QStringLiteral("monochrome") : QStringLiteral("color"));
        for (const QString &warning : r.warnings)
            out += QStringLiteral("    warning: %1\n").arg(warning);
    }
    return out;
}

int runnerList(const RunnerService &service, const QString &defaultId, QTextStream &out)
{
    out << formatRunnerList(service.list(), defaultId) << '\n'
        << "* default runner. User runners: " << service.locations().userRoot << '\n';
    out.flush();
    return ExitOk;
}

int runnerImport(const RunnerService &service, const Invocation &invocation, QTextStream &out, QTextStream &err)
{
    ImportOptions options;
    options.id = invocation.id;
    options.replaceExisting = invocation.force;
    const ImportResult result = service.importArchive(invocation.archivePath, options);
    if (!result.ok()) {
        err << "aqtruncat: cannot import " << invocation.archivePath << ": " << result.message << '\n';
        if (result.error == RunnerError::Conflict && !invocation.force)
            err << "Use --force to replace it, or --id to install under another id.\n";
        err.flush();
        return exitCodeFor(result.error);
    }
    const Runner &r = *result.runner;
    out << "Imported runner \"" << r.id << "\" (" << describe(r) << ") into " << result.installedPath << '\n'
        << "Took " << (result.extractMs + result.validateMs + result.installMs) << " ms (extract " << result.extractMs
        << ", validate " << result.validateMs << ", install " << result.installMs << ")\n";
    if (!result.replacedUserPath.isEmpty())
        out << "Replaced the previous user runner " << result.replacedUserPath << '\n';
    if (result.overridesBundled)
        out << "It takes precedence over the bundled runner \"" << r.id << "\".\n";
    out << "Show it with: aqtruncat run --runner " << r.id << '\n';
    out.flush();
    for (const QString &warning : r.warnings)
        err << "aqtruncat: warning: " << warning << '\n';
    if (!result.skippedEntries.isEmpty())
        err << "aqtruncat: note: skipped " << result.skippedEntries.size() << " packaging entries (__MACOSX, hidden files)\n";
    err.flush();
    return ExitOk;
}

int runnerRemove(const RunnerService &service, const Invocation &invocation, QTextStream &out, QTextStream &err)
{
    const RemoveResult result = service.remove(invocation.id);
    if (!result.ok()) {
        err << "aqtruncat: cannot remove \"" << invocation.id << "\": " << result.message << '\n';
        err.flush();
        return exitCodeFor(result.error);
    }
    out << "Removed user runner \"" << invocation.id << "\" (" << result.removedPath << ")\n";
    if (result.bundledVisibleAgain)
        out << "The bundled runner \"" << invocation.id << "\" is used again.\n";
    out.flush();
    if (!result.message.isEmpty())
        err << "aqtruncat: warning: " << result.message << '\n';
    err.flush();
    return ExitOk;
}

} // namespace aqtruncat::cli
