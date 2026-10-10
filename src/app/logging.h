#pragma once

#include <QLoggingCategory>

// Debug output is off by default; enable with --verbose or
// QT_LOGGING_RULES="aqtruncat.*.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcCpu)
Q_DECLARE_LOGGING_CATEGORY(lcSpeed)
Q_DECLARE_LOGGING_CATEGORY(lcRunner)
