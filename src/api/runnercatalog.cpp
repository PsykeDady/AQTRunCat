#include "runnercatalog.h"

#include "apischema.h"
#include "app/runnerselection.h"

#include <QJsonArray>

namespace aqtruncat::api {

namespace {

QJsonArray strings(const QStringList &list)
{
    QJsonArray array;
    for (const QString &s : list)
        array.append(s);
    return array;
}

QJsonObject runnerObject(const ListedRunner &listed)
{
    const RunnerLoadResult &result = listed.result;
    QJsonObject object{{QStringLiteral("id"), listed.entry.id},
                       {QStringLiteral("source"), sourceName(listed.entry.source)},
                       {QStringLiteral("valid"), result.isValid()},
                       {QStringLiteral("error"), result.error}};
    if (!result.runner) {
        object.insert(QStringLiteral("displayName"), displayNameFromId(listed.entry.id));
        object.insert(QStringLiteral("author"), QString());
        object.insert(QStringLiteral("monochrome"), QJsonValue());
        object.insert(QStringLiteral("frameWidth"), QJsonValue());
        object.insert(QStringLiteral("frameHeight"), QJsonValue());
        object.insert(QStringLiteral("frames"), QJsonArray());
        object.insert(QStringLiteral("sequence"), QJsonArray());
        object.insert(QStringLiteral("warnings"), QJsonArray());
        return object;
    }
    const Runner &runner = *result.runner;
    QJsonArray sequence;
    for (int index : runner.sequence)
        sequence.append(index);
    object.insert(QStringLiteral("displayName"), runner.displayName);
    object.insert(QStringLiteral("author"), runner.author);
    object.insert(QStringLiteral("monochrome"), runner.monochrome);
    object.insert(QStringLiteral("frameWidth"), runner.frameSize.width());
    object.insert(QStringLiteral("frameHeight"), runner.frameSize.height());
    object.insert(QStringLiteral("frames"), strings(runner.frameFiles));
    object.insert(QStringLiteral("sequence"), sequence);
    object.insert(QStringLiteral("warnings"), strings(runner.warnings));
    return object;
}

} // namespace

QJsonObject runnerCatalog(const RunnerService &runners, const QString &requestedId, const QString &defaultId)
{
    QJsonArray list;
    for (const ListedRunner &listed : runners.list())
        list.append(runnerObject(listed));
    // The stored id is not a command-line choice: an unusable one falls back
    // to the default runner, exactly as "aqtruncat run" does.
    const StartupRunnerSelection selection = selectStartupRunner(runners, requestedId, defaultId, false);
    return QJsonObject{{QStringLiteral("schema"), SchemaVersion},
                       {QStringLiteral("requested"), requestedId},
                       {QStringLiteral("selected"), selection.choice.runner ? QJsonValue(selection.choice.runner->id)
                                                                             : QJsonValue()},
                       {QStringLiteral("defaultRunnerId"), defaultId},
                       {QStringLiteral("runners"), list}};
}

} // namespace aqtruncat::api
