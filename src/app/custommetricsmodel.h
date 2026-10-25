#pragma once

#include "custommetrics/custommetric.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace aqtruncat::custommetrics {

// v2F1.6: one source named in a reload report. id is the source id
// (Source::id), name its configured id or, without one, its file path;
// message is set for the sources left in Error.
struct ReloadEntry {
    QString id;
    QString name;
    QString message;
};

// v2F1.6: the outcome of an explicit reload of the Custom Metrics
// configuration. ok = false: nothing was applied (error says why) and the
// active configuration is unchanged. ok = true: the new configuration is
// active; errors lists the sources it contains in Error (a malformed file,
// an invalid group or a duplicate id fails on its own, as on start-up);
// warnings lists what does not stop a source (Source::warnings), one entry
// per warning.
struct ReloadReport {
    bool ok = false;
    QString error;
    QList<ReloadEntry> added;
    QList<ReloadEntry> changed;
    QList<ReloadEntry> removed;
    QList<ReloadEntry> errors;
    QList<ReloadEntry> warnings;
    int unchanged = 0;
};

// v2F1.7: the answer to a manual refresh (a metric's or a group's icon
// clicked). Accepted: started lists the source ids whose command was
// started for it, coalesced those already running or refreshed less than a
// second ago (no second process: they keep their current run or result).
// A group with no enabled executable descendant is Accepted with both
// empty. Otherwise nothing ran and error says why.
enum class RefreshStatus { Accepted, Invalid, NotFound, Disabled, NotRunnable };

struct RefreshResult {
    RefreshStatus status = RefreshStatus::NotRunnable;
    QString error;
    QStringList started;
    QStringList coalesced;
};

// One line for Settings and aqtruncatctl: "Custom Metrics reloaded: 1
// added, 2 changed, 0 removed, 3 unchanged." (+ the errors and the
// warnings), or the failure.
QString reloadSummary(const ReloadReport &report);

} // namespace aqtruncat::custommetrics

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
    // v2F1.6: rescans the configuration (explicit files, the discovery
    // directory and the groups below it) and applies what changed. The
    // answer arrives with reloadFinished, possibly later (the D-Bus client
    // never blocks the GUI); requests made while one is pending share its
    // answer.
    virtual void reloadConfigurations() = 0;
    // v2F1.7: requests a fresh sample of a command source (Source::id) or
    // of a group's enabled executable descendants, through the engine's own
    // scheduler (never a second process of a running command). The answer
    // arrives with refreshFinished, possibly later.
    virtual void refreshMetric(const QString &sourceId) = 0;

signals:
    void sourcesChanged(const QList<aqtruncat::custommetrics::Source> &sources);
    void reloadFinished(const aqtruncat::custommetrics::ReloadReport &report);
    void refreshFinished(const QString &sourceId, const aqtruncat::custommetrics::RefreshResult &result);
};
