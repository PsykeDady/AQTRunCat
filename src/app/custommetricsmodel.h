#pragma once

#include "custommetrics/custommetric.h"

#include <QList>
#include <QObject>
#include <QString>

// What the Settings window and the frontend need from Custom Metrics: the
// current sources and explicit (un)registration. Implemented by the engine's
// CustomMetricsService (backend, tests) and by the frontend's D-Bus client
// (src/app/backendclient.*), so the UI does not care where sources live.
class CustomMetricsModel : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual const QList<aqtruncat::custommetrics::Source> &sources() const = 0;
    virtual QString discoveryDirectory() const = 0;
    virtual bool registerFile(const QString &path, QString *error = nullptr) = 0;
    virtual void unregisterFile(const QString &path) = 0;
    // v2F1.2: writes "enabled" into the source's configuration file; the
    // engine applies it at once (a disabled source stops running and leaves
    // the presentation surfaces). False with *error when it cannot.
    virtual bool setEnabled(const QString &path, bool enabled, QString *error = nullptr) = 0;

signals:
    void sourcesChanged(const QList<aqtruncat::custommetrics::Source> &sources);
};
