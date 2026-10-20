// F2 live updates: the opt-in Claude Code status-line bridge. It may only
// change the "statusLine" member of a (synthetic) settings.json, must keep
// everything else byte for byte, wrap and later restore a user's status
// line, and refuse anything it cannot edit safely.

#include "claude/statuslinebridge.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::claude;

namespace {

// A settings.json as Claude Code writes it (2-space indentation, key order
// of insertion), with values that must survive untouched.
const QByteArray UserSettings =
    "{\n"
    "  \"env\": {\n"
    "    \"FOO\": \"bar \\\"quoted\\\" {not an object}\"\n"
    "  },\n"
    "  \"permissions\": {\n"
    "    \"allow\": [\"Bash(git status:*)\", \"Read(//tmp/**)\"],\n"
    "    \"deny\": []\n"
    "  },\n"
    "  \"model\": \"opus[1m]\",\n"
    "  \"theme\": \"dark\"\n"
    "}\n";

const QByteArray WithStatusLine =
    "{\n"
    "  \"model\": \"opus\",\n"
    "  \"statusLine\": {\n"
    "    \"type\": \"command\",\n"
    "    \"command\": \"~/.claude/my-status.sh --fancy\",\n"
    "    \"padding\": 0\n"
    "  },\n"
    "  \"theme\": \"light\"\n"
    "}\n";

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

QJsonObject object(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

struct Fixture {
    QTemporaryDir temporary;
    BridgePaths paths{temporary.filePath(QStringLiteral("claude dir/settings.json")),
                      temporary.filePath(QStringLiteral("data/AQTRunCat/integrations/claude-code/statusline-backup.json")),
                      QStringLiteral("/opt/it's here/bin/aqtruncat-claude-metric")};
};

} // namespace

class TestClaudeStatusLine : public QObject
{
    Q_OBJECT

private slots:
    void textualEditsKeepEverythingElse()
    {
        const QJsonObject value{{QStringLiteral("type"), QStringLiteral("command")}};
        // Insert as the first member, with the file's own indentation.
        const QByteArray inserted = *replaceMember(UserSettings, QStringLiteral("statusLine"), QJsonValue(value));
        QVERIFY(inserted.startsWith("{\n  \"statusLine\": {\"type\":\"command\"},\n  \"env\": {"));
        QVERIFY(inserted.endsWith(UserSettings.mid(1)));
        // And removed again: back to the identical bytes.
        QCOMPARE(*replaceMember(inserted, QStringLiteral("statusLine"), std::nullopt), UserSettings);

        // Replace in the middle, remove in the middle, the last, the only member.
        const QByteArray replaced = *replaceMember(WithStatusLine, QStringLiteral("statusLine"), QJsonValue(value));
        QCOMPARE(replaced, QByteArray("{\n  \"model\": \"opus\",\n  \"statusLine\": {\"type\":\"command\"},\n"
                                      "  \"theme\": \"light\"\n}\n"));
        QCOMPARE(*replaceMember(WithStatusLine, QStringLiteral("statusLine"), std::nullopt),
                 QByteArray("{\n  \"model\": \"opus\",\n  \"theme\": \"light\"\n}\n"));
        QCOMPARE(*replaceMember("{\"a\": 1, \"statusLine\": 2}", QStringLiteral("statusLine"), std::nullopt),
                 QByteArray("{\"a\": 1}"));
        QCOMPARE(object(*replaceMember("{\"statusLine\": 2}", QStringLiteral("statusLine"), std::nullopt)), QJsonObject());
        QCOMPARE(*replaceMember("{}", QStringLiteral("statusLine"), QJsonValue(1)), QByteArray("{\n  \"statusLine\": 1\n}"));
        QCOMPARE(*replaceMember("{\"a\":1}", QStringLiteral("statusLine"), QJsonValue(1)), QByteArray("{\"statusLine\": 1, \"a\":1}"));
        // A nested "statusLine" is not the top-level one.
        QCOMPARE(object(*replaceMember("{\"x\": {\"statusLine\": 5}}", QStringLiteral("statusLine"), QJsonValue(1))),
                 object("{\"statusLine\": 1, \"x\": {\"statusLine\": 5}}"));

        // Refused: not an object, broken, trailing garbage, duplicate keys.
        QVERIFY(!replaceMember("[1]", QStringLiteral("statusLine"), QJsonValue(1)));
        QVERIFY(!replaceMember("{\"a\": ", QStringLiteral("statusLine"), QJsonValue(1)));
        QVERIFY(!replaceMember("{} x", QStringLiteral("statusLine"), QJsonValue(1)));
        QVERIFY(!replaceMember("{\"statusLine\": 1, \"statusLine\": 2}", QStringLiteral("statusLine"), std::nullopt));
    }

