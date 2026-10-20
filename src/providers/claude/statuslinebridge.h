#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>

#include <optional>

// Opt-in "live updates" for the Claude Code usage quotas. Claude Code passes
// rate_limits only to a configured status-line command, so turning live
// updates on sets "statusLine" in the user's Claude Code settings.json to
// "aqtruncat-claude-metric statusline", which records the two quota windows
// and then runs the user's previous status-line command, if there was one,
// with the same input, so that status line keeps working unchanged.
//
// Only the "statusLine" member is changed: the file is edited textually and
// the result is verified to differ from the original in that member only, so
// formatting, key order and every other setting stay as they were. The
// previous value is kept in a backup file and put back when live updates are
// turned off. Nothing is ever done without an explicit request.
namespace aqtruncat::claude {

inline constexpr qint64 MaximumSettingsBytes = 4 * 1024 * 1024;

struct BridgePaths {
    QString settingsFile; // <claude config>/settings.json
    QString backupFile;   // $XDG_DATA_HOME/AQTRunCat/integrations/claude-code/statusline-backup.json
    QString executable;   // absolute path of aqtruncat-claude-metric
};

enum class BridgeState {
    Off,      // settings.json has no AQTRunCat status line
    On,       // settings.json runs the bridge
    Replaced, // a backup exists but settings.json was changed to something else since
};

struct BridgeStatus {
    BridgeState state = BridgeState::Off;
    bool otherStatusLine = false;  // a non-AQTRunCat status line is configured
    bool chained = false;          // On, and a previous command is run after the capture
    QString error;                 // settings.json unreadable or invalid
};

// True for the statusLine value written by installBridge (any install path).
bool isBridgeStatusLine(const QJsonValue &statusLine);

BridgeStatus bridgeStatus(const BridgePaths &paths);

// Both return false with a message and leave every file unchanged when the
// operation cannot be done safely (invalid JSON, unexpected layout).
bool installBridge(const BridgePaths &paths, QString *message);
bool removeBridge(const BridgePaths &paths, QString *message);

// The previous status-line command to run after a capture, if any.
std::optional<QString> chainedCommand(const QString &backupFile);

// For a POSIX shell command line: 'it'\''s'.
QString shellQuote(const QString &text);

// Textual JSON object surgery used by install/remove (exposed for tests).
// replaceMember sets (or inserts, or with nullopt removes) one top-level
// member and returns the new text, or nullopt if the text is not a JSON
// object it can edit safely.
std::optional<QByteArray> replaceMember(const QByteArray &json, const QString &key,
                                        const std::optional<QJsonValue> &value);
// The same with the new value given as JSON text, inserted verbatim.
std::optional<QByteArray> replaceMemberText(const QByteArray &json, const QString &key,
                                            const std::optional<QByteArray> &valueText);
// The exact text of a top-level member's value, if the member exists.
std::optional<QByteArray> memberText(const QByteArray &json, const QString &key);

} // namespace aqtruncat::claude
