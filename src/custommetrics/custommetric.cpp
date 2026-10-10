#include "custommetric.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <cmath>

namespace aqtruncat::custommetrics {
namespace {

bool stringField(const QJsonObject &object, const char *name, bool required,
                 std::optional<QString> &out, QString &error)
{
    const auto value = object.value(QLatin1String(name));
    if (value.isUndefined() || value.isNull()) {
        if (required)
            error = QStringLiteral("missing required field \"%1\"").arg(QLatin1String(name));
        return !required;
    }
    if (!value.isString()) {
        error = QStringLiteral("field \"%1\" must be a string%2")
                    .arg(QLatin1String(name), required ? QString() : QStringLiteral(" or null"));
        return false;
    }
    const QString text = value.toString();
    if (text.size() > MaxStringCharacters) {
        error = QStringLiteral("field \"%1\" exceeds the %2 character limit")
                    .arg(QLatin1String(name)).arg(MaxStringCharacters);
        return false;
    }
    out = text;
    return true;
}

bool requiredString(const QJsonObject &object, const char *name, QString &out, QString &error)
{
    std::optional<QString> value;
    if (!stringField(object, name, true, value, error))
        return false;
    out = *value;
    return true;
}

} // namespace

ParseResult parseJson(const QByteArray &json)
{
    ParseResult result;
    if (json.size() > MaxJsonBytes) {
        result.error = QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("invalid JSON at offset %1: %2")
                           .arg(parseError.offset).arg(parseError.errorString());
        return result;
    }
    if (!document.isObject()) {
        result.error = QStringLiteral("top-level JSON value must be an object");
        return result;
    }

    const QJsonObject object = document.object();
    Snapshot snapshot;
    if (!requiredString(object, "title", snapshot.title, result.error))
        return result;
    if (!stringField(object, "symbol", false, snapshot.symbol, result.error)
        || !stringField(object, "metricsBarValue", false, snapshot.metricsBarValue, result.error))
        return result;

    const QJsonValue metrics = object.value(QStringLiteral("metrics"));
    if (!metrics.isArray()) {
        result.error = metrics.isUndefined()
            ? QStringLiteral("missing required field \"metrics\"")
            : QStringLiteral("field \"metrics\" must be an array");
        return result;
    }
    if (metrics.toArray().size() > MaxMetricEntries) {
        result.error = QStringLiteral("field \"metrics\" exceeds the %1 entry limit").arg(MaxMetricEntries);
        return result;
    }
    int index = 0;
    for (const QJsonValue &value : metrics.toArray()) {
        if (!value.isObject()) {
            result.error = QStringLiteral("metrics[%1] must be an object").arg(index);
            return result;
        }
        const QJsonObject itemObject = value.toObject();
        Item item;
        QString fieldError;
        if (!requiredString(itemObject, "title", item.title, fieldError)
            || !requiredString(itemObject, "formattedValue", item.formattedValue, fieldError)) {
            result.error = QStringLiteral("metrics[%1]: %2").arg(index).arg(fieldError);
            return result;
        }
        const QJsonValue normalized = itemObject.value(QStringLiteral("normalizedValue"));
        if (!normalized.isUndefined() && !normalized.isNull()) {
            if (!normalized.isDouble() || !std::isfinite(normalized.toDouble())) {
                result.error = QStringLiteral("metrics[%1].normalizedValue must be a number or null").arg(index);
                return result;
            }
            item.normalizedValue = normalized.toDouble();
        }
        snapshot.metrics.append(std::move(item));
        ++index;
    }

    QString dateText;
    if (!requiredString(object, "lastUpdatedDate", dateText, result.error))
        return result;
    static const QRegularExpression isoTimestamp(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?(?:Z|[+-]\\d{2}:\\d{2})$"));
    if (!isoTimestamp.match(dateText).hasMatch()) {
        result.error = QStringLiteral("field \"lastUpdatedDate\" must be an ISO 8601 timestamp");
        return result;
    }
    snapshot.lastUpdatedDate = QDateTime::fromString(dateText, Qt::ISODate);
    if (!snapshot.lastUpdatedDate.isValid())
        snapshot.lastUpdatedDate = QDateTime::fromString(dateText, Qt::ISODateWithMs);
    if (!snapshot.lastUpdatedDate.isValid()) {
        result.error = QStringLiteral("field \"lastUpdatedDate\" must be an ISO 8601 timestamp");
        return result;
    }
    result.snapshot = std::move(snapshot);
    return result;
}

ParseResult parseFile(const QString &path)
{
    QFile file(path);
    ParseResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    const QByteArray bytes = file.read(MaxJsonBytes + 1);
    if (bytes.size() > MaxJsonBytes) {
        result.error = QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes);
        return result;
    }
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return result;
    }
    return parseJson(bytes);
}

} // namespace aqtruncat::custommetrics
