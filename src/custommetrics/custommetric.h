#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <optional>

namespace aqtruncat::custommetrics {

inline constexpr qint64 MaxJsonBytes = 1024 * 1024;
inline constexpr qsizetype MaxMetricEntries = 256;
inline constexpr qsizetype MaxStringCharacters = 4096;

enum class IconType {
    Theme,
    File,
};

// A v2 symbol reference. `value` is the spelling from the configuration;
// resolvedValue is identical for theme names and an absolute clean path for
// file icons. Legacy M6 string symbols remain in Snapshot::symbol.
struct IconReference {
    IconType type = IconType::Theme;
    QString value;
    QString resolvedValue;

    bool operator==(const IconReference &) const = default;
};

enum class State {
    Pending,
    Available,
    Unavailable,
    Error,
};

// Item ids are short, stable keys chosen by the producer.
inline constexpr qsizetype MaxItemIdCharacters = 64;
bool isValidItemId(const QString &id);

struct Item {
    QString title;
    QString formattedValue;
    std::optional<double> normalizedValue;
    // AQTRunCat extensions, all optional (RunCat Neo ignores unknown keys).
    // An id plus a metricsBarValue make the item selectable on its own in the
    // Metrics Bar; detail is a secondary line under the value; the label is
    // a short caption for the bar ("5h") shown instead of the source icon;
    // warning marks an item that needs the user's attention (out-of-date or
    // missing data), shown with a warning icon.
    QString id = {};
    QString detail = {};
    std::optional<QString> metricsBarValue = {};
    QString metricsBarLabel = {};
    bool warning = false;
};

// Shared presentation snapshot. M6 parses this from producer-owned RunCat Neo
// JSON; F1 also builds it from bounded command output. formattedValue and
// metricsBarValue remain opaque to consumers.
struct Snapshot {
    QString title;
    std::optional<QString> symbol;
    std::optional<IconReference> icon;
    std::optional<QString> metricsBarValue;
    QList<Item> metrics;
    QDateTime lastUpdatedDate;
    // v2F1.4: the configuration's optional display.showBar; false hides the
    // progress bars of this source's items. Missing (or true) = as before: a
    // bar wherever an item has a normalized value, never an invented one.
    bool showBar = true;
};

// v2F1.4: the optional display.showBar of a configuration object; a
// "display" that is not an object, or a non-boolean showBar, is an error.
bool parseShowBar(const QJsonObject &object, bool &showBar, QString &error);

enum class SourceType {
    Snapshot,
    Command,
    Group,    // v2F1.5: a container of child sources; never runs
};

struct Source {
    QString id;                 // canonical file identity
    QString path;               // user-facing absolute path
    QString explicitPath;       // persisted spelling, when explicitly registered
    bool explicitlyRegistered = false;
    bool discovered = false;
    SourceType type = SourceType::Snapshot;
    QString configuredId;       // command configuration id; empty for M6 snapshots
    // Official provider integration owning the source (CommandDefinition::
    // integration); empty = a user-defined Custom Metric. Never derived from
    // title, symbol or path; M6 snapshots are always user-defined.
    QString integration;
    // v2F1.2: the configuration's optional top-level "enabled" (missing =
    // true). A disabled source is never run and keeps no values (a snapshot
    // with only its title and icon), state Unavailable; presentation
    // surfaces leave it out, Settings still lists it to enable it again.
    bool enabled = true;
    // v2F1.5: the configuration's own "enabled" (enabled above is the
    // effective state: false also under a disabled group), and the source
    // id of the group this child belongs to (empty at the top level).
    bool configuredEnabled = true;
    QString parentId;
    State state = State::Pending;
    QString message;
    std::optional<Snapshot> snapshot; // last known good snapshot, retained on failure
};

struct ParseResult {
    std::optional<Snapshot> snapshot;
    QString error;
    bool enabled = true; // the snapshot file's top-level "enabled"
};

// v2F1.2: the optional top-level "enabled" of a configuration object.
// Missing means true; any other type than a boolean is an error.
bool parseEnabled(const QJsonObject &object, bool &enabled, QString &error);

// v2F1.2: writes "enabled": <enabled> into the configuration file at path,
// keeping every other byte of the file (key order, formatting, unrelated
// fields): the existing top-level value is replaced or the key is inserted
// first. The edited text is verified to parse to the same object plus the
// flag, then replaces the file atomically (QSaveFile: same directory, the
// file's permissions, through a symlink to its target). Returns false with
// *error when the file is unreadable, not a JSON object, or not writable.
bool writeEnabled(const QString &path, bool enabled, QString *error = nullptr);

// Shared by snapshot and command configurations: a legacy string symbol, or
// {type: theme|file, value}. Relative file paths resolve from baseDirectory
// (the configuration file's directory), never from the process CWD.
bool parseSymbol(const QJsonValue &value, const QString &baseDirectory,
                 std::optional<QString> &legacySymbol, std::optional<IconReference> &icon,
                 QString &error);

ParseResult parseJson(const QByteArray &json, const QString &baseDirectory = {});
ParseResult parseFile(const QString &path);

} // namespace aqtruncat::custommetrics
