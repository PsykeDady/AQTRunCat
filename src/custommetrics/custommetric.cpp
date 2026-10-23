#include "custommetric.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <utility>

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

bool isValidItemId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

bool parseSymbol(const QJsonValue &value, const QString &baseDirectory,
                 std::optional<QString> &legacySymbol, std::optional<IconReference> &icon,
                 QString &error)
{
    if (value.isUndefined() || value.isNull())
        return true;
    if (value.isString()) {
        const QString text = value.toString();
        if (text.size() > MaxStringCharacters) {
            error = QStringLiteral("field \"symbol\" exceeds the %1 character limit")
                        .arg(MaxStringCharacters);
            return false;
        }
        legacySymbol = text;
        return true;
    }
    if (!value.isObject()) {
        error = QStringLiteral("field \"symbol\" must be a string, object or null");
        return false;
    }
    const QJsonObject object = value.toObject();
    QString type;
    QString symbolValue;
    if (!requiredString(object, "type", type, error)
        || !requiredString(object, "value", symbolValue, error))
        return false;
    if (symbolValue.trimmed().isEmpty()) {
        error = QStringLiteral("field \"symbol.value\" must not be empty");
        return false;
    }

    IconReference reference;
    reference.value = symbolValue;
    if (type == QLatin1String("theme")) {
        reference.type = IconType::Theme;
        reference.resolvedValue = symbolValue;
        // Preserve the existing textual SF-symbol fallback where one exists.
        legacySymbol = symbolValue;
    } else if (type == QLatin1String("file")) {
        reference.type = IconType::File;
        const QFileInfo info(symbolValue);
        reference.resolvedValue = QDir::cleanPath(info.isAbsolute()
            ? info.absoluteFilePath()
            : QDir(baseDirectory).absoluteFilePath(symbolValue));
    } else {
        error = QStringLiteral("field \"symbol.type\" must be \"theme\" or \"file\"");
        return false;
    }
    icon = std::move(reference);
    return true;
}

ParseResult parseJson(const QByteArray &json, const QString &baseDirectory)
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
    if (!parseEnabled(object, result.enabled, result.error))
        return result;
    Snapshot snapshot;
    if (!requiredString(object, "title", snapshot.title, result.error))
        return result;
    if (!parseSymbol(object.value(QStringLiteral("symbol")), baseDirectory,
                     snapshot.symbol, snapshot.icon, result.error))
        return result;
    if (!stringField(object, "metricsBarValue", false, snapshot.metricsBarValue, result.error))
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
        std::optional<QString> id;
        std::optional<QString> detail;
        std::optional<QString> barLabel;
        if (!stringField(itemObject, "id", false, id, fieldError)
            || !stringField(itemObject, "detail", false, detail, fieldError)
            || !stringField(itemObject, "metricsBarValue", false, item.metricsBarValue, fieldError)
            || !stringField(itemObject, "metricsBarLabel", false, barLabel, fieldError)) {
            result.error = QStringLiteral("metrics[%1]: %2").arg(index).arg(fieldError);
            return result;
        }
        if (id) {
            if (!isValidItemId(*id)) {
                result.error = QStringLiteral("metrics[%1].id must match [A-Za-z0-9][A-Za-z0-9._-]{0,63}").arg(index);
                return result;
            }
            const bool duplicate = std::any_of(snapshot.metrics.cbegin(), snapshot.metrics.cend(),
                                               [&](const Item &other) { return other.id == *id; });
            if (duplicate) {
                result.error = QStringLiteral("metrics[%1].id \"%2\" is not unique").arg(index).arg(*id);
                return result;
            }
            item.id = *id;
        }
        item.detail = detail.value_or(QString());
        const QJsonValue warning = itemObject.value(QStringLiteral("warning"));
        if (!warning.isUndefined() && !warning.isNull() && !warning.isBool()) {
            result.error = QStringLiteral("metrics[%1].warning must be a boolean").arg(index);
            return result;
        }
        item.warning = warning.toBool();
        item.metricsBarLabel = barLabel.value_or(QString());
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
    return parseJson(bytes, QFileInfo(path).absolutePath());
}

