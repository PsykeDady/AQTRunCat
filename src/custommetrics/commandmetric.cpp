#include "commandmetric.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringDecoder>

#include <cmath>

namespace aqtruncat::custommetrics {
namespace {

bool requiredString(const QJsonObject &object, const char *name, QString &out, QString &error)
{
    const QJsonValue value = object.value(QLatin1String(name));
    if (!value.isString()) {
        error = value.isUndefined()
            ? QStringLiteral("missing required field \"%1\"").arg(QLatin1String(name))
            : QStringLiteral("field \"%1\" must be a string").arg(QLatin1String(name));
        return false;
    }
    out = value.toString();
    if (out.isEmpty()) {
        error = QStringLiteral("field \"%1\" must not be empty").arg(QLatin1String(name));
        return false;
    }
    if (out.size() > MaxStringCharacters) {
        error = QStringLiteral("field \"%1\" exceeds the %2 character limit")
                    .arg(QLatin1String(name)).arg(MaxStringCharacters);
        return false;
    }
    return true;
}

bool optionalString(const QJsonObject &object, const char *name, QString &out, QString &error)
{
    const QJsonValue value = object.value(QLatin1String(name));
    if (value.isUndefined())
        return true;
    if (!value.isString()) {
        error = QStringLiteral("field \"%1\" must be a string").arg(QLatin1String(name));
        return false;
    }
    out = value.toString();
    if (out.size() > MaxStringCharacters) {
        error = QStringLiteral("field \"%1\" exceeds the %2 character limit")
                    .arg(QLatin1String(name)).arg(MaxStringCharacters);
        return false;
    }
    return true;
}

bool integerInRange(const QJsonObject &object, const char *name, int defaultValue,
                    int minimum, int maximum, int &out, QString &error)
{
    const QJsonValue value = object.value(QLatin1String(name));
    if (value.isUndefined()) {
        out = defaultValue;
        return true;
    }
    if (!value.isDouble() || !std::isfinite(value.toDouble())
        || std::floor(value.toDouble()) != value.toDouble()
        || value.toDouble() < minimum || value.toDouble() > maximum) {
        error = QStringLiteral("field \"%1\" must be an integer from %2 to %3")
                    .arg(QLatin1String(name)).arg(minimum).arg(maximum);
        return false;
    }
    out = int(value.toDouble());
    return true;
}

} // namespace

CommandParseResult parseCommandJson(const QByteArray &json, const QString &configurationPath)
{
    CommandParseResult result;
    if (json.size() > MaxJsonBytes) {
        result.error = QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes);
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return result; // The M6 parser provides the compatibility diagnostic.
    const QJsonObject object = document.object();
    const QJsonValue sourceValue = object.value(QStringLiteral("source"));
    if (!sourceValue.isObject()
        || sourceValue.toObject().value(QStringLiteral("type")) != QLatin1String("command"))
        return result;
    result.recognized = true;

    CommandDefinition definition;
    definition.configurationPath = QDir::cleanPath(QFileInfo(configurationPath).absoluteFilePath());
    const QString baseDirectory = QFileInfo(configurationPath).absolutePath();
    if (!requiredString(object, "id", definition.id, result.error)
        || !requiredString(object, "title", definition.title, result.error))
        return result;
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$"));
    if (!validId.match(definition.id).hasMatch()) {
        result.error = QStringLiteral("field \"id\" must match [A-Za-z0-9][A-Za-z0-9._-]{0,99}");
        return result;
    }
    if (!parseSymbol(object.value(QStringLiteral("symbol")), baseDirectory,
                     definition.legacySymbol, definition.icon, result.error))
        return result;
    const QJsonValue integration = object.value(QStringLiteral("integration"));
    if (!integration.isUndefined()) {
        if (!integration.isString() || !validId.match(integration.toString()).hasMatch()) {
            result.error = QStringLiteral("field \"integration\" must match [A-Za-z0-9][A-Za-z0-9._-]{0,99}");
            return result;
        }
        definition.integration = integration.toString();
    }
    if (!parseEnabled(object, definition.enabled, result.error))
        return result;

    const QJsonObject source = sourceValue.toObject();
    if (!requiredString(source, "executable", definition.configuredExecutable, result.error))
        return result;
    const QFileInfo executableInfo(definition.configuredExecutable);
    definition.executable = QDir::cleanPath(executableInfo.isAbsolute()
        ? executableInfo.absoluteFilePath()
        : QDir(baseDirectory).absoluteFilePath(definition.configuredExecutable));
    definition.workingDirectory = baseDirectory;

    const QJsonValue arguments = source.value(QStringLiteral("arguments"));
    if (!arguments.isUndefined()) {
        if (!arguments.isArray()) {
            result.error = QStringLiteral("field \"arguments\" must be an array of strings");
            return result;
        }
        if (arguments.toArray().size() > 256) {
            result.error = QStringLiteral("field \"arguments\" exceeds the 256 entry limit");
            return result;
        }
        for (const QJsonValue &argument : arguments.toArray()) {
            if (!argument.isString() || argument.toString().size() > MaxStringCharacters) {
                result.error = QStringLiteral("field \"arguments\" must contain only bounded strings");
                return result;
            }
            definition.arguments.append(argument.toString());
        }
    }
    if (!integerInRange(source, "intervalSeconds", MinimumCommandIntervalSeconds,
                        MinimumCommandIntervalSeconds, MaximumCommandIntervalSeconds,
                        definition.intervalSeconds, result.error)
        || !integerInRange(source, "timeoutSeconds", DefaultCommandTimeoutSeconds,
                           MinimumCommandIntervalSeconds, MaximumCommandTimeoutSeconds,
                           definition.timeoutSeconds, result.error))
        return result;

    const QJsonValue outputValue = source.value(QStringLiteral("output"));
    if (!outputValue.isUndefined()) {
        if (outputValue == QLatin1String("scalar")) {
            definition.output = CommandOutput::Scalar;
        } else if (outputValue == QLatin1String("snapshot")) {
            definition.output = CommandOutput::Snapshot;
        } else {
            result.error = QStringLiteral("field \"output\" must be \"scalar\" or \"snapshot\"");
            return result;
        }
    }

    const QJsonValue displayValue = object.value(QStringLiteral("display"));
    if (!parseShowBar(object, definition.showBar, result.error))
        return result;
    if (!displayValue.isUndefined() && definition.output == CommandOutput::Snapshot) {
        // A snapshot carries its own formatted values and normalized values;
        // only whether its bars are drawn (v2F1.4) belongs to the configuration.
        const QStringList keys = displayValue.toObject().keys();
        if (!keys.isEmpty() && keys != QStringList{QStringLiteral("showBar")}) {
            result.error = QStringLiteral("field \"display\" applies only to scalar output (except \"showBar\")");
            return result;
        }
    }
    if (!displayValue.isUndefined()) {
        if (!displayValue.isObject()) {
            result.error = QStringLiteral("field \"display\" must be an object");
            return result;
        }
        const QJsonObject display = displayValue.toObject();
        if (!optionalString(display, "prefix", definition.prefix, result.error)
            || !optionalString(display, "suffix", definition.suffix, result.error))
            return result;
        const QJsonValue rangeValue = display.value(QStringLiteral("normalizedRange"));
        if (!rangeValue.isUndefined()) {
            if (!rangeValue.isObject()) {
                result.error = QStringLiteral("field \"display.normalizedRange\" must be an object");
                return result;
            }
            const QJsonObject range = rangeValue.toObject();
            const QJsonValue minimum = range.value(QStringLiteral("minimum"));
            const QJsonValue maximum = range.value(QStringLiteral("maximum"));
            if (!minimum.isDouble() || !maximum.isDouble()
                || !std::isfinite(minimum.toDouble()) || !std::isfinite(maximum.toDouble())
                || maximum.toDouble() <= minimum.toDouble()) {
                result.error = QStringLiteral("display.normalizedRange requires finite minimum < maximum");
                return result;
            }
            definition.normalizedRange = NormalizedRange{minimum.toDouble(), maximum.toDouble()};
        }
    }
    result.definition = std::move(definition);
    return result;
}

CommandParseResult parseCommandFile(const QString &path)
{
    QFile file(path);
    CommandParseResult result;
    if (!file.open(QIODevice::ReadOnly))
        return result;
    const QByteArray bytes = file.read(MaxJsonBytes + 1);
    if (bytes.size() > MaxJsonBytes) {
        result.error = QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes);
        return result;
    }
    return parseCommandJson(bytes, path);
}

