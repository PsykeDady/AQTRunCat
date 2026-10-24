// v2F1.6: explicit reload of the Custom Metrics configuration
// (CustomMetricsService::reloadNow): added, changed and removed standalone
// metrics, groups and children; inherited changes; unchanged sources keep
// their job, their values and their process; display-only and schedule-only
// changes never restart a command; execution changes do, and the replaced
// run's result never comes back; a reload that cannot read a directory is
// not applied at all; per-file errors fail on their own and are reported.
#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "app/dashboardwindow.h"
#include "app/metricsbarids.h"
#include "app/metricsbarmodel.h"
#include "custommetrics/metricgroup.h"

#include <QApplication>
#include <QLabel>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cerrno>
#include <csignal>
#include <sys/types.h>

using namespace aqtruncat::custommetrics;

namespace {

QString helperPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/custommetric_test_helper");
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

void writeJson(const QString &path, const QJsonObject &object)
{
    writeFile(path, QJsonDocument(object).toJson());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

QJsonObject command(const QString &id, const QString &title, const QJsonArray &arguments, int interval = 3600)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                              {QStringLiteral("executable"), helperPath()},
                                                              {QStringLiteral("arguments"), arguments},
                                                              {QStringLiteral("intervalSeconds"), interval}}}};
}

// A group whose children count their runs in <name>.count next to them; the
// parent owns the program, the "counter" mode and the interval.
QString writeCounterGroup(const QString &discovery, const QStringList &children, int interval = 3600)
{
    const QString group = discovery + QStringLiteral("/counters");
    writeJson(group + QStringLiteral("/metric.json"),
              QJsonObject{{QStringLiteral("id"), QStringLiteral("counters")},
                          {QStringLiteral("title"), QStringLiteral("Counters")},
                          {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                                 {QStringLiteral("executable"), helperPath()},
                                                                 {QStringLiteral("intervalSeconds"), interval}}}});
    for (const QString &child : children) {
        writeJson(group + u'/' + child + QStringLiteral("/metric.json"),
                  QJsonObject{{QStringLiteral("title"), child.toUpper()},
                              {QStringLiteral("source"), QJsonObject{
                                   {QStringLiteral("type"), QStringLiteral("command")},
                                   {QStringLiteral("arguments"), QJsonArray{QStringLiteral("counter"),
                                                                            group + u'/' + child + QStringLiteral(".count")}}}}});
    }
    return group;
}

