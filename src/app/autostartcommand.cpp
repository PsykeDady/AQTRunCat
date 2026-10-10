#include "autostartcommand.h"

#include "appsettings.h"
#include "autostartmanager.h"

int runAutostartCommand(std::optional<bool> enable, aqtruncat::AppSettings &settings,
                        const AutostartManager &autostart, QTextStream &out, QTextStream &err)
{
    if (!enable) {
        out << (autostart.isEnabled() ? "enabled" : "disabled") << ": " << autostart.filePath() << '\n';
        return 0;
    }
    QString error;
    if (!autostart.setEnabled(*enable, &error)) {
        err << "aqtruncat: cannot " << (*enable ? "enable" : "disable") << " autostart: " << error << '\n';
        return 1;
    }
    // The running application applies this preference at start-up, so the
    // entry and the preference must always agree.
    settings.setAutostartEnabled(*enable);
    out << "Autostart " << (*enable ? "enabled" : "disabled") << ": " << autostart.filePath() << '\n';
    return 0;
}
