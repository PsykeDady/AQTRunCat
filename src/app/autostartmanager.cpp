#include "autostartmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

AutostartManager::AutostartManager(QString configRoot, QString executable)
    : m_configRoot(configRoot.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                       : std::move(configRoot))
    , m_executable(executable.isEmpty() ? currentExecutable() : std::move(executable))
{
}

QString AutostartManager::currentExecutable()
{
    // The real file, not the PATH spelling it was started through (on merged
    // /usr systems /usr/sbin/aqtruncat may be a symlink to /usr/bin/aqtruncat).
    const QString path = QCoreApplication::applicationFilePath();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical;
}

QString AutostartManager::filePath() const
{
    return QDir(m_configRoot).filePath(QStringLiteral("autostart/aqtruncat.desktop"));
}

bool AutostartManager::isEnabled() const
{
    return QFile::exists(filePath());
}

QString AutostartManager::quotedExec(const QString &path)
{
    QString escaped = path;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'\"', QStringLiteral("\\\""));
    escaped.replace(u'`', QStringLiteral("\\`"));
    escaped.replace(u'$', QStringLiteral("\\$"));
    return u'\"' + escaped + u'\"';
}

bool AutostartManager::setEnabled(bool enabled, QString *error) const
{
    const QString path = filePath();
    if (!enabled) {
        if (!QFile::exists(path) || QFile::remove(path))
            return true;
        if (error)
            *error = QStringLiteral("cannot remove %1").arg(path);
        return false;
    }

    if (!QDir().mkpath(QFileInfo(path).path())) {
        if (error)
            *error = QStringLiteral("cannot create autostart directory for %1").arg(path);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray contents = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Version=1.0\n"
        "Name=AQTRunCat\n"
        "Comment=A Cute Run Cat for Linux\n"
        "Exec=%1\n"
        "Icon=aqtruncat\n"
        "Terminal=false\n"
        "Categories=System;Monitor;\n"
        "X-GNOME-Autostart-enabled=true\n")
                                    .arg(quotedExec(m_executable))
                                    .toUtf8();
    if (file.write(contents) != contents.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
