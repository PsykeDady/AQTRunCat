#pragma once

#include <QByteArray>
#include <QDateTime>
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
};

enum class SourceType {
    Snapshot,
    Command,
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
    State state = State::Pending;
    QString message;
    std::optional<Snapshot> snapshot; // last known good snapshot, retained on failure
};

struct ParseResult {
    std::optional<Snapshot> snapshot;
    QString error;
};

// Shared by snapshot and command configurations: a legacy string symbol, or
// {type: theme|file, value}. Relative file paths resolve from baseDirectory
// (the configuration file's directory), never from the process CWD.
bool parseSymbol(const QJsonValue &value, const QString &baseDirectory,
                 std::optional<QString> &legacySymbol, std::optional<IconReference> &icon,
                 QString &error);

ParseResult parseJson(const QByteArray &json, const QString &baseDirectory = {});
ParseResult parseFile(const QString &path);

} // namespace aqtruncat::custommetrics
