#include "package.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace aqtruncat::providers {

QString aqtruncatDataDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("AQTRunCat"));
}

QString packageDirectory(const QString &id)
{
    // Same location AQTRunCat discovers packages in (custommetricpaths.cpp).
    return aqtruncatDataDirectory() + QStringLiteral("/metrics/") + id;
}

QJsonObject readJsonObject(const QString &path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.read(limit)).object();
}

bool isManagedPackage(const QJsonObject &metric, const QString &id, const QString &executableName)
{
    const QString executable = metric.value(QStringLiteral("source")).toObject()
                                   .value(QStringLiteral("executable")).toString();
    return metric.value(QStringLiteral("id")) == id && QFileInfo(executable).fileName() == executableName;
}

QString packageOption(const QJsonArray &arguments, const QString &option)
{
    for (int i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments.at(i) == option)
            return arguments.at(i + 1).toString();
    }
    return {};
}

QJsonArray packageArguments(const QJsonObject &metric)
{
    return metric.value(QStringLiteral("source")).toObject().value(QStringLiteral("arguments")).toArray();
}

bool writeFileAtomically(const QString &path, const QByteArray &contents, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace aqtruncat::providers
