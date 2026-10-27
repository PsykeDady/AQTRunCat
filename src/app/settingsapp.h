#pragma once

#include <QString>

class QApplication;

namespace aqtruncat {
class RunnerService;
}

int runStandaloneSettings(QApplication &app, const aqtruncat::RunnerService &runners,
                          const QString &defaultRunnerId);
