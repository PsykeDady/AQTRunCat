#include "statuslinebridge.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSaveFile>

namespace aqtruncat::claude {
namespace {

const QString StatusLineKey = QStringLiteral("statusLine");
const QString BridgeArgument = QStringLiteral("statusline");
const QString ExecutableName = QStringLiteral("aqtruncat-claude-metric");

// --- minimal JSON scanner: positions of the top-level members -------------

struct Member {
    QString key;
    qsizetype keyStart = 0;   // opening quote of the key
    qsizetype valueStart = 0;
    qsizetype valueEnd = 0;   // one past the value
    qsizetype nextStart = -1; // key of the following member, -1 for the last
};

struct ObjectLayout {
    qsizetype open = 0;  // '{'
    qsizetype close = 0; // '}'
    QList<Member> members;
};

class Scanner
{
public:
    explicit Scanner(const QByteArray &text) : m_text(text) {}

    std::optional<ObjectLayout> topLevelObject()
    {
        ObjectLayout layout;
        skipSpace();
        if (!peek('{'))
            return std::nullopt;
        layout.open = m_pos++;
        skipSpace();
        if (peek('}')) {
            layout.close = m_pos++;
            return finish(layout);
        }
        for (;;) {
            skipSpace();
            Member member;
            member.keyStart = m_pos;
            const auto key = string();
            if (!key)
                return std::nullopt;
            member.key = *key;
            skipSpace();
            if (!peek(':'))
                return std::nullopt;
            ++m_pos;
            skipSpace();
            member.valueStart = m_pos;
            if (!value())
                return std::nullopt;
            member.valueEnd = m_pos;
            skipSpace();
            if (!layout.members.isEmpty())
                layout.members.last().nextStart = member.keyStart;
            layout.members.append(member);
            if (peek(',')) {
                ++m_pos;
                continue;
            }
            if (peek('}')) {
                layout.close = m_pos++;
                return finish(layout);
            }
            return std::nullopt;
        }
    }

private:
    std::optional<ObjectLayout> finish(const ObjectLayout &layout)
    {
        skipSpace();
        if (m_pos != m_text.size())
            return std::nullopt;
        return layout;
    }

    bool peek(char c) const { return m_pos < m_text.size() && m_text.at(m_pos) == c; }

    void skipSpace()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text.at(m_pos);
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                return;
            ++m_pos;
        }
    }

    std::optional<QString> string()
    {
        if (!peek('"'))
            return std::nullopt;
        const qsizetype start = m_pos++;
        while (m_pos < m_text.size()) {
            const char c = m_text.at(m_pos++);
            if (c == '\\') {
                ++m_pos;
            } else if (c == '"') {
                const QByteArray literal = m_text.mid(start, m_pos - start);
                const QJsonDocument document = QJsonDocument::fromJson("[" + literal + "]");
                return document.isArray() ? std::optional<QString>(document.array().at(0).toString()) : std::nullopt;
            }
        }
        return std::nullopt;
    }

    bool value()
    {
        if (peek('"'))
            return string().has_value();
        if (peek('{') || peek('[')) {
            int depth = 0;
            while (m_pos < m_text.size()) {
                const char c = m_text.at(m_pos);
                if (c == '"') {
                    if (!string())
                        return false;
                    continue;
                }
                ++m_pos;
                if (c == '{' || c == '[')
                    ++depth;
                else if ((c == '}' || c == ']') && --depth == 0)
                    return true;
            }
            return false;
        }
        const qsizetype start = m_pos;
        while (m_pos < m_text.size()) {
            const char c = m_text.at(m_pos);
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
                break;
            ++m_pos;
        }
        return m_pos > start;
    }

    const QByteArray &m_text;
    qsizetype m_pos = 0;
};

// Indentation of the line holding position, e.g. "  " for a 2-space file.
QByteArray indentationAt(const QByteArray &text, qsizetype position)
{
    qsizetype lineStart = position;
    while (lineStart > 0 && text.at(lineStart - 1) != '\n')
        --lineStart;
    QByteArray indent;
    for (qsizetype i = lineStart; i < position && (text.at(i) == ' ' || text.at(i) == '\t'); ++i)
        indent += text.at(i);
    return indent;
}

QByteArray serialized(const QJsonValue &value)
{
    // Compact and on one line: the member is easy to recognise and remove.
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2);
}

