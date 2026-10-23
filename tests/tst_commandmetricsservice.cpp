#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "custommetrics/commandmetric.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <cerrno>
#include <csignal>
#include <unistd.h>

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
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

void writeConfiguration(const QString &path, const QString &executable, const QStringList &arguments,
                        int intervalSeconds = 1, int timeoutSeconds = 3,
                        QJsonValue symbol = QJsonValue::Undefined)
{
    QJsonArray args;
    for (const QString &argument : arguments)
        args.append(argument);
    QJsonObject object{
        {QStringLiteral("id"), QStringLiteral("fixture")},
        {QStringLiteral("title"), QStringLiteral("Fixture")},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("command")},
             {QStringLiteral("executable"), executable},
             {QStringLiteral("arguments"), args},
             {QStringLiteral("intervalSeconds"), intervalSeconds},
             {QStringLiteral("timeoutSeconds"), timeoutSeconds}}},
        {QStringLiteral("display"), QJsonObject{{QStringLiteral("suffix"), QStringLiteral(" units")}}}
    };
    if (!symbol.isUndefined())
        object.insert(QStringLiteral("symbol"), symbol);
    writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void installRelativeHelper(const QString &directory)
{
    const QString destination = directory + QStringLiteral("/metric-helper");
    QVERIFY2(QFile::copy(helperPath(), destination), qPrintable(destination));
    QVERIFY(QFile::setPermissions(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                               | QFileDevice::ExeOwner));
}

pid_t readPid(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return pid_t(file.readAll().trimmed().toInt());
}

bool processGone(pid_t pid)
{
    errno = 0;
    return ::kill(pid, 0) == -1 && errno == ESRCH;
}

const Source &onlySource(const CustomMetricsService &service)
{
    if (service.sources().size() != 1)
        qFatal("expected exactly one source, got %lld", qlonglong(service.sources().size()));
    return service.sources().first();
}

} // namespace

