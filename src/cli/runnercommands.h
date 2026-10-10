#pragma once

// "aqtruncat runner …" commands: presentation of RunnerService results.
// The work itself happens in RunnerService, shared with the future GUI.

#include "commandline.h"
#include "runner/runnerservice.h"

#include <QTextStream>

namespace aqtruncat::cli {

int exitCodeFor(RunnerError error);

// Human readable table (one line per runner, warnings indented below).
QString formatRunnerList(const QList<ListedRunner> &runners, const QString &defaultId);

int runnerList(const RunnerService &service, const QString &defaultId, QTextStream &out);
int runnerImport(const RunnerService &service, const Invocation &invocation, QTextStream &out, QTextStream &err);
int runnerRemove(const RunnerService &service, const Invocation &invocation, QTextStream &out, QTextStream &err);

} // namespace aqtruncat::cli