QByteArray readSettings(const QString &path, bool &exists, QString &error)
{
    exists = QFileInfo::exists(path);
    if (!exists)
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    const QByteArray bytes = file.read(MaximumSettingsBytes + 1);
    if (bytes.size() > MaximumSettingsBytes)
        error = QStringLiteral("%1 is larger than %2 bytes").arg(path).arg(MaximumSettingsBytes);
    return bytes;
}

std::optional<QJsonObject> parseObject(const QByteArray &bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *message, bool privateFile = false)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (message)
            *message = QStringLiteral("cannot create %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    // QSaveFile writes through a symlink to its target and keeps the
    // permissions of an existing file.
    QSaveFile file(path);
    bool ok = file.open(QIODevice::WriteOnly);
    if (ok && privateFile)
        ok = file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    ok = ok && file.write(bytes) == bytes.size() && file.commit();
    if (!ok && message)
        *message = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    return ok;
}

// The edited text must parse to exactly the original object with only the
// statusLine member changed; anything else means the textual edit misread
// the file, and nothing is written.
bool verifiedEdit(const QJsonObject &original, const QByteArray &edited, const std::optional<QJsonValue> &value)
{
    const std::optional<QJsonObject> result = parseObject(edited);
    if (!result)
        return false;
    QJsonObject expected = original;
    if (value)
        expected.insert(StatusLineKey, *value);
    else
        expected.remove(StatusLineKey);
    return *result == expected;
}

} // namespace

std::optional<QByteArray> replaceMember(const QByteArray &json, const QString &key,
                                        const std::optional<QJsonValue> &value)
{
    return replaceMemberText(json, key, value ? std::optional<QByteArray>(serialized(*value)) : std::nullopt);
}

std::optional<QByteArray> memberText(const QByteArray &json, const QString &key)
{
    Scanner scanner(json);
    const std::optional<ObjectLayout> layout = scanner.topLevelObject();
    if (!layout)
        return std::nullopt;
    for (const Member &member : layout->members) {
        if (member.key == key)
            return json.mid(member.valueStart, member.valueEnd - member.valueStart);
    }
    return std::nullopt;
}

std::optional<QByteArray> replaceMemberText(const QByteArray &json, const QString &key,
                                            const std::optional<QByteArray> &value)
{
    Scanner scanner(json);
    const std::optional<ObjectLayout> layout = scanner.topLevelObject();
    if (!layout)
        return std::nullopt;
    int found = -1;
    for (int i = 0; i < layout->members.size(); ++i) {
        if (layout->members.at(i).key == key) {
            if (found >= 0)
                return std::nullopt; // duplicate keys: ambiguous, do not touch
            found = i;
        }
    }
    QByteArray text = json;
    if (found >= 0) {
        const Member &member = layout->members.at(found);
        if (value)
            return text.replace(member.valueStart, member.valueEnd - member.valueStart, *value);
        if (member.nextStart >= 0)
            return text.remove(member.keyStart, member.nextStart - member.keyStart);
        if (found > 0) {
            const qsizetype from = layout->members.at(found - 1).valueEnd;
            return text.remove(from, member.valueEnd - from);
        }
        return text.remove(member.keyStart, member.valueEnd - member.keyStart);
    }
    if (!value)
        return text;
    const QByteArray entry = serialized(QJsonValue(key)) + ": " + *value;
    if (layout->members.isEmpty())
        return text.replace(layout->open, layout->close - layout->open + 1, "{\n  " + entry + "\n}");
    // Insert as the first member, on its own line with the file's indentation.
    const qsizetype first = layout->members.first().keyStart;
    const bool multiline = json.mid(layout->open, first - layout->open).contains('\n');
    const QByteArray indent = multiline ? indentationAt(json, first) : QByteArray(" ");
    return text.insert(first, entry + (multiline ? ",\n" + indent : QByteArray(", ")));
}

QString shellQuote(const QString &text)
{
    QString quoted = text;
    quoted.replace(u'\'', QStringLiteral("'\\''"));
    return u'\'' + quoted + u'\'';
}

bool isBridgeStatusLine(const QJsonValue &statusLine)
{
    const QJsonObject object = statusLine.toObject();
    const QString command = object.value(QStringLiteral("command")).toString().trimmed();
    return object.value(QStringLiteral("type")) == QLatin1String("command")
        && command.endsWith(u' ' + BridgeArgument)
        && command.contains(ExecutableName);
}