    void onWithoutPreviousStatusLineAndOffRestoresBytes()
    {
        Fixture f;
        writeFile(f.paths.settingsFile, UserSettings);
        QCOMPARE(bridgeStatus(f.paths).state, BridgeState::Off);
        QVERIFY(!bridgeStatus(f.paths).otherStatusLine);

        QString message;
        QVERIFY2(installBridge(f.paths, &message), qPrintable(message));
        const QJsonObject on = object(readAll(f.paths.settingsFile));
        const QJsonObject statusLine = on.value(QStringLiteral("statusLine")).toObject();
        QCOMPARE(statusLine.value(QStringLiteral("command")).toString(),
                 QStringLiteral("'/opt/it'\\''s here/bin/aqtruncat-claude-metric' statusline"));
        QJsonObject withoutStatusLine = on;
        withoutStatusLine.remove(QStringLiteral("statusLine"));
        QCOMPARE(withoutStatusLine, object(UserSettings));
        QCOMPARE(bridgeStatus(f.paths).state, BridgeState::On);
        QVERIFY(!bridgeStatus(f.paths).chained);
        QVERIFY(!chainedCommand(f.paths.backupFile));
        QCOMPARE(QFileInfo(f.paths.backupFile).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions());

        // Turning it on twice changes nothing.
        const QByteArray once = readAll(f.paths.settingsFile);
        QVERIFY(installBridge(f.paths, &message));
        QCOMPARE(readAll(f.paths.settingsFile), once);

        QVERIFY2(removeBridge(f.paths, &message), qPrintable(message));
        QCOMPARE(readAll(f.paths.settingsFile), UserSettings);
        QVERIFY(!QFileInfo::exists(f.paths.backupFile));
        QCOMPARE(bridgeStatus(f.paths).state, BridgeState::Off);
    }

    void existingStatusLineIsChainedAndRestoredExactly()
    {
        Fixture f;
        writeFile(f.paths.settingsFile, WithStatusLine);
        QVERIFY(bridgeStatus(f.paths).otherStatusLine);
        QString message;
        QVERIFY2(installBridge(f.paths, &message), qPrintable(message));
        QVERIFY(message.contains(QStringLiteral("previous status line")));
        const QJsonObject statusLine = object(readAll(f.paths.settingsFile)).value(QStringLiteral("statusLine")).toObject();
        QVERIFY(isBridgeStatusLine(statusLine));
        QCOMPARE(statusLine.value(QStringLiteral("padding")).toInt(-1), 0); // presentation kept
        QCOMPARE(chainedCommand(f.paths.backupFile), std::optional<QString>(QStringLiteral("~/.claude/my-status.sh --fancy")));
        QVERIFY(bridgeStatus(f.paths).chained);

        QVERIFY(removeBridge(f.paths, &message));
        QCOMPARE(readAll(f.paths.settingsFile), WithStatusLine); // byte for byte
        QVERIFY(message.contains(QStringLiteral("restored")));
    }

    void otherSettingsChangedMeanwhileAreKept()
    {
        Fixture f;
        writeFile(f.paths.settingsFile, WithStatusLine);
        QVERIFY(installBridge(f.paths, nullptr));
        // The user (or Claude Code) changes another setting while it is on.
        QByteArray edited = readAll(f.paths.settingsFile);
        edited.replace("\"theme\": \"light\"", "\"theme\": \"dark\"");
        writeFile(f.paths.settingsFile, edited);
        QVERIFY(removeBridge(f.paths, nullptr));
        QJsonObject expected = object(WithStatusLine);
        expected.insert(QStringLiteral("theme"), QStringLiteral("dark"));
        QCOMPARE(object(readAll(f.paths.settingsFile)), expected);
    }

    void statusLineReplacedByTheUserIsLeftAlone()
    {
        Fixture f;
        writeFile(f.paths.settingsFile, UserSettings);
        QVERIFY(installBridge(f.paths, nullptr));
        QByteArray mine = *replaceMember(readAll(f.paths.settingsFile), QStringLiteral("statusLine"),
                                         QJsonValue(QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                {QStringLiteral("command"), QStringLiteral("my-new-line")}}));
        writeFile(f.paths.settingsFile, mine);
        QCOMPARE(bridgeStatus(f.paths).state, BridgeState::Replaced);
        QString message;
        QVERIFY(removeBridge(f.paths, &message));
        QCOMPARE(readAll(f.paths.settingsFile), mine);
        QVERIFY(message.contains(QStringLiteral("left unchanged")));
        QVERIFY(!QFileInfo::exists(f.paths.backupFile));
    }

