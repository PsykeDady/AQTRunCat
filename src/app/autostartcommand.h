#pragma once

#include <QTextStream>

#include <optional>

namespace aqtruncat { class AppSettings; }
class AutostartManager;

// "aqtruncat autostart enable|disable|status": the same per-user preference
// and XDG entry as the Settings window, so installers never write the entry
// themselves. `enable` empty means status. Returns a process exit code.
int runAutostartCommand(std::optional<bool> enable, aqtruncat::AppSettings &settings,
                        const AutostartManager &autostart, QTextStream &out, QTextStream &err);
