#pragma once

#include <QString>

class AutostartManager
{
public:
    explicit AutostartManager(QString configRoot = {}, QString executable = {});

    QString filePath() const;
    bool isEnabled() const;
    bool setEnabled(bool enabled, QString *error = nullptr) const;

private:
    static QString quotedExec(const QString &path);
    static QString currentExecutable();

    QString m_configRoot;
    QString m_executable;
};

