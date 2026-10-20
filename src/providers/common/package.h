#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Metric-package helpers shared by the provider adapters' management
// commands (enable/disable/upgrade/status). A provider package is
// $XDG_DATA_HOME/AQTRunCat/metrics/<id>/metric.json, an F1 command source
// with "integration": "<id>" (the provenance AQTRunCat uses to show it under
// Integrations).
namespace aqtruncat::providers {

// $XDG_DATA_HOME/AQTRunCat
QString aqtruncatDataDirectory();
// <data>/metrics/<id>
QString packageDirectory(const QString &id);

// metric.json as an object; empty when missing or not JSON.
QJsonObject readJsonObject(const QString &path, qint64 limit = 1024 * 1024);

// The package belongs to the adapter: its id and the file name of the
// executable it runs match. Only such a package is replaced or removed.
bool isManagedPackage(const QJsonObject &metric, const QString &id, const QString &executableName);

// The value after `option` in a package's argument array, or empty.
QString packageOption(const QJsonArray &arguments, const QString &option);
QJsonArray packageArguments(const QJsonObject &metric);

// QSaveFile write; the error (if any) in *error.
bool writeFileAtomically(const QString &path, const QByteArray &contents, QString *error);

} // namespace aqtruncat::providers