BridgeStatus bridgeStatus(const BridgePaths &paths)
{
    BridgeStatus status;
    bool exists = false;
    const QByteArray bytes = readSettings(paths.settingsFile, exists, status.error);
    if (!status.error.isEmpty())
        return status;
    std::optional<QJsonObject> settings = exists ? parseObject(bytes) : QJsonObject();
    if (!settings) {
        status.error = QStringLiteral("%1 is not a valid JSON object").arg(paths.settingsFile);
        return status;
    }
    const QJsonValue statusLine = settings->value(StatusLineKey);
    if (isBridgeStatusLine(statusLine)) {
        status.state = BridgeState::On;
        status.chained = chainedCommand(paths.backupFile).has_value();
    } else {
        status.otherStatusLine = !statusLine.isUndefined() && !statusLine.isNull();
        status.state = QFileInfo::exists(paths.backupFile) ? BridgeState::Replaced : BridgeState::Off;
    }
    return status;
}

bool installBridge(const BridgePaths &paths, QString *message)
{
    bool exists = false;
    QString error;
    const QByteArray bytes = readSettings(paths.settingsFile, exists, error);
    if (!error.isEmpty()) {
        if (message)
            *message = error + QStringLiteral("; nothing changed");
        return false;
    }
    const QByteArray original = exists ? bytes : QByteArray("{}");
    const std::optional<QJsonObject> settings = parseObject(original);
    if (!settings) {
        if (message)
            *message = QStringLiteral("%1 is not a valid JSON object; nothing changed").arg(paths.settingsFile);
        return false;
    }
    const QJsonValue previous = settings->value(StatusLineKey);
    QJsonObject statusLine{
        {QStringLiteral("type"), QStringLiteral("command")},
        {QStringLiteral("command"), shellQuote(paths.executable) + u' ' + BridgeArgument},
    };
    if (isBridgeStatusLine(previous)) {
        QJsonObject current = previous.toObject();
        if (current.value(QStringLiteral("command")) == statusLine.value(QStringLiteral("command"))) {
            if (message)
                *message = QStringLiteral("Live updates are already on.");
            return true;
        }
        // On already, but for an adapter at another path (AQTRunCat was
        // reinstalled elsewhere): only the command changes, the backup of the
        // user's own status line stays as it is.
        current.insert(QStringLiteral("command"), statusLine.value(QStringLiteral("command")));
        const std::optional<QByteArray> edited = replaceMember(original, StatusLineKey, QJsonValue(current));
        if (!edited || !verifiedEdit(*settings, *edited, QJsonValue(current))) {
            if (message)
                *message = QStringLiteral("cannot edit %1 safely; nothing changed").arg(paths.settingsFile);
            return false;
        }
        if (!writeFile(paths.settingsFile, *edited, message))
            return false;
        if (message)
            *message = QStringLiteral("Live updates are on (status line updated to %1).").arg(paths.executable);
        return true;
    }

    // Keep the presentation options of the status line being wrapped.
    const QJsonObject previousObject = previous.toObject();
    for (const QString &option : {QStringLiteral("padding"), QStringLiteral("refreshInterval")}) {
        if (previousObject.contains(option))
            statusLine.insert(option, previousObject.value(option));
    }
    const std::optional<QByteArray> edited = replaceMember(original, StatusLineKey, QJsonValue(statusLine));
    if (!edited || !verifiedEdit(*settings, *edited, QJsonValue(statusLine))) {
        if (message)
            *message = QStringLiteral("cannot edit %1 safely; nothing changed").arg(paths.settingsFile);
        return false;
    }

    // The backup is written first: if anything fails later the user's
    // previous status line is still on record.
    const QJsonObject backup{
        {QStringLiteral("version"), 1},
        {QStringLiteral("settingsFile"), paths.settingsFile},
        {QStringLiteral("settingsFileExisted"), exists},
        {QStringLiteral("hadStatusLine"), !previous.isUndefined()},
        {QStringLiteral("statusLine"), previous.isUndefined() ? QJsonValue() : previous},
        // Its exact spelling, so that turning live updates off restores the
        // very same bytes.
        {QStringLiteral("statusLineText"),
         QString::fromUtf8(memberText(original, StatusLineKey).value_or(QByteArray()))},
        {QStringLiteral("installedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    };
    if (!writeFile(paths.backupFile, QJsonDocument(backup).toJson(QJsonDocument::Indented), message, true))
        return false;
    if (!writeFile(paths.settingsFile, *edited, message)) {
        QFile::remove(paths.backupFile);
        return false;
    }
    if (message) {
        *message = previous.isUndefined()
            ? QStringLiteral("Live updates are on (%1 now runs the AQTRunCat status line).").arg(paths.settingsFile)
            : QStringLiteral("Live updates are on; your previous status line still runs after AQTRunCat and is "
                             "restored when live updates are turned off (backup: %1).").arg(paths.backupFile);
    }
    return true;
}

bool removeBridge(const BridgePaths &paths, QString *message)
{
    bool exists = false;
    QString error;
    const QByteArray bytes = readSettings(paths.settingsFile, exists, error);
    if (!error.isEmpty()) {
        if (message)
            *message = error + QStringLiteral("; nothing changed");
        return false;
    }
    const std::optional<QJsonObject> settings = exists ? parseObject(bytes) : QJsonObject();
    if (!settings) {
        if (message)
            *message = QStringLiteral("%1 is not a valid JSON object; nothing changed").arg(paths.settingsFile);
        return false;
    }
    QFile backupFile(paths.backupFile);
    const QJsonObject backup = backupFile.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(backupFile.read(MaximumSettingsBytes)).object() : QJsonObject();
    backupFile.close();

    if (!isBridgeStatusLine(settings->value(StatusLineKey))) {
        // Changed by the user (or never on): their status line is theirs.
        QFile::remove(paths.backupFile);
        if (message) {
            *message = backup.isEmpty() ? QStringLiteral("Live updates are off.")
                                        : QStringLiteral("Live updates are off; %1 no longer used the AQTRunCat status "
                                                         "line and was left unchanged.").arg(paths.settingsFile);
        }
        return true;
    }

    std::optional<QJsonValue> restored;
    std::optional<QByteArray> restoredText;
    if (backup.value(QStringLiteral("hadStatusLine")).toBool()) {
        restored = backup.value(QStringLiteral("statusLine"));
        // The recorded spelling when it still means the recorded value.
        const QByteArray text = backup.value(QStringLiteral("statusLineText")).toString().toUtf8();
        const QJsonDocument wrapped = QJsonDocument::fromJson("[" + text + "]");
        restoredText = !text.isEmpty() && wrapped.isArray() && wrapped.array().size() == 1
                && wrapped.array().at(0) == *restored
            ? text : serialized(*restored);
    }
    const std::optional<QByteArray> edited = replaceMemberText(bytes, StatusLineKey, restoredText);
    if (!edited || !verifiedEdit(*settings, *edited, restored)) {
        if (message)
            *message = QStringLiteral("cannot edit %1 safely; nothing changed").arg(paths.settingsFile);
        return false;
    }
    const bool createdByUs = !backup.isEmpty() && !backup.value(QStringLiteral("settingsFileExisted")).toBool(true);
    const std::optional<QJsonObject> remaining = parseObject(*edited);
    if (createdByUs && remaining && remaining->isEmpty()) {
        if (!QFile::remove(paths.settingsFile)) {
            if (message)
                *message = QStringLiteral("cannot remove %1").arg(paths.settingsFile);
            return false;
        }
    } else if (!writeFile(paths.settingsFile, *edited, message)) {
        return false;
    }
    QFile::remove(paths.backupFile);
    if (message) {
        *message = restored ? QStringLiteral("Live updates are off; your previous status line was restored.")
                            : QStringLiteral("Live updates are off; the AQTRunCat status line was removed.");
    }
    return true;
}

std::optional<QString> chainedCommand(const QString &backupFile)
{
    QFile file(backupFile);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject statusLine = QJsonDocument::fromJson(file.read(MaximumSettingsBytes)).object()
                                       .value(QStringLiteral("statusLine")).toObject();
    const QString command = statusLine.value(QStringLiteral("command")).toString();
    if (statusLine.value(QStringLiteral("type")) != QLatin1String("command") || command.trimmed().isEmpty()
        || isBridgeStatusLine(statusLine))
        return std::nullopt;
    return command;
}

} // namespace aqtruncat::claude