    void missingSettingsFileIsCreatedAndRemovedAgain()
    {
        Fixture f;
        QVERIFY(installBridge(f.paths, nullptr));
        QVERIFY(isBridgeStatusLine(object(readAll(f.paths.settingsFile)).value(QStringLiteral("statusLine"))));
        QVERIFY(removeBridge(f.paths, nullptr));
        QVERIFY(!QFileInfo::exists(f.paths.settingsFile));
    }

    void invalidSettingsAreNeverTouched()
    {
        Fixture f;
        const QByteArray broken = "{\n  \"model\": \"opus\",\n  // a comment\n}\n";
        writeFile(f.paths.settingsFile, broken);
        QString message;
        QVERIFY(!installBridge(f.paths, &message));
        QVERIFY(message.contains(QStringLiteral("nothing changed")));
        QCOMPARE(readAll(f.paths.settingsFile), broken);
        QVERIFY(!QFileInfo::exists(f.paths.backupFile));
        QVERIFY(!bridgeStatus(f.paths).error.isEmpty());
        QVERIFY(!removeBridge(f.paths, &message));
        QCOMPARE(readAll(f.paths.settingsFile), broken);

        writeFile(f.paths.settingsFile, "{\"statusLine\": 1, \"statusLine\": 2}");
        QVERIFY(!installBridge(f.paths, &message));
    }

    void symlinkedSettingsAreWrittenThrough()
    {
        Fixture f;
        const QString real = f.temporary.filePath(QStringLiteral("dotfiles/claude-settings.json"));
        writeFile(real, UserSettings);
        QDir().mkpath(QFileInfo(f.paths.settingsFile).absolutePath());
        QVERIFY(QFile::link(real, f.paths.settingsFile));
        QVERIFY(installBridge(f.paths, nullptr));
        QVERIFY(QFileInfo(f.paths.settingsFile).isSymLink());
        QVERIFY(isBridgeStatusLine(object(readAll(real)).value(QStringLiteral("statusLine"))));
        QVERIFY(removeBridge(f.paths, nullptr));
        QVERIFY(QFileInfo(f.paths.settingsFile).isSymLink());
        QCOMPARE(readAll(real), UserSettings);
    }

    void movedAdapterUpdatesOnlyTheCommand()
    {
        Fixture f;
        writeFile(f.paths.settingsFile, WithStatusLine);
        QVERIFY(installBridge(f.paths, nullptr));
        const QByteArray backup = readAll(f.paths.backupFile);
        f.paths.executable = QStringLiteral("/usr/bin/aqtruncat-claude-metric");
        QVERIFY(installBridge(f.paths, nullptr));
        QCOMPARE(object(readAll(f.paths.settingsFile)).value(QStringLiteral("statusLine")).toObject()
                     .value(QStringLiteral("command")).toString(),
                 QStringLiteral("'/usr/bin/aqtruncat-claude-metric' statusline"));
        QCOMPARE(readAll(f.paths.backupFile), backup);
        QVERIFY(removeBridge(f.paths, nullptr));
        QCOMPARE(readAll(f.paths.settingsFile), WithStatusLine);
    }

    void recognisesOnlyItsOwnStatusLine()
    {
        QVERIFY(isBridgeStatusLine(object("{\"type\":\"command\",\"command\":\"'/x/aqtruncat-claude-metric' statusline\"}")));
        QVERIFY(!isBridgeStatusLine(object("{\"type\":\"command\",\"command\":\"my-status.sh\"}")));
        QVERIFY(!isBridgeStatusLine(object("{\"type\":\"command\",\"command\":\"aqtruncat-claude-metric snapshot\"}")));
        QVERIFY(!isBridgeStatusLine(QJsonValue()));
        QCOMPARE(shellQuote(QStringLiteral("a b")), QStringLiteral("'a b'"));
        QCOMPARE(shellQuote(QStringLiteral("it's")), QStringLiteral("'it'\\''s'"));
    }
};

QTEST_GUILESS_MAIN(TestClaudeStatusLine)
#include "tst_claudestatusline.moc"
