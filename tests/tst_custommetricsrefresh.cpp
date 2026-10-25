// v2F1.7: manual refresh of Custom Metrics (CustomMetricsService::refreshNow,
// an icon clicked): a standalone metric, a group's child, a group and its
// nested descendants; disabled, unknown and invalid targets; never a second
// process of a running command (rapid clicks, a periodic run at the same
// moment, many children at once); timeouts, exit status 69, failures and
// recovery through the same pipeline; reloads and removals while a refresh
// runs, whose stale results never come back; the Dashboard's icons.
#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "app/dashboardwindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>

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

QJsonObject command(const QString &id, const QString &title, const QJsonArray &arguments, int interval = 3600,
                    int timeout = 10)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                              {QStringLiteral("executable"), helperPath()},
                                                              {QStringLiteral("arguments"), arguments},
                                                              {QStringLiteral("intervalSeconds"), interval},
                                                              {QStringLiteral("timeoutSeconds"), timeout}}}};
}

QJsonObject disabled(QJsonObject object)
{
    object.insert(QStringLiteral("enabled"), false);
    return object;
}

QJsonObject group(const QString &id, const QString &title)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("title"), title},
                       {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                              {QStringLiteral("executable"), helperPath()},
                                                              {QStringLiteral("intervalSeconds"), 3600}}}};
}

// A group's child counting its runs in a file (the group owns the program).
QJsonObject counterChild(const QString &title, const QString &count)
{
    return QJsonObject{{QStringLiteral("title"), title},
                       {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                              {QStringLiteral("arguments"),
                                                               QJsonArray{QStringLiteral("counter"), count}}}}};
}

QJsonObject symbol(const QString &file)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("file")}, {QStringLiteral("value"), file}};
}

const Source *byConfiguredId(const CustomMetricsService &service, const QString &id)
{
    for (const Source &source : service.sources()) {
        if (source.configuredId == id)
            return &source;
    }
    return nullptr;
}

QString idOf(const CustomMetricsService &service, const QString &configuredId)
{
    const Source *source = byConfiguredId(service, configuredId);
    return source ? source->id : QString();
}

State stateOf(const CustomMetricsService &service, const QString &id)
{
    const Source *source = byConfiguredId(service, id);
    return source ? source->state : State::Pending;
}

QString valueOf(const CustomMetricsService &service, const QString &id)
{
    const Source *source = byConfiguredId(service, id);
    return source && source->snapshot && !source->snapshot->metrics.isEmpty()
        ? source->snapshot->metrics.first().formattedValue : QString();
}

bool settled(const CustomMetricsService &service, const QString &id)
{
    return stateOf(service, id) == State::Available;
}

// Past the spacing that coalesces a refresh with the run that just ended.
void pastSpacing()
{
    QTest::qWait(1100);
}

struct Fixture {
    QTemporaryDir temporary;
    QString discovery = temporary.filePath(QStringLiteral("metrics"));
    aqtruncat::AppSettings settings{QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini"))};
    QString path(const QString &name) const { return discovery + u'/' + name; }
    QString count(const QString &name) const { return temporary.filePath(name + QStringLiteral(".count")); }
};

} // namespace

