#pragma once

#include <QProcess>

#include <csignal>
#include <unistd.h>

// Subprocesses that AQTRunCat runs (F1 command metrics, F5 update checks)
// lead their own process group, so that a timeout, an output overflow or a
// shutdown also reaches the helpers a script or tool spawned.
namespace aqtruncat::process {

inline void runInOwnGroup(QProcess &process)
{
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
}

inline void signalGroup(const QProcess &process, int signal)
{
    const qint64 pid = process.processId();
    if (pid > 0)
        ::kill(-pid_t(pid), signal);
}

} // namespace aqtruncat::process
