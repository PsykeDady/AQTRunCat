#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace aqtruncat::custommetrics {

inline constexpr qint64 MaxJsonBytes = 1024 * 1024;
inline constexpr qsizetype MaxMetricEntries = 256;
inline constexpr qsizetype MaxStringCharacters = 4096;

enum class State {
    Pending,
    Available,
    Unavailable,
    Error,
};

struct Item {
    QString title;
    QString formattedValue;
    std::optional<double> normalizedValue;
};

// One producer-owned RunCat Neo compatible JSON snapshot. formattedValue and
// metricsBarValue are opaque presentation strings: AQTRunCat never attempts
// to infer raw units or recompute them.
struct Snapshot {
    QString title;
    std::optional<QString> symbol;
    std::optional<QString> metricsBarValue;
    QList<Item> metrics;
    QDateTime lastUpdatedDate;
};

struct Source {
    QString id;                 // canonical file identity
    QString path;               // user-facing absolute path
    QString explicitPath;       // persisted spelling, when explicitly registered
    bool explicitlyRegistered = false;
    bool discovered = false;
    State state = State::Pending;
    QString message;
    std::optional<Snapshot> snapshot; // last known good snapshot, retained on failure
};

struct ParseResult {
    std::optional<Snapshot> snapshot;
    QString error;
};

ParseResult parseJson(const QByteArray &json);
ParseResult parseFile(const QString &path);

} // namespace aqtruncat::custommetrics