QJsonObject readObject(const QString &path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

const Source *byConfiguredId(const CustomMetricsService &service, const QString &id)
{
    for (const Source &source : service.sources()) {
        if (source.configuredId == id)
            return &source;
    }
    return nullptr;
}

QString valueOf(const CustomMetricsService &service, const QString &id)
{
    const Source *source = byConfiguredId(service, id);
    return source && source->snapshot && !source->snapshot->metrics.isEmpty()
        ? source->snapshot->metrics.first().formattedValue : QString();
}

bool settled(const CustomMetricsService &service, const QString &id)
{
    const Source *source = byConfiguredId(service, id);
    return source && source->state == State::Available;
}

QStringList names(const QList<ReloadEntry> &entries)
{
    QStringList list;
    for (const ReloadEntry &entry : entries)
        list.append(entry.name);
    list.sort();
    return list;
}

QStringList sourceIds(const CustomMetricsService &service)
{
    QStringList ids;
    for (const Source &source : service.sources())
        ids.append(source.id);
    return ids;
}

bool processGone(pid_t pid)
{
    errno = 0;
    return ::kill(pid, 0) == -1 && errno == ESRCH;
}

struct Fixture {
    QTemporaryDir temporary;
    QString discovery = temporary.filePath(QStringLiteral("metrics"));
    aqtruncat::AppSettings settings{QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini"))};
    QString path(const QString &name) const { return discovery + u'/' + name; }
    QString count(const QString &name) const { return temporary.filePath(name + QStringLiteral(".count")); }
};

} // namespace

class TestCustomMetricsReload : public QObject
{
    Q_OBJECT

private slots:
    // Nothing changed: everything unchanged, no command runs again, the
    // values and ids stay.
    void noChanges()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")),
                  command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("counter"), f.count(QStringLiteral("a"))}));
        writeCounterGroup(f.discovery, {QStringLiteral("x"), QStringLiteral("y")});
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("counters/x"))
                                     && settled(service, QStringLiteral("counters/y")), 5000);
        const QStringList ids = sourceIds(service);
        QSignalSpy finished(&service, &CustomMetricsModel::reloadFinished);

        service.reloadConfigurations();
        QCOMPARE(finished.size(), 1);
        const ReloadReport report = finished.first().first().value<ReloadReport>();
        QVERIFY(report.ok);
        QVERIFY(report.added.isEmpty() && report.changed.isEmpty() && report.removed.isEmpty() && report.errors.isEmpty());
        QCOMPARE(report.unchanged, 4); // a, the group, x, y
        QCOMPARE(reloadSummary(report), QStringLiteral("Custom Metrics reloaded: 0 added, 0 changed, 0 removed, 4 unchanged."));
        QCOMPARE(sourceIds(service), ids);
        QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("1"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->state, State::Available); // no "Waiting…"
        QTest::qWait(300);
        QCOMPARE(readFile(f.count(QStringLiteral("a"))), QByteArray("1"));
        QCOMPARE(readFile(f.discovery + QStringLiteral("/counters/x.count")), QByteArray("1"));
        service.stop();
    }

    // A standalone metric added, changed (what runs: restarted, its new
    // value) and removed (its job stops).
    void standaloneLifecycle()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("old")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")), 5000);

        writeJson(f.path(QStringLiteral("b.json")),
                  command(QStringLiteral("b"), QStringLiteral("B"), {QStringLiteral("counter"), f.count(QStringLiteral("b"))}, 1));
        ReloadReport report = service.reloadNow();
        QVERIFY(report.ok);
        QCOMPARE(names(report.added), QStringList{QStringLiteral("b")});
        QCOMPARE(report.unchanged, 1);
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("b")), 5000);

        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("new")}));
        report = service.reloadNow();
        QCOMPARE(names(report.changed), QStringList{QStringLiteral("a")});
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("a")), QStringLiteral("new"), 5000);

        QVERIFY(QFile::remove(f.path(QStringLiteral("b.json"))));
        report = service.reloadNow();
        QCOMPARE(names(report.removed), QStringList{QStringLiteral("b")});
        QVERIFY(!byConfiguredId(service, QStringLiteral("b")));
        const QByteArray runs = readFile(f.count(QStringLiteral("b")));
        QTest::qWait(1500); // its 1 s interval: it would have run again
        QCOMPARE(readFile(f.count(QStringLiteral("b"))), runs);
        service.stop();
    }

    // RunCat Neo snapshot files are reloaded too.
    void snapshotFiles()
    {
        Fixture f;
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        writeJson(f.path(QStringLiteral("neo.json")),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("Neo")},
                              {QStringLiteral("lastUpdatedDate"), QStringLiteral("2026-10-24T10:00:00Z")},
                              {QStringLiteral("metrics"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("T")},
                                                                                 {QStringLiteral("formattedValue"), QStringLiteral("1")}}}}});
        ReloadReport report = service.reloadNow();
        QCOMPARE(report.added.size(), 1);
        QVERIFY2(service.sources().first().state == State::Available, qPrintable(service.sources().first().message));
        QVERIFY(QFile::remove(f.path(QStringLiteral("neo.json"))));
        report = service.reloadNow();
        QCOMPARE(report.removed.size(), 1);
        QVERIFY(service.sources().isEmpty());
        service.stop();
    }

    // Groups and children appear and disappear with their directories.
    void groupLifecycle()
    {
        Fixture f;
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        const QString group = writeCounterGroup(f.discovery, {QStringLiteral("x"), QStringLiteral("y")});
        ReloadReport report = service.reloadNow();
        QCOMPARE(names(report.added), (QStringList{QStringLiteral("counters"), QStringLiteral("counters/x"), QStringLiteral("counters/y")}));
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("counters/x")) && settled(service, QStringLiteral("counters/y")), 5000);
        QCOMPARE(byConfiguredId(service, QStringLiteral("counters/x"))->parentId, byConfiguredId(service, QStringLiteral("counters"))->id);

        writeJson(group + QStringLiteral("/z/metric.json"),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("Z")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                     {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), QStringLiteral("z")}}}}});
        report = service.reloadNow();
        QCOMPARE(names(report.added), QStringList{QStringLiteral("counters/z")});
        QCOMPARE(report.unchanged, 3);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("counters/z")), QStringLiteral("z"), 5000);

        QVERIFY(QDir(group + QStringLiteral("/y")).removeRecursively());
        report = service.reloadNow();
        QCOMPARE(names(report.removed), QStringList{QStringLiteral("counters/y")});
        QVERIFY(!byConfiguredId(service, QStringLiteral("counters/y")));

        QVERIFY(QDir(group).removeRecursively());
        report = service.reloadNow();
        QCOMPARE(names(report.removed), (QStringList{QStringLiteral("counters"), QStringLiteral("counters/x"), QStringLiteral("counters/z")}));
        QVERIFY(service.sources().isEmpty());
        service.stop();
    }

    // The parent's interval changes: the inheriting child gets it (no
    // restart: a schedule change), the child with its own interval does not
    // change at all; both keep their value.
    void inheritedChanges()
    {
        Fixture f;
        const QString group = writeCounterGroup(f.discovery, {QStringLiteral("x"), QStringLiteral("y")});
        QJsonObject y = readObject(group + QStringLiteral("/y/metric.json"));
        QJsonObject ySource = y.value(QStringLiteral("source")).toObject();
        ySource.insert(QStringLiteral("intervalSeconds"), 1800); // its own override
        y.insert(QStringLiteral("source"), ySource);
        writeJson(group + QStringLiteral("/y/metric.json"), y);
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("counters/x")) && settled(service, QStringLiteral("counters/y")), 5000);
        const QByteArray yBefore = byConfiguredId(service, QStringLiteral("counters/y"))->configuration;

        QJsonObject parent = readObject(group + QStringLiteral("/metric.json"));
        QJsonObject source = parent.value(QStringLiteral("source")).toObject();
        source.insert(QStringLiteral("intervalSeconds"), 1);
        parent.insert(QStringLiteral("source"), source);
        writeJson(group + QStringLiteral("/metric.json"), parent);
        const ReloadReport report = service.reloadNow();
        QCOMPARE(names(report.changed), (QStringList{QStringLiteral("counters"), QStringLiteral("counters/x")}));
        QCOMPARE(report.unchanged, 1);
        QVERIFY(byConfiguredId(service, QStringLiteral("counters/x"))->configuration.contains("\"intervalSeconds\":1"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("counters/y"))->configuration, yBefore);
        QVERIFY(yBefore.contains("\"intervalSeconds\":1800"));
        // Not restarted: the value stays, then the new 1 s interval applies.
        QCOMPARE(valueOf(service, QStringLiteral("counters/x")), QStringLiteral("1"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("counters/x"))->state, State::Available);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("counters/x")), QStringLiteral("2"), 5000);
        QCOMPARE(readFile(group + QStringLiteral("/y.count")), QByteArray("1"));
        // The files are never rewritten by a reload.
        QCOMPARE(readObject(group + QStringLiteral("/y/metric.json")), y);
        service.stop();
    }

    // enabled: false stops a child (no job), true starts it again; a
    // disabled group disables every child.
    void enabledTransitions()
    {
        Fixture f;
        const QString group = writeCounterGroup(f.discovery, {QStringLiteral("x"), QStringLiteral("y")}, 1);
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("counters/x")), 5000);

        QJsonObject x = readObject(group + QStringLiteral("/x/metric.json"));
        x.insert(QStringLiteral("enabled"), false);
        writeJson(group + QStringLiteral("/x/metric.json"), x);
        ReloadReport report = service.reloadNow();
        QCOMPARE(names(report.changed), QStringList{QStringLiteral("counters/x")});
        QVERIFY(!byConfiguredId(service, QStringLiteral("counters/x"))->enabled);
        QTest::qWait(100);
        const QByteArray stopped = readFile(group + QStringLiteral("/x.count"));
        QTest::qWait(1500);
        QCOMPARE(readFile(group + QStringLiteral("/x.count")), stopped);

        x.insert(QStringLiteral("enabled"), true);
        writeJson(group + QStringLiteral("/x/metric.json"), x);
        service.reloadNow();
        QVERIFY(byConfiguredId(service, QStringLiteral("counters/x"))->enabled);
        QTRY_VERIFY_WITH_TIMEOUT(readFile(group + QStringLiteral("/x.count")) != stopped, 5000);

        QJsonObject parent = readObject(group + QStringLiteral("/metric.json"));
        parent.insert(QStringLiteral("enabled"), false);
        writeJson(group + QStringLiteral("/metric.json"), parent);
        report = service.reloadNow();
        QCOMPARE(names(report.changed), (QStringList{QStringLiteral("counters"), QStringLiteral("counters/x"), QStringLiteral("counters/y")}));
        for (const Source &source : service.sources())
            QVERIFY(!source.enabled);
        service.stop();
    }

    // Title, icon and display.showBar only: the command keeps running on its
    // schedule (not restarted) and its last value takes the new title.
    void displayOnlyChange()
    {
        Fixture f;
        QJsonObject a = command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("counter"), f.count(QStringLiteral("a"))});
        a.insert(QStringLiteral("display"), QJsonObject{{QStringLiteral("normalizedRange"),
                                                         QJsonObject{{QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10}}}});
        writeJson(f.path(QStringLiteral("a.json")), a);
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")), 5000);

        a.insert(QStringLiteral("title"), QStringLiteral("Renamed"));
        a.insert(QStringLiteral("symbol"), QStringLiteral("cpu"));
        QJsonObject display = a.value(QStringLiteral("display")).toObject();
        display.insert(QStringLiteral("showBar"), false);
        a.insert(QStringLiteral("display"), display);
        writeJson(f.path(QStringLiteral("a.json")), a);
        const ReloadReport report = service.reloadNow();
        QCOMPARE(names(report.changed), QStringList{QStringLiteral("a")});
        const Source *source = byConfiguredId(service, QStringLiteral("a"));
        QCOMPARE(source->state, State::Available);
        QCOMPARE(source->snapshot->title, QStringLiteral("Renamed"));
        QCOMPARE(source->snapshot->metrics.first().title, QStringLiteral("Renamed"));
        QCOMPARE(source->snapshot->symbol, std::optional<QString>(QStringLiteral("cpu")));
        QVERIFY(!source->snapshot->showBar);
        QCOMPARE(source->snapshot->metrics.first().formattedValue, QStringLiteral("1"));
        QCOMPARE(*source->snapshot->metrics.first().normalizedValue, 0.1);
        QTest::qWait(300);
        QCOMPARE(readFile(f.count(QStringLiteral("a"))), QByteArray("1"));
        service.stop();
    }

    // A changed program while the old run is still going: the old process
    // group is killed and its result never shows; the new one runs at once.
    void executionChangeDuringRun()
    {
        Fixture f;
        const QString pidFile = f.temporary.filePath(QStringLiteral("old.pid"));
        writeJson(f.path(QStringLiteral("a.json")),
                  command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("pid-sleep"), pidFile, QStringLiteral("1500")}, 1));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(!readFile(pidFile).isEmpty(), 5000);
        const pid_t old = pid_t(readFile(pidFile).toInt());
        QVERIFY(old > 0);

        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("new")}, 1));
        const ReloadReport report = service.reloadNow();
        QCOMPARE(names(report.changed), QStringList{QStringLiteral("a")});
        QVERIFY(processGone(old));
        QStringList seen;
        connect(&service, &CustomMetricsModel::sourcesChanged, this, [&seen](const QList<Source> &sources) {
            for (const Source &source : sources) {
                if (source.snapshot && !source.snapshot->metrics.isEmpty())
                    seen.append(source.snapshot->metrics.first().formattedValue);
            }
        });
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("a")), QStringLiteral("new"), 5000);
        QTest::qWait(2000); // past the old run's end
        QVERIFY2(!seen.contains(QStringLiteral("old")) && !seen.contains(QStringLiteral("done")), qPrintable(seen.join(u',')));
        QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("new"));
        service.stop();
    }

    // A display-only change while the command runs: the same run goes on
    // and its result arrives under the new title.
    void displayChangeDuringRun()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")),
                  command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("sleep"), QStringLiteral("800"), QStringLiteral("slow")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("a"))->message, QStringLiteral("Command is running."), 3000);
        QJsonObject a = command(QStringLiteral("a"), QStringLiteral("Renamed"), {QStringLiteral("sleep"), QStringLiteral("800"), QStringLiteral("slow")});
        writeJson(f.path(QStringLiteral("a.json")), a);
        service.reloadNow();
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->message, QStringLiteral("Command is running."));
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")), 5000);
        QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("slow"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->snapshot->title, QStringLiteral("Renamed"));
        service.stop();
    }

    // Per-file errors (established policy): the broken file is an Error
    // listed in the report, never silently dropped; the others go on.
    void malformedAndDuplicates()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")),
                  command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("counter"), f.count(QStringLiteral("a"))}));
        writeJson(f.path(QStringLiteral("b.json")), command(QStringLiteral("b"), QStringLiteral("B"), {QStringLiteral("value"), QStringLiteral("b")}));
        const QString group = writeCounterGroup(f.discovery, {QStringLiteral("x"), QStringLiteral("y")});
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("b")), 5000);

        writeFile(f.path(QStringLiteral("b.json")), "{ not json");
        QJsonObject y = readObject(group + QStringLiteral("/y/metric.json"));
        y.insert(QStringLiteral("id"), QStringLiteral("x")); // the id of its sibling
        writeJson(group + QStringLiteral("/y/metric.json"), y);
        ReloadReport report = service.reloadNow();
        QVERIFY(report.ok);
        QCOMPARE(report.errors.size(), 2);
        QVERIFY(reloadSummary(report).contains(QStringLiteral("2 with errors")));
        QVERIFY(reloadSummary(report).contains(QStringLiteral("Duplicate id")));
        // The broken file stays listed, in Error, under its path.
        const auto broken = std::find_if(service.sources().cbegin(), service.sources().cend(), [&](const Source &s) {
            return s.path == f.path(QStringLiteral("b.json"));
        });
        QVERIFY(broken != service.sources().cend());
        QCOMPARE(broken->state, State::Error);
        QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("1"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->state, State::Available);

        writeJson(f.path(QStringLiteral("b.json")), command(QStringLiteral("b"), QStringLiteral("B"), {QStringLiteral("value"), QStringLiteral("b")}));
        report = service.reloadNow();
        QCOMPARE(report.errors.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("b")), 5000);
        service.stop();
    }

    // A directory that cannot be read: nothing is applied, the active
    // sources keep running with their values, and the next readable reload
    // applies everything.
    void unreadableDirectoryKeepsState()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("a")}));
        const QString group = writeCounterGroup(f.discovery, {QStringLiteral("x")});
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("counters/x")), 5000);
        const QStringList ids = sourceIds(service);
        QSignalSpy published(&service, &CustomMetricsModel::sourcesChanged);

        for (const QString &directory : {f.discovery, group, group + QStringLiteral("/x")}) {
            const QFile::Permissions permissions = QFile::permissions(directory);
            QVERIFY(QFile::setPermissions(directory, QFile::WriteOwner));
            const ReloadReport report = service.reloadNow();
            QVERIFY(QFile::setPermissions(directory, permissions));
            QVERIFY2(!report.ok, qPrintable(directory));
            QCOMPARE(report.error, QStringLiteral("Cannot read the directory %1.").arg(directory));
            QVERIFY(reloadSummary(report).endsWith(QStringLiteral("The active configuration is unchanged.")));
            QCOMPARE(sourceIds(service), ids);
            QCOMPARE(published.size(), 0);
            QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("a"));
            QCOMPARE(byConfiguredId(service, QStringLiteral("counters/x"))->state, State::Available);
        }
        QVERIFY(QFile::remove(f.path(QStringLiteral("a.json"))));
        const ReloadReport report = service.reloadNow();
        QVERIFY(report.ok);
        QCOMPARE(names(report.removed), QStringList{QStringLiteral("a")});
        service.stop();
    }

    // A missing discovery directory is empty, not a failure.
    void missingDirectoryIsEmpty()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("a")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QVERIFY(QDir(f.discovery).removeRecursively());
        const ReloadReport report = service.reloadNow();
        QVERIFY(report.ok);
        QCOMPARE(report.removed.size(), 1);
        QVERIFY(service.sources().isEmpty());
        service.stop();
    }

    // A reload requested while one is being applied (from a sourcesChanged
    // handler) is refused and folded into one more reload right after.
    void reentrantReloadIsCoalesced()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("value"), QStringLiteral("a")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        std::optional<ReloadReport> inner;
        auto connection = connect(&service, &CustomMetricsModel::sourcesChanged, this, [&] {
            if (!inner)
                inner = service.reloadNow();
        });
        writeJson(f.path(QStringLiteral("b.json")), command(QStringLiteral("b"), QStringLiteral("B"), {QStringLiteral("value"), QStringLiteral("b")}));
        const ReloadReport outer = service.reloadNow();
        disconnect(connection);
        QVERIFY(outer.ok);
        QCOMPARE(outer.added.size(), 1);
        QVERIFY(inner && !inner->ok);
        QVERIFY(inner->error.contains(QStringLiteral("already running")));
        QSignalSpy published(&service, &CustomMetricsModel::sourcesChanged);
        QVERIFY(published.wait(2000)); // the folded reload
        QCOMPARE(service.sources().size(), 2);
        service.stop();
    }

    // 2.1.3 execution warnings survive a reload: a failing command keeps its
    // Error and message, and it is not a configuration error.
    void executionWarningsKept()
    {
        Fixture f;
        const QString control = f.temporary.filePath(QStringLiteral("control"));
        writeFile(control, "3\n\nsomething broke\n");
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("control"), control}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("a"))->state, State::Error, 5000);
        const QString message = byConfiguredId(service, QStringLiteral("a"))->message;
        QVERIFY(message.contains(QStringLiteral("something broke")));
        const ReloadReport report = service.reloadNow();
        QVERIFY(report.errors.isEmpty());
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->state, State::Error);
        QCOMPARE(byConfiguredId(service, QStringLiteral("a"))->message, message);
        service.stop();
    }

    // The Qt frontends follow a reload through sourcesChanged alone, as
    // they follow the backend: the Dashboard and the Metrics Bar drop a
    // removed metric, show a renamed one with its value, and rebuild a group
    // with its new children; no window is reopened.
    void frontendsFollowAReload()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("Alpha"), {QStringLiteral("value"), QStringLiteral("7")}));
        writeJson(f.path(QStringLiteral("b.json")), command(QStringLiteral("b"), QStringLiteral("Beta"), {QStringLiteral("value"), QStringLiteral("8")}));
        CustomMetricsService service(&f.settings, f.discovery);
        DashboardWindow dashboard(f.temporary.path().toStdString());
        MetricsBarModel bar(f.temporary.path().toStdString());
        connect(&service, &CustomMetricsModel::sourcesChanged, &dashboard, &DashboardWindow::updateCustomMetrics);
        connect(&service, &CustomMetricsModel::sourcesChanged, &bar, &MetricsBarModel::updateCustom);
        const QString a = aqtruncat::metricsbar::customId(QFileInfo(f.path(QStringLiteral("a.json"))).canonicalFilePath());
        const QString b = aqtruncat::metricsbar::customId(QFileInfo(f.path(QStringLiteral("b.json"))).canonicalFilePath());
        bar.setConfiguration(true, {a, b});
        dashboard.show();
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("b")), 5000);
        auto titles = [&dashboard] {
            QStringList list;
            for (int index = 0; QLabel *label = dashboard.findChild<QLabel *>(QStringLiteral("customTitle_%1").arg(index)); ++index)
                list.append(label->text());
            return list;
        };
        auto barLabels = [&bar] {
            QStringList list;
            for (const aqtruncat::metricsbar::Item &item : bar.items())
                list.append(item.label + u'=' + item.shortValue);
            return list;
        };
        QTRY_COMPARE_WITH_TIMEOUT(titles(), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(barLabels(), (QStringList{QStringLiteral("Alpha=7"), QStringLiteral("Beta=8")}), 5000);

        writeJson(f.path(QStringLiteral("a.json")), command(QStringLiteral("a"), QStringLiteral("Renamed"), {QStringLiteral("value"), QStringLiteral("7")}));
        QVERIFY(QFile::remove(f.path(QStringLiteral("b.json"))));
        writeCounterGroup(f.discovery, {QStringLiteral("x")});
        QVERIFY(service.reloadNow().ok);
        QTRY_COMPARE_WITH_TIMEOUT(titles(), (QStringList{QStringLiteral("Renamed"), QStringLiteral("Counters")}), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(dashboard.findChild<QWidget *>(QStringLiteral("customChild_1_0")), 5000);
        // The renamed metric keeps its value (display-only change); the
        // removed one leaves the bar, its selection is kept for its return.
        QTRY_COMPARE_WITH_TIMEOUT(barLabels(), QStringList{QStringLiteral("Renamed=7")}, 5000);

        // A child added to the group appears in its card.
        writeJson(f.discovery + QStringLiteral("/counters/y/metric.json"),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("Y")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                     {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), QStringLiteral("y")}}}}});
        QVERIFY(service.reloadNow().ok);
        QTRY_VERIFY_WITH_TIMEOUT(dashboard.findChild<QWidget *>(QStringLiteral("customChild_1_1")), 5000);

        writeJson(f.path(QStringLiteral("b.json")), command(QStringLiteral("b"), QStringLiteral("Beta"), {QStringLiteral("value"), QStringLiteral("8")}));
        QVERIFY(service.reloadNow().ok);
        QTRY_COMPARE_WITH_TIMEOUT(barLabels(), (QStringList{QStringLiteral("Renamed=7"), QStringLiteral("Beta=8")}), 5000);
        service.stop();
    }

    // A missing icon (file or theme) and a value outside
    // display.normalizedRange do not stop a metric: it stays Available
    // with a warning, listed by every reload until it is fixed (the
    // user's wrong-icon and wrong-range examples).
    void warningsDoNotStopAMetric()
    {
        Fixture f;
        const QByteArray dataHome = qgetenv("XDG_DATA_HOME");
        const QByteArray dataDirs = qgetenv("XDG_DATA_DIRS");
        const QString icons = f.temporary.filePath(QStringLiteral("share/icons/test-theme"));
        writeFile(icons + QStringLiteral("/index.theme"), "[Icon Theme]\nName=Test\nDirectories=16x16/apps\n");
        writeFile(icons + QStringLiteral("/16x16/apps/aqtruncat-test-present.svg"), "<svg/>");
        qputenv("XDG_DATA_HOME", f.temporary.filePath(QStringLiteral("share")).toUtf8());
        qputenv("XDG_DATA_DIRS", f.temporary.filePath(QStringLiteral("none")).toUtf8());
        const auto symbol = [](const QString &type, const QString &value) {
            return QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("value"), value}};
        };
        const auto range = [](double minimum, double maximum) {
            return QJsonObject{{QStringLiteral("normalizedRange"),
                                QJsonObject{{QStringLiteral("minimum"), minimum}, {QStringLiteral("maximum"), maximum}}}};
        };
        QJsonObject wrongIcon = command(QStringLiteral("wrong-icon"), QStringLiteral("WrongIcon"), {QStringLiteral("value"), QStringLiteral("1")});
        wrongIcon.insert(QStringLiteral("symbol"), symbol(QStringLiteral("file"), QStringLiteral("./d10.svg")));
        wrongIcon.insert(QStringLiteral("display"), range(0, 1));
        writeJson(f.path(QStringLiteral("wrong-icon/metric.json")), wrongIcon);
        QJsonObject wrongRange = command(QStringLiteral("wrong-range"), QStringLiteral("WrongRange"), {QStringLiteral("value"), QStringLiteral("10")});
        wrongRange.insert(QStringLiteral("symbol"), symbol(QStringLiteral("theme"), QStringLiteral("aqtruncat-test-present")));
        wrongRange.insert(QStringLiteral("display"), range(0, 1));
        writeJson(f.path(QStringLiteral("wrong-range.json")), wrongRange);
        QJsonObject theme = command(QStringLiteral("theme"), QStringLiteral("Theme"), {QStringLiteral("value"), QStringLiteral("x")});
        theme.insert(QStringLiteral("symbol"), symbol(QStringLiteral("theme"), QStringLiteral("aqtruncat-test-absent")));
        writeJson(f.path(QStringLiteral("theme.json")), theme);

        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("wrong-icon")) && settled(service, QStringLiteral("wrong-range"))
                                     && settled(service, QStringLiteral("theme")), 5000);
        const QString missing = f.path(QStringLiteral("wrong-icon/d10.svg"));
        QCOMPARE(byConfiguredId(service, QStringLiteral("wrong-icon"))->warnings,
                 QStringList{QStringLiteral("icon file %1 does not exist").arg(missing)});
        QCOMPARE(byConfiguredId(service, QStringLiteral("theme"))->warnings,
                 QStringList{QStringLiteral("icon \"aqtruncat-test-absent\" is not in any installed icon theme")});
        const Source *outside = byConfiguredId(service, QStringLiteral("wrong-range"));
        QCOMPARE(outside->warnings, QStringList{QStringLiteral("value 10 is outside display.normalizedRange 0–1")});
        QCOMPARE(outside->snapshot->metrics.first().formattedValue, QStringLiteral("10")); // still shown
        QVERIFY(outside->snapshot->metrics.first().warning);
        QCOMPARE(outside->snapshot->metrics.first().detail, QStringLiteral("outside the range 0–1"));

        ReloadReport report = service.reloadNow();
        QVERIFY(report.ok && report.errors.isEmpty());
        QCOMPARE(report.unchanged, 3);
        QCOMPARE(names(report.warnings), (QStringList{QStringLiteral("theme"), QStringLiteral("wrong-icon"), QStringLiteral("wrong-range")}));
        QVERIFY(reloadSummary(report).contains(QStringLiteral("3 with warnings: ")));

        // Fixed: the icon file appears (the configuration is unchanged, the
        // next reload finds it), the range is widened (what is read changed:
        // a new run, inside it now).
        writeFile(missing, "<svg/>");
        wrongRange.insert(QStringLiteral("display"), range(0, 20));
        writeJson(f.path(QStringLiteral("wrong-range.json")), wrongRange);
        report = service.reloadNow();
        QVERIFY(report.ok);
        QCOMPARE(names(report.changed), QStringList{QStringLiteral("wrong-range")});
        QCOMPARE(names(report.warnings), QStringList{QStringLiteral("theme")});
        QVERIFY(byConfiguredId(service, QStringLiteral("wrong-icon"))->warnings.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("wrong-range")), 5000);
        outside = byConfiguredId(service, QStringLiteral("wrong-range"));
        QVERIFY(outside->warnings.isEmpty());
        QVERIFY(!outside->snapshot->metrics.first().warning);
        service.stop();

        qputenv("XDG_DATA_HOME", dataHome);
        qputenv("XDG_DATA_DIRS", dataDirs);
        if (dataHome.isNull())
            qunsetenv("XDG_DATA_HOME");
        if (dataDirs.isNull())
            qunsetenv("XDG_DATA_DIRS");
    }

    void notRunning()
    {
        Fixture f;
        CustomMetricsService service(&f.settings, f.discovery);
        const ReloadReport report = service.reloadNow();
        QVERIFY(!report.ok);
        QVERIFY(!report.error.isEmpty());
    }
};

QTEST_MAIN(TestCustomMetricsReload)
#include "tst_custommetricsreload.moc"
