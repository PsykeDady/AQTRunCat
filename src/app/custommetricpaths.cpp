#include "custommetricpaths.h"

#include <QDir>
#include <QStandardPaths>

QString customMetricsDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("AQTRunCat/metrics"));
}