bool parseEnabled(const QJsonObject &object, bool &enabled, QString &error)
{
    const QJsonValue value = object.value(QStringLiteral("enabled"));
    if (value.isUndefined()) {
        enabled = true;
        return true;
    }
    if (!value.isBool()) {
        error = QStringLiteral("field \"enabled\" must be a boolean");
        return false;
    }
    enabled = value.toBool();
    return true;
}

namespace {

int skipSpace(const QByteArray &text, int at)
{
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r'))
        ++at;
    return at;
}

// End (one past the closing quote) of the string starting at text[at] == '"'.
int skipString(const QByteArray &text, int at)
{
    for (++at; at < text.size(); ++at) {
        if (text[at] == '\\')
            ++at;
        else if (text[at] == '"')
            return at + 1;
    }
    return -1;
}

// End of the JSON value starting at text[at].
int skipValue(const QByteArray &text, int at)
{
    if (at >= text.size())
        return -1;
    if (text[at] == '"')
        return skipString(text, at);
    if (text[at] == '{' || text[at] == '[') {
        int depth = 0;
        while (at < text.size()) {
            const char c = text[at];
            if (c == '"') {
                at = skipString(text, at);
                if (at < 0)
                    return -1;
                continue;
            }
            if (c == '{' || c == '[')
                ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0)
                    return at + 1;
            }
            ++at;
        }
        return -1;
    }
    while (at < text.size() && !QByteArrayView(",}] \t\r\n").contains(text[at]))
        ++at;
    return at;
}

// The text with "enabled" set: the top-level value replaced in place, or
// the key inserted first with the indentation of the existing first key.
std::optional<QByteArray> withEnabled(const QByteArray &text, bool enabled)
{
    const QByteArray literal = enabled ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
    int open = text.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    open = skipSpace(text, open);
    if (open >= text.size() || text[open] != '{')
        return std::nullopt;
    int at = skipSpace(text, open + 1);
    const int firstKey = at;
    std::optional<std::pair<int, int>> found;
    while (at < text.size() && text[at] == '"') {
        const int keyEnd = skipString(text, at);
        if (keyEnd < 0)
            return std::nullopt;
        const bool isEnabled = text.mid(at, keyEnd - at) == QByteArrayLiteral("\"enabled\"");
        at = skipSpace(text, keyEnd);
        if (at >= text.size() || text[at] != ':')
            return std::nullopt;
        at = skipSpace(text, at + 1);
        const int valueEnd = skipValue(text, at);
        if (valueEnd < 0)
            return std::nullopt;
        if (isEnabled)
            found = std::pair{at, valueEnd};
        at = skipSpace(text, valueEnd);
        if (at < text.size() && text[at] == ',')
            at = skipSpace(text, at + 1);
    }
    QByteArray edited = text;
    if (found) {
        edited.replace(found->first, found->second - found->first, literal);
    } else if (firstKey < text.size() && text[firstKey] == '"') {
        const QByteArray indent = text.mid(open + 1, firstKey - open - 1);
        edited.insert(open + 1, indent + "\"enabled\": " + literal + ',');
    } else {
        edited.insert(open + 1, "\"enabled\": " + literal);
    }
    return edited;
}

} // namespace

bool writeEnabled(const QString &path, bool enabled, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot read %1: %2").arg(path, file.errorString()));
    const QByteArray text = file.read(MaxJsonBytes + 1);
    file.close();
    if (text.size() > MaxJsonBytes)
        return fail(QStringLiteral("JSON exceeds the %1 byte limit").arg(MaxJsonBytes));
    QJsonParseError parseError;
    const QJsonDocument original = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError || !original.isObject())
        return fail(QStringLiteral("%1 is not a JSON object").arg(path));

    QJsonObject expected = original.object();
    expected.insert(QStringLiteral("enabled"), enabled);
    const std::optional<QByteArray> edited = withEnabled(text, enabled);
    if (!edited || QJsonDocument::fromJson(*edited).object() != expected)
        return fail(QStringLiteral("cannot update \"enabled\" in %1 without changing other content").arg(path));
    if (*edited == text)
        return true;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
    if (out.write(*edited) != edited->size() || !out.commit())
        return fail(QStringLiteral("cannot write %1: %2").arg(path, out.errorString()));
    return true;
}

} // namespace aqtruncat::custommetrics