class TestCustomMetricsRefresh : public QObject
{
    Q_OBJECT

private slots:
    // Only the selected metric runs again, at once; its periodic schedule
    // continues from this run (no extra run right after it).
    void standaloneMetric()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("a.json")),
                  command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("counter"), f.count(QStringLiteral("a"))}));
        writeJson(f.path(QStringLiteral("b.json")),
                  command(QStringLiteral("b"), QStringLiteral("B"), {QStringLiteral("counter"), f.count(QStringLiteral("b"))}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("b")), 5000);
        pastSpacing();

        QSignalSpy finished(&service, &CustomMetricsModel::refreshFinished);
        const QString a = idOf(service, QStringLiteral("a"));
        service.refreshMetric(a);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), a);
        const RefreshResult result = finished.at(0).at(1).value<RefreshResult>();
        QCOMPARE(result.status, RefreshStatus::Accepted);
        QCOMPARE(result.started, QStringList{a});
        QVERIFY(result.coalesced.isEmpty());
        QCOMPARE(stateOf(service, QStringLiteral("a")), State::Pending); // running, its value kept
        QCOMPARE(valueOf(service, QStringLiteral("a")), QStringLiteral("1"));
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("a")), QStringLiteral("2"), 5000);
        QVERIFY(settled(service, QStringLiteral("a")));
        QTest::qWait(500);
        QCOMPARE(readFile(f.count(QStringLiteral("a"))), QByteArray("2"));
        QCOMPARE(readFile(f.count(QStringLiteral("b"))), QByteArray("1"));
        service.stop();
    }

    // A child's icon: that child only, with its own (inherited) command.
    void groupedChild()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("g/metric.json")), group(QStringLiteral("g"), QStringLiteral("G")));
        writeJson(f.path(QStringLiteral("g/x/metric.json")), counterChild(QStringLiteral("X"), f.count(QStringLiteral("x"))));
        writeJson(f.path(QStringLiteral("g/y/metric.json")), counterChild(QStringLiteral("Y"), f.count(QStringLiteral("y"))));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("g/x")) && settled(service, QStringLiteral("g/y")), 5000);
        pastSpacing();

        const RefreshResult result = service.refreshNow(idOf(service, QStringLiteral("g/x")));
        QCOMPARE(result.status, RefreshStatus::Accepted);
        QCOMPARE(result.started, QStringList{idOf(service, QStringLiteral("g/x"))});
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("g/x")), QStringLiteral("2"), 5000);
        QTest::qWait(300);
        QCOMPARE(readFile(f.count(QStringLiteral("y"))), QByteArray("1"));
        service.stop();
    }

    // A group's icon: every enabled executable descendant, nested groups
    // included; disabled children and a disabled nested group's children
    // never run; nothing outside the group runs; the group itself has no
    // command.
    void groupAndNestedDescendants()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("g/metric.json")), group(QStringLiteral("g"), QStringLiteral("G")));
        writeJson(f.path(QStringLiteral("g/x/metric.json")), counterChild(QStringLiteral("X"), f.count(QStringLiteral("x"))));
        writeJson(f.path(QStringLiteral("g/off/metric.json")), disabled(counterChild(QStringLiteral("Off"), f.count(QStringLiteral("off")))));
        writeJson(f.path(QStringLiteral("g/n/metric.json")),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("N")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")}}}});
        writeJson(f.path(QStringLiteral("g/n/z/metric.json")), counterChild(QStringLiteral("Z"), f.count(QStringLiteral("z"))));
        writeJson(f.path(QStringLiteral("g/m/metric.json")),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("M")}, {QStringLiteral("enabled"), false},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")}}}});
        writeJson(f.path(QStringLiteral("g/m/w/metric.json")), counterChild(QStringLiteral("W"), f.count(QStringLiteral("w"))));
        writeJson(f.path(QStringLiteral("outside.json")),
                  command(QStringLiteral("outside"), QStringLiteral("Outside"), {QStringLiteral("counter"), f.count(QStringLiteral("outside"))}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("g/x")) && settled(service, QStringLiteral("g/n/z"))
                                     && settled(service, QStringLiteral("outside")), 5000);
        pastSpacing();

        const RefreshResult result = service.refreshNow(idOf(service, QStringLiteral("g")));
        QCOMPARE(result.status, RefreshStatus::Accepted);
        QStringList started = result.started;
        started.sort();
        QStringList expected{idOf(service, QStringLiteral("g/x")), idOf(service, QStringLiteral("g/n/z"))};
        expected.sort();
        QCOMPARE(started, expected);
        QTRY_VERIFY_WITH_TIMEOUT(valueOf(service, QStringLiteral("g/x")) == QLatin1String("2")
                                     && valueOf(service, QStringLiteral("g/n/z")) == QLatin1String("2"), 5000);
        QTest::qWait(300);
        QCOMPARE(readFile(f.count(QStringLiteral("outside"))), QByteArray("1"));
        QVERIFY(!QFile::exists(f.count(QStringLiteral("off"))));
        QVERIFY(!QFile::exists(f.count(QStringLiteral("w"))));

        // A nested group refreshes its own descendants only.
        pastSpacing();
        const RefreshResult nested = service.refreshNow(idOf(service, QStringLiteral("g/n")));
        QCOMPARE(nested.started, QStringList{idOf(service, QStringLiteral("g/n/z"))});
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("g/n/z")), QStringLiteral("3"), 5000);
        QCOMPARE(readFile(f.count(QStringLiteral("x"))), QByteArray("2"));
        service.stop();
    }

    // Disabled targets never run: a metric, a child, a group and the
    // children of a disabled group; a group whose children are all
    // disabled runs nothing.
    void disabledTargets()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("off.json")),
                  disabled(command(QStringLiteral("off"), QStringLiteral("Off"), {QStringLiteral("counter"), f.count(QStringLiteral("off"))})));
        QJsonObject offGroup = group(QStringLiteral("dg"), QStringLiteral("DG"));
        offGroup.insert(QStringLiteral("enabled"), false);
        writeJson(f.path(QStringLiteral("dg/metric.json")), offGroup);
        writeJson(f.path(QStringLiteral("dg/c/metric.json")), counterChild(QStringLiteral("C"), f.count(QStringLiteral("c"))));
        writeJson(f.path(QStringLiteral("eg/metric.json")), group(QStringLiteral("eg"), QStringLiteral("EG")));
        writeJson(f.path(QStringLiteral("eg/d/metric.json")), disabled(counterChild(QStringLiteral("D"), f.count(QStringLiteral("d")))));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("eg/d")), 5000);

        for (const QString &id : {QStringLiteral("off"), QStringLiteral("dg"), QStringLiteral("dg/c"), QStringLiteral("eg/d")}) {
            const RefreshResult result = service.refreshNow(idOf(service, id));
            QVERIFY2(result.status == RefreshStatus::Disabled, qPrintable(id));
            QVERIFY2(result.error.contains(QLatin1String("disabled")), qPrintable(result.error));
            QVERIFY(result.started.isEmpty() && result.coalesced.isEmpty());
        }
        const RefreshResult empty = service.refreshNow(idOf(service, QStringLiteral("eg")));
        QCOMPARE(empty.status, RefreshStatus::Accepted);
        QVERIFY(empty.started.isEmpty() && empty.coalesced.isEmpty());
        QTest::qWait(300);
        for (const QString &name : {QStringLiteral("off"), QStringLiteral("c"), QStringLiteral("d")})
            QVERIFY2(!QFile::exists(f.count(name)), qPrintable(name));
        service.stop();
    }

    // Unknown, empty, snapshot and broken targets; a stopped service.
    void unknownAndInvalidTargets()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("neo.json")),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("Neo")},
                              {QStringLiteral("lastUpdatedDate"), QStringLiteral("2026-10-24T10:00:00Z")},
                              {QStringLiteral("metrics"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("V")},
                                                                                 {QStringLiteral("formattedValue"), QStringLiteral("1")}}}}});
        writeFile(f.path(QStringLiteral("broken.json")),
                  R"({"title": "Broken", "source": {"type": "command", "intervalSeconds": 60}})");
        CustomMetricsService service(&f.settings, f.discovery);
        QCOMPARE(service.refreshNow(QStringLiteral("/anything.json")).status, RefreshStatus::NotRunnable); // not started
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), qsizetype(2), 5000);

        RefreshResult result = service.refreshNow(f.path(QStringLiteral("missing.json")));
        QCOMPARE(result.status, RefreshStatus::NotFound);
        QVERIFY(result.error.contains(QLatin1String("missing.json")));
        QCOMPARE(service.refreshNow(QStringLiteral(" ")).status, RefreshStatus::Invalid);
        const QString neo = QFileInfo(f.path(QStringLiteral("neo.json"))).canonicalFilePath();
        QCOMPARE(service.sources().at(1).id, neo);
        QCOMPARE(service.sources().at(1).state, State::Available);
        result = service.refreshNow(neo);
        QCOMPARE(result.status, RefreshStatus::Invalid);
        QVERIFY2(result.error.contains(QLatin1String("not a command")), qPrintable(result.error));
        const QString broken = QFileInfo(f.path(QStringLiteral("broken.json"))).canonicalFilePath();
        result = service.refreshNow(broken);
        QCOMPARE(result.status, RefreshStatus::NotRunnable);
        QVERIFY2(result.error.contains(QLatin1String("cannot run")), qPrintable(result.error));
        service.stop();
        QCOMPARE(service.refreshNow(neo).status, RefreshStatus::NotRunnable);
    }

    // Never two processes of one command: refreshes while it runs, rapid
    // repeated clicks and periodic runs falling due at the same time are
    // coalesced (the helper records any overlap).
    void noOverlappingExecutions()
    {
        Fixture f;
        const QString lock = f.temporary.filePath(QStringLiteral("lock"));
        const QString violation = f.temporary.filePath(QStringLiteral("violation"));
        writeJson(f.path(QStringLiteral("slow.json")),
                  command(QStringLiteral("slow"), QStringLiteral("Slow"),
                          {QStringLiteral("overlap"), lock, violation, QStringLiteral("400")}, 1));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("slow")), 5000);
        const QString slow = idOf(service, QStringLiteral("slow"));
        // The first run is under way (it starts at once).
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(lock), 5000);
        RefreshResult result = service.refreshNow(slow);
        QCOMPARE(result.status, RefreshStatus::Accepted);
        QCOMPARE(result.coalesced, QStringList{slow});
        QVERIFY(result.started.isEmpty());

        // 3 s of clicks every 20 ms against a 1 s interval: runs start, end
        // and fall due meanwhile; none overlaps and the runs stay few.
        int started = 0;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 3000) {
            started += int(service.refreshNow(slow).started.size());
            QTest::qWait(20);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(lock), 5000);
        QVERIFY2(!QFile::exists(violation), "two runs of one command overlapped");
        // At most one run per (400 ms run + 1 s spacing) from the clicks.
        QVERIFY2(started <= 3, qPrintable(QString::number(started)));
        service.stop();
    }

    // A group refresh starts each child at most once, however often it is
    // clicked: the total of processes is bounded by the children.
    void groupConcurrencyIsBounded()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("g/metric.json")), group(QStringLiteral("g"), QStringLiteral("G")));
        const QStringList children{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")};
        for (const QString &child : children) {
            writeJson(f.path(QStringLiteral("g/%1/metric.json").arg(child)),
                      QJsonObject{{QStringLiteral("title"), child},
                                  {QStringLiteral("source"), QJsonObject{
                                       {QStringLiteral("type"), QStringLiteral("command")},
                                       {QStringLiteral("arguments"), QJsonArray{QStringLiteral("overlap"),
                                                                                f.temporary.filePath(child + QStringLiteral(".lock")),
                                                                                f.temporary.filePath(child + QStringLiteral(".violation")),
                                                                                QStringLiteral("500")}}}}});
        }
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("g/d")), 5000);
        const QString g = idOf(service, QStringLiteral("g"));
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("g/a")) && settled(service, QStringLiteral("g/d")), 5000);
        pastSpacing();

        const RefreshResult first = service.refreshNow(g);
        QCOMPARE(first.started.size(), children.size());
        for (int click = 0; click < 10; ++click) {
            const RefreshResult again = service.refreshNow(g);
            QVERIFY(again.started.isEmpty());
            QCOMPARE(again.coalesced.size(), children.size());
        }
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("g/a")) && settled(service, QStringLiteral("g/b"))
                                     && settled(service, QStringLiteral("g/c")) && settled(service, QStringLiteral("g/d")), 5000);
        for (const QString &child : children)
            QVERIFY2(!QFile::exists(f.temporary.filePath(child + QStringLiteral(".violation"))), qPrintable(child));
        service.stop();
    }

    // A refresh goes through the scheduler's own pipeline: its timeout,
    // exit status 69 (Unavailable, values kept), failures (Error) and the
    // recovery that clears them.
    void executionOutcomes()
    {
        Fixture f;
        const QString control = f.temporary.filePath(QStringLiteral("control"));
        writeFile(control, "0\n5\n");
        writeJson(f.path(QStringLiteral("c.json")),
                  command(QStringLiteral("c"), QStringLiteral("C"), {QStringLiteral("control"), control}));
        writeJson(f.path(QStringLiteral("t.json")),
                  command(QStringLiteral("t"), QStringLiteral("T"), {QStringLiteral("sleep"), QStringLiteral("60000"), QStringLiteral("x")}, 3600, 1));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("c")), 5000);
        const QString c = idOf(service, QStringLiteral("c"));

        // Timeout: the manual run is bounded like any other.
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(service, QStringLiteral("t")), State::Error, 5000);
        QVERIFY(byConfiguredId(service, QStringLiteral("t"))->message.contains(QLatin1String("timed out")));
        pastSpacing();
        QCOMPARE(service.refreshNow(idOf(service, QStringLiteral("t"))).started.size(), qsizetype(1));
        QCOMPARE(stateOf(service, QStringLiteral("t")), State::Pending);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(service, QStringLiteral("t")), State::Error, 5000);
        QVERIFY(byConfiguredId(service, QStringLiteral("t"))->message.contains(QLatin1String("timed out")));

        // Exit status 69: Unavailable with its reason, the last value kept.
        pastSpacing();
        writeFile(control, "69\n\nnot here today\n");
        QCOMPARE(service.refreshNow(c).started, QStringList{c});
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(service, QStringLiteral("c")), State::Unavailable, 5000);
        QCOMPARE(byConfiguredId(service, QStringLiteral("c"))->message, QStringLiteral("not here today"));
        QCOMPARE(valueOf(service, QStringLiteral("c")), QStringLiteral("5"));

        // A failure: Error with the reason.
        pastSpacing();
        writeFile(control, "3\n\nboom\n");
        service.refreshNow(c);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(service, QStringLiteral("c")), State::Error, 5000);
        QVERIFY(byConfiguredId(service, QStringLiteral("c"))->message.contains(QLatin1String("boom")));

        // Fixed: a refresh recovers at once, the error is gone.
        pastSpacing();
        writeFile(control, "0\n9\n");
        service.refreshNow(c);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(service, QStringLiteral("c")), State::Available, 5000);
        QCOMPARE(valueOf(service, QStringLiteral("c")), QStringLiteral("9"));
        QVERIFY(byConfiguredId(service, QStringLiteral("c"))->message.isEmpty());
        service.stop();
    }

    // A reload while a refresh runs: a display-only change keeps the run
    // (its result arrives); an execution change replaces it, and the
    // replaced run's value never appears.
    void reloadWhileRefreshing()
    {
        Fixture f;
        writeJson(f.path(QStringLiteral("r.json")),
                  command(QStringLiteral("r"), QStringLiteral("R"), {QStringLiteral("sleep"), QStringLiteral("600"), QStringLiteral("old")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("r")), 5000);
        const QString r = idOf(service, QStringLiteral("r"));
        pastSpacing();

        QCOMPARE(service.refreshNow(r).started, QStringList{r});
        writeJson(f.path(QStringLiteral("r.json")),
                  command(QStringLiteral("r"), QStringLiteral("Renamed"), {QStringLiteral("sleep"), QStringLiteral("600"), QStringLiteral("old")}));
        QVERIFY(service.reloadNow().ok);
        QCOMPARE(stateOf(service, QStringLiteral("r")), State::Pending); // still the same run
        QCOMPARE(service.refreshNow(r).coalesced, QStringList{r});
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("r")), 5000);
        QCOMPARE(byConfiguredId(service, QStringLiteral("r"))->snapshot->title, QStringLiteral("Renamed"));

        pastSpacing();
        QCOMPARE(service.refreshNow(r).started, QStringList{r});
        QStringList seen;
        connect(&service, &CustomMetricsModel::sourcesChanged, this, [&] { seen.append(valueOf(service, QStringLiteral("r"))); });
        writeJson(f.path(QStringLiteral("r.json")),
                  command(QStringLiteral("r"), QStringLiteral("Renamed"), {QStringLiteral("sleep"), QStringLiteral("300"), QStringLiteral("new")}));
        QVERIFY(service.reloadNow().ok);
        QTRY_COMPARE_WITH_TIMEOUT(valueOf(service, QStringLiteral("r")), QStringLiteral("new"), 5000);
        QTest::qWait(800); // past the replaced run's end
        QCOMPARE(valueOf(service, QStringLiteral("r")), QStringLiteral("new"));
        QVERIFY(!seen.contains(QStringLiteral("old")));
        service.stop();
    }

    // A metric removed while its refresh runs: the run stops, its result
    // never comes back, and the id is then unknown.
    void removedWhileRefreshing()
    {
        Fixture f;
        const QString pid = f.temporary.filePath(QStringLiteral("pid"));
        writeJson(f.path(QStringLiteral("gone.json")),
                  command(QStringLiteral("gone"), QStringLiteral("Gone"), {QStringLiteral("value"), QStringLiteral("1")}));
        CustomMetricsService service(&f.settings, f.discovery);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("gone")), 5000);
        const QString gone = idOf(service, QStringLiteral("gone"));
        writeJson(f.path(QStringLiteral("gone.json")),
                  command(QStringLiteral("gone"), QStringLiteral("Gone"), {QStringLiteral("pid-sleep"), pid, QStringLiteral("60000")}));
        QVERIFY(service.reloadNow().ok); // an execution change runs at once
        QTRY_VERIFY_WITH_TIMEOUT(!readFile(pid).isEmpty(), 5000);
        QCOMPARE(service.refreshNow(gone).coalesced, QStringList{gone});

        QSignalSpy changes(&service, &CustomMetricsModel::sourcesChanged);
        QVERIFY(QFile::remove(f.path(QStringLiteral("gone.json"))));
        const ReloadReport report = service.reloadNow();
        QCOMPARE(report.removed.size(), qsizetype(1));
        QVERIFY(!byConfiguredId(service, QStringLiteral("gone")));
        QTRY_VERIFY_WITH_TIMEOUT(::kill(pid_t(readFile(pid).toInt()), 0) == -1, 5000);
        const qsizetype published = changes.size();
        QTest::qWait(300);
        QCOMPARE(changes.size(), published);
        QCOMPARE(service.refreshNow(gone).status, RefreshStatus::NotFound);
        service.stop();
    }

    // The Dashboard: a metric's, a group's and a child's own icon request a
    // refresh (pointer cursor, tooltip); the answer is shown briefly. A
    // snapshot file's card has nothing to click.
    void dashboardIcons()
    {
        Fixture f;
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::red);
        const QString red = f.temporary.filePath(QStringLiteral("red.png"));
        QVERIFY(image.save(red));
        image.fill(Qt::blue);
        const QString blue = f.temporary.filePath(QStringLiteral("blue.png"));
        QVERIFY(image.save(blue));

        QJsonObject a = command(QStringLiteral("a"), QStringLiteral("A"), {QStringLiteral("counter"), f.count(QStringLiteral("a"))});
        a.insert(QStringLiteral("symbol"), symbol(red));
        writeJson(f.path(QStringLiteral("a.json")), a);
        QJsonObject g = group(QStringLiteral("g"), QStringLiteral("G"));
        g.insert(QStringLiteral("symbol"), symbol(red));
        writeJson(f.path(QStringLiteral("g/metric.json")), g);
        writeJson(f.path(QStringLiteral("g/x/metric.json")), counterChild(QStringLiteral("X"), f.count(QStringLiteral("x"))));
        QJsonObject y = counterChild(QStringLiteral("Y"), f.count(QStringLiteral("y")));
        y.insert(QStringLiteral("symbol"), symbol(blue));
        writeJson(f.path(QStringLiteral("g/y/metric.json")), y);
        writeJson(f.path(QStringLiteral("neo.json")),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("Neo")},
                              {QStringLiteral("lastUpdatedDate"), QStringLiteral("2026-10-24T10:00:00Z")},
                              {QStringLiteral("metrics"), QJsonArray{QJsonObject{{QStringLiteral("title"), QStringLiteral("V")},
                                                                                 {QStringLiteral("formattedValue"), QStringLiteral("1")}}}}});

        CustomMetricsService service(&f.settings, f.discovery);
        DashboardWindow dashboard(f.temporary.path().toStdString());
        connect(&service, &CustomMetricsModel::sourcesChanged, &dashboard, &DashboardWindow::updateCustomMetrics);
        connect(&dashboard, &DashboardWindow::refreshRequested, &service, &CustomMetricsModel::refreshMetric);
        connect(&service, &CustomMetricsModel::refreshFinished, &dashboard, &DashboardWindow::showRefreshResult);
        QSignalSpy requested(&dashboard, &DashboardWindow::refreshRequested);
        dashboard.show();
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service, QStringLiteral("a")) && settled(service, QStringLiteral("g/x"))
                                     && settled(service, QStringLiteral("g/y")), 5000);
        pastSpacing();

        // Card order: a.json, neo.json, then the package g (the service's order).
        const auto icon = [&dashboard](const QString &name) {
            QLabel *label = nullptr;
            [&] { QTRY_VERIFY2_WITH_TIMEOUT((label = dashboard.findChild<QLabel *>(name)) != nullptr, qPrintable(name), 5000); }();
            return label;
        };
        QLabel *aIcon = icon(QStringLiteral("customIcon_0"));
        QCOMPARE(aIcon->cursor().shape(), Qt::PointingHandCursor);
        QCOMPARE(aIcon->toolTip(), QStringLiteral("Refresh now"));
        QTest::mouseClick(aIcon, Qt::LeftButton);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.at(0).at(0).toString(), idOf(service, QStringLiteral("a")));
        QCOMPARE(dashboard.refreshFeedback(), QStringLiteral("Refreshing…"));
        QTRY_COMPARE_WITH_TIMEOUT(readFile(f.count(QStringLiteral("a"))), QByteArray("2"), 5000);
        QTest::mouseClick(icon(QStringLiteral("customIcon_0")), Qt::LeftButton); // right after it: coalesced
        QCOMPARE(dashboard.refreshFeedback(), QStringLiteral("Already running or just refreshed"));
        QTest::mouseClick(icon(QStringLiteral("customIcon_0")), Qt::RightButton); // only the left button
        QCOMPARE(requested.size(), 2);

        QLabel *groupIcon = icon(QStringLiteral("customIcon_2"));
        QCOMPARE(groupIcon->toolTip(), QStringLiteral("Refresh the group now"));
        QTest::mouseClick(groupIcon, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(readFile(f.count(QStringLiteral("x"))) == "2" && readFile(f.count(QStringLiteral("y"))) == "2", 5000);
        QCOMPARE(readFile(f.count(QStringLiteral("a"))), QByteArray("2"));

        pastSpacing();
        QTest::mouseClick(icon(QStringLiteral("customChildIcon_2_1")), Qt::LeftButton); // y's own icon
        QTRY_COMPARE_WITH_TIMEOUT(readFile(f.count(QStringLiteral("y"))), QByteArray("3"), 5000);
        QCOMPARE(readFile(f.count(QStringLiteral("x"))), QByteArray("2"));
        QVERIFY(!dashboard.findChild<QLabel *>(QStringLiteral("customChildIcon_2_0"))); // x shares the group's

        // A snapshot file's card: nothing on it requests a refresh.
        QCOMPARE(icon(QStringLiteral("customTitle_1"))->text(), QStringLiteral("Neo"));
        const QWidget *neoCard = dashboard.findChild<QWidget *>(QStringLiteral("customSource_1"));
        QVERIFY(neoCard);
        for (const QLabel *label : neoCard->findChildren<QLabel *>())
            QVERIFY(label->property("refreshSource").toString().isEmpty());

        // A refused request says why.
        RefreshResult refused;
        refused.status = RefreshStatus::NotFound;
        refused.error = QStringLiteral("gone");
        dashboard.showRefreshResult(QStringLiteral("/x"), refused);
        QCOMPARE(dashboard.refreshFeedback(), QStringLiteral("gone"));
        service.stop();
    }
};

QTEST_MAIN(TestCustomMetricsRefresh)
#include "tst_custommetricsrefresh.moc"
