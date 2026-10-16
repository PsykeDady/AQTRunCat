#pragma once

#include "custommetric.h"

#include <QByteArray>
#include <QStringList>

#include <optional>

namespace aqtruncat::custommetrics {

inline constexpr int MinimumCommandIntervalSeconds = 1;
inline constexpr int MaximumCommandIntervalSeconds = 24 * 60 * 60;
inline constexpr int DefaultCommandTimeoutSeconds = 10;
inline constexpr int MaximumCommandTimeoutSeconds = 5 * 60;
inline constexpr qsizetype MaxCommandOutputBytes = 16 * 1024;

struct NormalizedRange {
    double minimum = 0.0;
    double maximum = 1.0;

    bool operator==(const NormalizedRange &) const = default;
};

struct CommandDefinition {
    QString configurationPath;
    QString id;
    QString title;
    std::optional<IconReference> icon;
    std::optional<QString> legacySymbol;
    QString configuredExecutable;
    QString executable;
    QStringList arguments;
    QString workingDirectory;
    int intervalSeconds = MinimumCommandIntervalSeconds;
    int timeoutSeconds = DefaultCommandTimeoutSeconds;
    QString prefix;
    QString suffix;
    std::optional<NormalizedRange> normalizedRange;

    bool operator==(const CommandDefinition &) const = default;
};

// recognized is true only for an explicit source.type="command" object. Once
// recognized, every validation error is terminal and must never fall back to
// interpreting the file as an M6 snapshot.
struct CommandParseResult {
    bool recognized = false;
    std::optional<CommandDefinition> definition;
    QString error;
};

CommandParseResult parseCommandJson(const QByteArray &json, const QString &configurationPath);
CommandParseResult parseCommandFile(const QString &path);

struct ScalarResult {
    std::optional<Snapshot> snapshot;
    QString error;
};

ScalarResult snapshotFromCommandOutput(const CommandDefinition &definition,
                                       const QByteArray &standardOutput,
                                       const QDateTime &updatedAt = QDateTime::currentDateTimeUtc());

} // namespace aqtruncat::custommetrics
