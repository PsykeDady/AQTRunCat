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

signals:
    void sourcesChanged(const QList<aqtruncat::custommetrics::Source> &sources);
};
