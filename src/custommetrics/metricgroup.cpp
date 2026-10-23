#include "metricgroup.h"

#include "custommetric.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>

namespace aqtruncat::custommetrics {
namespace {

const QRegularExpression &validId()
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$"));
    return pattern;
}

// Where this level declares the paths it carries.
void recordOrigins(const QJsonObject &level, const QString &directory, QString &executableBase, QString &symbolBase)
{
    if (level.value(QStringLiteral("source")).toObject().contains(QStringLiteral("executable")))
        executableBase = directory;
    const QJsonValue symbol = level.value(QStringLiteral("symbol"));
    if (symbol.isObject() && symbol.toObject().contains(QStringLiteral("value")))
        symbolBase = directory;
}

QJsonObject inheritable(QJsonObject object)
{
    object.remove(QStringLiteral("id"));
    object.remove(QStringLiteral("enabled"));
    return object;
}

// Digit runs compare as numbers, the rest case-insensitively.
bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && a.at(ei).isDigit())
                ++ei;
            while (ej < b.size() && b.at(ej).isDigit())
                ++ej;
            const qulonglong x = a.mid(i, ei - i).toULongLong(), y = b.mid(j, ej - j).toULongLong();
            if (x != y)
                return x < y;
            i = ei;
            j = ej;
            continue;
        }
        const QChar ca = a.at(i).toLower(), cb = b.at(j).toLower();
        if (ca != cb)
            return ca < cb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

QString absoluteFrom(const QString &base, const QString &path)
{
    return QFileInfo(path).isAbsolute() ? QDir::cleanPath(path) : QDir::cleanPath(QDir(base).absoluteFilePath(path));
}

} // namespace

ConfigObject readConfigObject(const QString &path)
{
    ConfigObject result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    const QByteArray bytes = file.read(MaxJsonBytes + 1);
    if (bytes.size() > MaxJsonBytes) {
        result.error = QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("invalid JSON at offset %1: %2").arg(parseError.offset).arg(parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.error = QStringLiteral("top-level JSON value must be an object");
        return result;
    }
    result.object = document.object();
    return result;
}

bool isGroupObject(const QJsonObject &object)
{
    return object.value(QStringLiteral("source")).toObject().value(QStringLiteral("type")) == QLatin1String("group");
}

QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &override)
{
    QJsonObject merged = base;
    for (auto it = override.constBegin(); it != override.constEnd(); ++it) {
        const QJsonValue current = merged.value(it.key());
        if (current.isObject() && it.value().isObject())
            merged.insert(it.key(), deepMerge(current.toObject(), it.value().toObject()));
        else
            merged.insert(it.key(), it.value());
    }
    return merged;
}

Inheritance rootInheritance(const QJsonObject &group, const QString &groupDirectory, const QString &groupId,
                            bool enabled)
{
    Inheritance root;
    root.object = inheritable(group);
    recordOrigins(group, groupDirectory, root.executableBase, root.symbolBase);
    root.idPrefix = groupId;
    root.enabled = enabled;
    root.groupTitle = group.value(QStringLiteral("title")).toString();
    return root;
}

Inheritance childInheritance(const Inheritance &parent, const QJsonObject &child, const QString &childDirectory,
                             const QString &childId, bool enabled)
{
    Inheritance next = parent;
    next.object = deepMerge(parent.object, inheritable(child));
    recordOrigins(child, childDirectory, next.executableBase, next.symbolBase);
    next.idPrefix = parent.idPrefix + u'/' + childId;
    next.enabled = enabled;
    next.groupTitle = next.object.value(QStringLiteral("title")).toString();
    return next;
}

bool childId(const QJsonObject &child, const QString &childDirectory, QString &id, QString &error)
{
    const QJsonValue explicitId = child.value(QStringLiteral("id"));
    if (!explicitId.isUndefined()) {
        if (!explicitId.isString() || !validId().match(explicitId.toString()).hasMatch()) {
            error = QStringLiteral("field \"id\" must match [A-Za-z0-9][A-Za-z0-9._-]{0,99}");
            return false;
        }
        id = explicitId.toString();
        return true;
    }
    id = QFileInfo(childDirectory).fileName();
    if (!validId().match(id).hasMatch()) {
        error = QStringLiteral("the directory name \"%1\" is not a valid id: set \"id\" in its metric.json").arg(id);
        return false;
    }
    return true;
}

QJsonObject effectiveChild(const Inheritance &parent, const QJsonObject &child, const QString &childDirectory,
                           const QString &childId, bool enabled)
{
    QJsonObject effective = deepMerge(parent.object, inheritable(child));
    QString executableBase = parent.executableBase;
    QString symbolBase = parent.symbolBase;
    recordOrigins(child, childDirectory, executableBase, symbolBase);

    QJsonObject source = effective.value(QStringLiteral("source")).toObject();
    const QJsonValue executable = source.value(QStringLiteral("executable"));
    if (executable.isString() && !executable.toString().isEmpty() && !executableBase.isEmpty()) {
        source.insert(QStringLiteral("executable"), absoluteFrom(executableBase, executable.toString()));
        effective.insert(QStringLiteral("source"), source);
    }
    QJsonObject symbol = effective.value(QStringLiteral("symbol")).toObject();
    const QJsonValue symbolValue = symbol.value(QStringLiteral("value"));
    if (symbol.value(QStringLiteral("type")) == QLatin1String("file") && symbolValue.isString()
        && !symbolValue.toString().isEmpty() && !symbolBase.isEmpty()) {
        symbol.insert(QStringLiteral("value"), absoluteFrom(symbolBase, symbolValue.toString()));
        effective.insert(QStringLiteral("symbol"), symbol);
    }
    effective.insert(QStringLiteral("id"), childId);
    effective.insert(QStringLiteral("enabled"), enabled);
    return effective;
}

QStringList childConfigurations(const QString &groupDirectory, QStringList &visited)
{
    QStringList children;
    // Natural order: d6, d20, d100.
    QFileInfoList directories = QDir(groupDirectory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(directories.begin(), directories.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return naturalLess(a.fileName(), b.fileName());
    });
    for (const QFileInfo &directory : directories) {
        const QString canonical = directory.canonicalFilePath();
        if (canonical.isEmpty() || visited.contains(canonical))
            continue; // a symlink back to an ancestor (or one already seen)
        const QString configuration = directory.absoluteFilePath() + QStringLiteral("/metric.json");
        if (!QFileInfo(configuration).isFile())
            continue; // not a child: unrelated subdirectories are left alone
        visited.append(canonical);
        children.append(configuration);
        if (children.size() >= MaxGroupChildren)
            break;
    }
    return children;
}

} // namespace aqtruncat::custommetrics
