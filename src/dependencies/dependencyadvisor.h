#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aqtruncat::dependencies {

struct DependencyFinding {
    QString id;
    QString feature;
    QString executable;
    QString package;
    QString installCommand;
};

struct AuditOptions {
    QString path;
    QString dataDirectory;
    QString sysRoot = QStringLiteral("/sys");
    QString root = QStringLiteral("/");
    QStringList customMetricFiles;
    QString distributionId;
    QStringList distributionLike;
    QString fpsSource;
    int updateIntervalMinutes = 60;
    bool showFps = true;
    bool codexLive = false;
    bool waylandSession = false;
    bool plasmaSession = false;
    bool plasmoidInstalled = false;
    bool waylandPluginAvailable = true;
    QStringList missingPlasmaModules;
    bool nvidiaGpuPresent = false;
    bool nvmlAvailable = true;
    bool gpuModelDatabaseAvailable = true;
};

// Read-only audit of software dependencies for currently enabled features.
// It does not start the backend, invoke tools, read credentials, or install.
QVector<DependencyFinding> missing(const AuditOptions &options);
AuditOptions currentUserOptions();
QString formatReport(const AuditOptions &options);

} // namespace aqtruncat::dependencies
