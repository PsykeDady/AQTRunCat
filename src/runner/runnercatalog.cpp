#include "runnercatalog.h"

#include "framenames.h"

#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace aqtruncat {

QString userRunnersDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/AQTRunCat/runners");
}

QString runnerIdForDirectoryName(const QString &name)
{
    static const QLatin1String suffix("-frames");
    if (name.size() > suffix.size() && name.endsWith(suffix))
        return name.chopped(suffix.size());
    return name;
}

bool isValidRunnerId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$"));
    return pattern.match(id).hasMatch();
}

QList<RunnerEntry> runnerDirectories(const QString &root, RunnerSource source)
{
    QList<RunnerEntry> entries;
    if (root.isEmpty())
        return entries;
    const QDir dir(root);
    QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot); // no QDir::Hidden: dot-dirs skipped
    std::sort(names.begin(), names.end(), naturalLess);
    for (const QString &name : std::as_const(names))
        entries.append(RunnerEntry{runnerIdForDirectoryName(name), dir.filePath(name), source});
    return entries;
}

QList<RunnerEntry> discoverRunners(const QString &bundledRoot, const QString &userRoot, QStringList *notes)
{
    QMap<QString, RunnerEntry> byId; // sorted by id
    // Bundled first, user second: a later root replaces an earlier one.
    const std::pair<QString, RunnerSource> roots[] = {{bundledRoot, RunnerSource::Bundled},
                                                      {userRoot, RunnerSource::User}};
    for (const auto &[rootPath, source] : roots) {
        QStringList seenInThisRoot;
        for (const RunnerEntry &entry : runnerDirectories(rootPath, source)) {
            const QString &id = entry.id;
            if (seenInThisRoot.contains(id)) {
                if (notes)
                    notes->append(QStringLiteral("%1 ignored: runner \"%2\" already provided by %3")
                                      .arg(entry.path, id, byId.value(id).path));
                continue;
            }
            seenInThisRoot.append(id);
            if (byId.contains(id) && notes)
                notes->append(QStringLiteral("%1 runner \"%2\" replaces %3 runner at %4")
                                  .arg(sourceName(source), id, sourceName(byId.value(id).source), byId.value(id).path));
            byId.insert(id, entry);
        }
    }
    return byId.values();
}

RunnerChoice chooseRunner(const QList<RunnerEntry> &entries, const QString &requestedId, const QString &defaultId)
{
    RunnerChoice choice;
    auto find = [&entries](const QString &id) {
        return std::find_if(entries.cbegin(), entries.cend(), [&id](const RunnerEntry &e) { return e.id == id; });
    };

    if (!requestedId.isEmpty()) {
        const auto it = find(requestedId);
        if (it == entries.cend()) {
            QStringList ids;
            for (const RunnerEntry &e : entries)
                ids.append(e.id);
            choice.error = QStringLiteral("unknown runner \"%1\"; runners found: %2")
                               .arg(requestedId, ids.isEmpty() ? QStringLiteral("none") : ids.join(QStringLiteral(", ")));
            choice.errorKind = RunnerError::NotFound;
            return choice;
        }
        RunnerLoadResult loaded = loadRunner(it->path, it->id, it->source);
        if (!loaded.isValid()) {
            choice.error = QStringLiteral("runner \"%1\" (%2) is invalid: %3").arg(it->id, it->path, loaded.error);
            choice.errorKind = RunnerError::Invalid;
        } else {
            choice.runner = std::move(loaded.runner);
        }
        return choice;
    }

    if (const auto it = find(defaultId); it != entries.cend()) {
        RunnerLoadResult loaded = loadRunner(it->path, it->id, it->source);
        if (loaded.isValid()) {
            choice.runner = std::move(loaded.runner);
            return choice;
        }
        choice.notes.append(QStringLiteral("default runner \"%1\" is invalid: %2").arg(defaultId, loaded.error));
    } else {
        choice.notes.append(QStringLiteral("default runner \"%1\" not found").arg(defaultId));
    }

    QList<RunnerEntry> ordered = entries;
    std::stable_sort(ordered.begin(), ordered.end(), [](const RunnerEntry &a, const RunnerEntry &b) {
        return a.source == RunnerSource::Bundled && b.source != RunnerSource::Bundled;
    });
    for (const RunnerEntry &entry : std::as_const(ordered)) {
        if (entry.id == defaultId)
            continue;
        RunnerLoadResult loaded = loadRunner(entry.path, entry.id, entry.source);
        if (loaded.isValid()) {
            choice.notes.append(QStringLiteral("using runner \"%1\" instead").arg(entry.id));
            choice.runner = std::move(loaded.runner);
            return choice;
        }
    }
    choice.error = QStringLiteral("no valid runner available");
    choice.errorKind = RunnerError::NotFound;
    return choice;
}

} // namespace aqtruncat