ScalarResult snapshotFromCommandOutput(const CommandDefinition &definition,
                                       const QByteArray &standardOutput,
                                       const QDateTime &updatedAt)
{
    ScalarResult result;
    if (standardOutput.size() > MaxCommandOutputBytes) {
        result.error = QStringLiteral("standard output exceeds the %1 byte limit")
                           .arg(MaxCommandOutputBytes);
        return result;
    }
    if (definition.output == CommandOutput::Snapshot) {
        ParseResult parsed = parseJson(standardOutput, QFileInfo(definition.configurationPath).absolutePath());
        if (!parsed.snapshot) {
            result.error = QStringLiteral("standard output is not a valid snapshot: %1").arg(parsed.error);
            return result;
        }
        // The configuration owns the source identity, as for scalar output:
        // title and symbol stay stable across runs and in Settings.
        parsed.snapshot->title = definition.title;
        parsed.snapshot->symbol = definition.legacySymbol;
        parsed.snapshot->icon = definition.icon;
        parsed.snapshot->showBar = definition.showBar;
        result.snapshot = std::move(parsed.snapshot);
        return result;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(standardOutput);
    if (decoder.hasError()) {
        result.error = QStringLiteral("standard output is not valid UTF-8");
        return result;
    }
    const QString scalar = decoded.trimmed();
    if (scalar.isEmpty()) {
        result.error = QStringLiteral("standard output is empty");
        return result;
    }
    if (scalar.contains(u'\n') || scalar.contains(u'\r')) {
        result.error = QStringLiteral("standard output must contain exactly one scalar line");
        return result;
    }
    const QString formatted = definition.prefix + scalar + definition.suffix;
    if (formatted.size() > MaxStringCharacters) {
        result.error = QStringLiteral("formatted value exceeds the %1 character limit")
                           .arg(MaxStringCharacters);
        return result;
    }

    Item item;
    item.title = definition.title;
    item.formattedValue = formatted;
    if (definition.normalizedRange) {
        bool ok = false;
        const double value = scalar.toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            result.error = QStringLiteral("standard output must be numeric when normalizedRange is configured");
            return result;
        }
        const auto &range = *definition.normalizedRange;
        item.normalizedValue = (value - range.minimum) / (range.maximum - range.minimum);
    }

    Snapshot snapshot;
    snapshot.title = definition.title;
    snapshot.symbol = definition.legacySymbol;
    snapshot.icon = definition.icon;
    snapshot.metricsBarValue = formatted;
    snapshot.metrics.append(std::move(item));
    snapshot.lastUpdatedDate = updatedAt;
    snapshot.showBar = definition.showBar;
    result.snapshot = std::move(snapshot);
    return result;
}

} // namespace aqtruncat::custommetrics
