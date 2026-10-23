#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

// v2F1.5: groups of Custom Metrics. A group is a package's metric.json with
// "source": {"type": "group", ...}; it never runs. Its children are the
// subdirectories of its directory that hold a metric.json; each child
// inherits the group's configuration (recursively, through nested groups)
// and overrides what it declares. Qt Core only, no I/O beyond reading the
// configuration files.
namespace aqtruncat::custommetrics {

inline constexpr int MaxGroupDepth = 4;         // group → … → command, levels below the top group
inline constexpr int MaxGroupChildren = 256;    // children of one group

struct ConfigObject {
    std::optional<QJsonObject> object;
    QString error;
};

// A configuration file read as a JSON object (bounded like every config).
ConfigObject readConfigObject(const QString &path);

// True for an object whose source.type is "group".
bool isGroupObject(const QJsonObject &object);

// Objects merged recursively: the override's scalars and arrays replace the
// base's values (arrays as a whole: arguments are never concatenated),
// nested objects are merged key by key.
QJsonObject deepMerge(const QJsonObject &base, const QJsonObject &override);

// What a level passes down to its children: the merged configuration of its
// ancestors and itself (without "id" and "enabled", which are never
// inherited), where each relative path was declared, the hierarchical id
// and whether every ancestor is enabled.
struct Inheritance {
    QJsonObject object;
    QString executableBase;   // directory of the file that declared source.executable
    QString symbolBase;       // directory of the file that declared symbol.value
    QString idPrefix;         // "dice-metrics", "dice-metrics/sub"
    bool enabled = true;
    QString groupTitle;       // the nearest group's title (presentation)
};

// The inheritance of a top-level group from its own metric.json.
Inheritance rootInheritance(const QJsonObject &group, const QString &groupDirectory, const QString &groupId,
                            bool enabled);

// The inheritance a nested group passes to its own children.
Inheritance childInheritance(const Inheritance &parent, const QJsonObject &child, const QString &childDirectory,
                             const QString &childId, bool enabled);

// The child's id inside its group: its explicit "id" or, without one, its
// directory name; both must match [A-Za-z0-9][A-Za-z0-9._-]{0,99}.
bool childId(const QJsonObject &child, const QString &childDirectory, QString &id, QString &error);

// The child's effective configuration: the inherited object with the child
// merged over it, "id" = childId, "enabled" = the effective state, and the
// executable and a file symbol made absolute against the directory of the
// file that declared them (never the child's, unless the child declared
// them; never the process working directory). The files are not changed.
QJsonObject effectiveChild(const Inheritance &parent, const QJsonObject &child, const QString &childDirectory,
                           const QString &childId, bool enabled);

// The subdirectories of a group directory that hold a metric.json, sorted by
// name, without directories already visited (canonical paths: symlink
// cycles) and at most MaxGroupChildren.
QStringList childConfigurations(const QString &groupDirectory, QStringList &visited);

} // namespace aqtruncat::custommetrics