class TestCommandMetricsService : public QObject
{
    Q_OBJECT

private slots:
    void absoluteExecutableArgumentsAndStateTransitions()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeConfiguration(config, helperPath(), {QStringLiteral("arguments"), QStringLiteral("23.4"),
                                                   QStringLiteral("sensor A")});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        QList<State> states;
        connect(&service, &CustomMetricsService::sourcesChanged, this, [&](const QList<Source> &sources) {
            if (!sources.isEmpty())
                states.append(sources.first().state);
        });
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QCOMPARE(onlySource(service).type, SourceType::Command);
        QCOMPARE(onlySource(service).configuredId, QStringLiteral("fixture"));
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue,
                 QStringLiteral("23.4|sensor A units"));
        QCOMPARE(*onlySource(service).snapshot->metricsBarValue,
                 QStringLiteral("23.4|sensor A units"));
        QVERIFY(states.contains(State::Pending));
        QVERIFY(states.contains(State::Available));
    }

    void relativeExecutableAndWorkingDirectoryInDiscoveredPackage()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString package = discovery + QStringLiteral("/self-contained");
        QDir().mkpath(package);
        installRelativeHelper(package);
        writeConfiguration(package + QStringLiteral("/metric.json"), QStringLiteral("./metric-helper"),
                           {QStringLiteral("cwd")});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QVERIFY(onlySource(service).discovered);
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue,
                 QDir::cleanPath(package) + QStringLiteral(" units"));
    }

    void refreshesAndNeverOverlapsSlowExecutions()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        const QString counter = temporary.filePath(QStringLiteral("counter"));
        writeFile(counter, QByteArray("0"));
        writeConfiguration(config, helperPath(), {QStringLiteral("counter"), counter});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue,
                 QStringLiteral("1 units"));
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).snapshot->metrics.first().formattedValue,
                                  QStringLiteral("2 units"), 2500);

        service.stop();
        const QString lock = temporary.filePath(QStringLiteral("active.lock"));
        const QString violation = temporary.filePath(QStringLiteral("overlap"));
        writeConfiguration(config, helperPath(), {QStringLiteral("overlap"), lock, violation,
                                                   QStringLiteral("1500")}, 1, 5);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Pending, 500);
        QVERIFY(onlySource(service).snapshot.has_value());
        QVERIFY(onlySource(service).snapshot->metrics.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2500);
        const QString previousValue = onlySource(service).snapshot->metrics.first().formattedValue;
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Pending, 3000);
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue, previousValue);
        QTest::qWait(1300); // a second run may start only after the first completed + interval
        QVERIFY(!QFileInfo::exists(violation));
    }

    void reportsMissingAndNonExecutableProgramsAsUnavailable()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeConfiguration(config, temporary.filePath(QStringLiteral("missing")), {QStringLiteral("value"), QStringLiteral("x")});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Unavailable, 1000);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("not available")));

        service.stop();
        const QString nonExecutable = temporary.filePath(QStringLiteral("not-executable"));
        QVERIFY(QFile::copy(helperPath(), nonExecutable));
        QVERIFY(QFile::setPermissions(nonExecutable, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        writeConfiguration(config, nonExecutable, {QStringLiteral("value"), QStringLiteral("x")});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Unavailable, 1000);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("not executable")));
    }

    void reportsExitMalformedExcessiveAndTimeoutFailures()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));

        writeConfiguration(config, helperPath(), {QStringLiteral("exit"), QStringLiteral("7"), QStringLiteral("bad sensor")});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 1500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("status 7")));
        service.stop();

        writeConfiguration(config, helperPath(), {QStringLiteral("multiline")});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 1500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("scalar line")));
        service.stop();

        writeConfiguration(config, helperPath(), {QStringLiteral("flood"),
                                                   QString::number(MaxCommandOutputBytes + 4096)});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 1500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("limit")));
        service.stop();

        writeConfiguration(config, helperPath(), {QStringLiteral("flood-stderr"),
                                                   QString::number(MaxCommandOutputBytes + 4096)});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 1500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("limit")));
        service.stop();

        const QString brokenInterpreter = temporary.filePath(QStringLiteral("broken-interpreter"));
        writeFile(brokenInterpreter, QByteArray("#!/definitely/not/a/real/interpreter\n"));
        QVERIFY(QFile::setPermissions(brokenInterpreter, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                           | QFileDevice::ExeOwner));
        writeConfiguration(config, brokenInterpreter, {});
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 1500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("Could not start")));
        service.stop();

        writeConfiguration(config, helperPath(), {QStringLiteral("sleep"), QStringLiteral("3000"),
                                                   QStringLiteral("late")}, 1, 1);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 2500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("timed out")));
    }

    void stopTerminatesAnActiveOwnedProcess()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        const QString pidFile = temporary.filePath(QStringLiteral("pid"));
        writeConfiguration(config, helperPath(), {QStringLiteral("pid-sleep"), pidFile,
                                                   QStringLiteral("30000")}, 1, 60);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile), 1500);
        QFile pid(pidFile);
        QVERIFY(pid.open(QIODevice::ReadOnly));
        const pid_t processId = pid.readAll().trimmed().toInt();
        QVERIFY(processId > 0);
        service.stop();
        errno = 0;
        QCOMPARE(::kill(processId, 0), -1);
        QCOMPARE(errno, ESRCH);
    }

    void timeoutAndStopAlsoTerminateSpawnedHelpers()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        const QString pidFile = temporary.filePath(QStringLiteral("grandchild"));
        writeConfiguration(config, helperPath(), {QStringLiteral("grandchild-sleep"), pidFile,
                                                   QStringLiteral("30000")}, 60, 1);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(readPid(pidFile) > 0, 1500);
        const pid_t timedOut = readPid(pidFile);
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 2500);
        QVERIFY(onlySource(service).message.contains(QStringLiteral("timed out")));
        QTRY_VERIFY_WITH_TIMEOUT(processGone(timedOut), 1000);
        service.stop();

        QFile::remove(pidFile);
        writeConfiguration(config, helperPath(), {QStringLiteral("grandchild-sleep"), pidFile,
                                                   QStringLiteral("30000")}, 60, 60);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(readPid(pidFile) > 0, 1500);
        const pid_t stopped = readPid(pidFile);
        service.stop();
        QTRY_VERIFY_WITH_TIMEOUT(processGone(stopped), 1000);
    }

    void packageDirectoryCreatedBeforeItsConfigurationIsDiscovered()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        QDir().mkpath(discovery);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QVERIFY(service.sources().isEmpty());

        const QString package = discovery + QStringLiteral("/late-package");
        QVERIFY(QDir().mkpath(package));
        QTest::qWait(300); // the empty package is noticed and watched first
        QVERIFY(service.sources().isEmpty());
        writeConfiguration(package + QStringLiteral("/metric.json"), helperPath(),
                           {QStringLiteral("value"), QStringLiteral("7")});
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), qsizetype(1), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue, QStringLiteral("7 units"));
    }

    void malformedConfigurationNeverExecutes()
    {
        QTemporaryDir temporary;
        const QString marker = temporary.filePath(QStringLiteral("executed"));
        const QJsonObject malformed{
            {QStringLiteral("id"), QStringLiteral("bad")},
            {QStringLiteral("title"), QStringLiteral("Bad")},
            {QStringLiteral("source"), QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("command")},
                 {QStringLiteral("executable"), helperPath()},
                 {QStringLiteral("arguments"), QJsonArray{QStringLiteral("touch"), marker}},
                 {QStringLiteral("intervalSeconds"), 0}}}
        };
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeFile(config, QJsonDocument(malformed).toJson());
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QCOMPARE(onlySource(service).state, State::Error);
        QTest::qWait(200);
        QVERIFY(!QFileInfo::exists(marker));
    }
    void unavailableExitStatusMapsToUnavailableAndKeepsLastValue()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeConfiguration(config, helperPath(), {QStringLiteral("value"), QStringLiteral("7")});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);

        writeConfiguration(config, helperPath(), {QStringLiteral("exit"), QString::number(UnavailableExitStatus),
                                                  QStringLiteral("tool is not installed")});
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Unavailable, 4000);
        QCOMPARE(onlySource(service).message, QStringLiteral("tool is not installed"));

        // Any other non-zero status stays Error.
        writeConfiguration(config, helperPath(), {QStringLiteral("exit"), QStringLiteral("70")});
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Error, 4000);
        QCOMPARE(onlySource(service).message, QStringLiteral("Command exited with status 70."));
    }

    void snapshotOutputRunsThroughTheScheduler()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        const QJsonObject object{
            {QStringLiteral("id"), QStringLiteral("multi")},
            {QStringLiteral("title"), QStringLiteral("Multi")},
            {QStringLiteral("source"), QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("command")},
                 {QStringLiteral("executable"), helperPath()},
                 {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), QStringLiteral(
                      R"({"title":"t","metricsBarValue":"3","metrics":[{"title":"A","formattedValue":"1"},{"title":"B","formattedValue":"2"}],"lastUpdatedDate":"2026-10-16T10:00:00Z"})")}},
                 {QStringLiteral("output"), QStringLiteral("snapshot")}}}};
        writeFile(config, QJsonDocument(object).toJson());
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QCOMPARE(onlySource(service).snapshot->title, QStringLiteral("Multi"));
        QCOMPARE(onlySource(service).snapshot->metrics.size(), 2);
        QCOMPARE(*onlySource(service).snapshot->metricsBarValue, QStringLiteral("3"));
    }

    // v2F1.2: "enabled": false — listed, never scheduled or executed, no
    // value; missing "enabled" keeps the v2 behaviour (covered above).
    void disabledCommandNeverRuns()
    {
        QTemporaryDir temporary;
        const QString marker = temporary.filePath(QStringLiteral("executed"));
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeConfiguration(config, helperPath(), {QStringLiteral("touch"), marker});
        QFile written(config);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QJsonObject object = QJsonDocument::fromJson(written.readAll()).object();
        written.close();
        object.insert(QStringLiteral("enabled"), false);
        writeFile(config, QJsonDocument(object).toJson());
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QVERIFY(!onlySource(service).enabled);
        QCOMPARE(onlySource(service).type, SourceType::Command);
        QCOMPARE(onlySource(service).state, State::Unavailable);
        QCOMPARE(onlySource(service).message, QStringLiteral("Disabled."));
        QCOMPARE(onlySource(service).snapshot->title, QStringLiteral("Fixture")); // its name, no values
        QVERIFY(onlySource(service).snapshot->metrics.isEmpty());
        QTest::qWait(1500); // more than one interval
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(!onlySource(service).enabled);
    }

    // Disabled while its command runs: the process (group) is stopped, the
    // late result never comes back, no timer keeps it alive; enabled again,
    // it is scheduled at once.
    void runtimeDisableStopsTheRunAndEnableResumes()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        const QString pidFile = temporary.filePath(QStringLiteral("pid"));
        writeConfiguration(config, helperPath(), {QStringLiteral("pid-sleep"), pidFile, QStringLiteral("700")}, 1, 30);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 3000);
        QFile::remove(pidFile);
        QTRY_VERIFY_WITH_TIMEOUT(readPid(pidFile) > 0, 3000); // the next run is in flight
        const pid_t running = readPid(pidFile);

        QString error;
        QVERIFY2(service.setEnabled(config, false, &error), qPrintable(error));
        QVERIFY(!onlySource(service).enabled); // applied at once, not on the watcher
        QVERIFY(onlySource(service).snapshot->metrics.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(processGone(running), 2000);
        QFile::remove(pidFile);
        QTest::qWait(2000); // its result, and any next run, would have come by now
        QVERIFY(!onlySource(service).enabled);
        QCOMPARE(onlySource(service).state, State::Unavailable);
        QVERIFY(!QFileInfo::exists(pidFile));

        // The file: the flag added, nothing else changed.
        QFile file(config);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject stored = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(stored.value(QStringLiteral("enabled")), QJsonValue(false));
        stored.remove(QStringLiteral("enabled"));
        QCOMPARE(stored.value(QStringLiteral("display")).toObject().value(QStringLiteral("suffix")).toString(), QStringLiteral(" units"));
        file.close();

        QVERIFY2(service.setEnabled(config, true, &error), qPrintable(error));
        QVERIFY(onlySource(service).enabled);
        QTRY_VERIFY_WITH_TIMEOUT(readPid(pidFile) > 0, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 3000);
        QCOMPARE(onlySource(service).snapshot->metrics.first().formattedValue, QStringLiteral("done units"));
        service.stop();
    }

    void setEnabledRefusals()
    {
        QTemporaryDir temporary;
        const QString config = temporary.filePath(QStringLiteral("metric.json"));
        writeConfiguration(config, helperPath(), {QStringLiteral("value"), QStringLiteral("1")});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({config});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("discovery")));
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(onlySource(service).state, State::Available, 2000);
        QString error;
        QVERIFY(!service.setEnabled(temporary.filePath(QStringLiteral("other.json")), false, &error));
        QVERIFY(error.contains(QLatin1String("not a known")));

        if (::geteuid() != 0) {
            // Not writable: refused with the reason, the metric keeps running.
            QVERIFY(QFile::setPermissions(temporary.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
            error.clear();
            const bool written = service.setEnabled(config, false, &error);
            QVERIFY(QFile::setPermissions(temporary.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
            QVERIFY(!written);
            QVERIFY(error.contains(QLatin1String("cannot write")));
            QVERIFY(onlySource(service).enabled);
            QCOMPARE(onlySource(service).state, State::Available);
        }

        // An integration's source is switched in Integrations, not here.
        QFile file(config);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        object.insert(QStringLiteral("integration"), QStringLiteral("claude-code"));
        writeFile(config, QJsonDocument(object).toJson());
        service.rescan();
        QCOMPARE(onlySource(service).integration, QStringLiteral("claude-code"));
        error.clear();
        QVERIFY(!service.setEnabled(config, false, &error));
        QVERIFY(error.contains(QLatin1String("Integrations")));
    }
};

QTEST_GUILESS_MAIN(TestCommandMetricsService)
#include "tst_commandmetricsservice.moc"
