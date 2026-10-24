// F4.1 backend, API and clients, end to end on an isolated D-Bus session
// bus (the test runs under dbus-run-session): the real aqtruncatd and
// aqtruncatctl of the build, the frontend's BackendClient and Dashboard.
// Every XDG, home, Claude Code and Codex directory is temporary.

#include "api/apischema.h"
#include "api/backendclient.h"
#include "app/appsettings.h"
#include "app/dashboardwindow.h"
#include "app/metricsbarmodel.h"
#include "app/snapshotthrottle.h"
#include "runnerfixture.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <tuple>

namespace {

QString daemonPath() { return QStringLiteral(AQTRUNCAT_DAEMON); }
QString ctlPath() { return QStringLiteral(AQTRUNCAT_CTL); }

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

QJsonObject object(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }

QDBusConnectionInterface *busInterface() { return QDBusConnection::sessionBus().interface(); }
bool backendRegistered() { return busInterface()->isServiceRegistered(aqtruncat::api::ServiceName); }
uint backendPid() { return busInterface()->servicePid(aqtruncat::api::ServiceName).value(); }

QDBusMessage call(const QString &method, const QVariantList &arguments = {}, bool autoStart = false)
{
    QDBusMessage message = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                          aqtruncat::api::InterfaceName, method);
    message.setArguments(arguments);
    message.setAutoStartService(autoStart);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, 20000);
}

QJsonObject callObject(const QString &method)
{
    const QDBusMessage reply = call(method);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << method << reply.errorMessage();
    return object(reply.arguments().value(0).toString());
}

QJsonObject metricById(const QJsonArray &metrics, const QString &id)
{
    for (const QJsonValue &value : metrics) {
        if (value.toObject().value(QStringLiteral("id")) == id)
            return value.toObject();
    }
    return {};
}

struct Run {
    int exitCode = -1;
    QByteArray out;
    QByteArray err;
};

Run ctl(const QStringList &arguments)
{
    QProcess process;
    process.start(ctlPath(), arguments);
    process.waitForFinished(30000);
    return {process.exitCode(), process.readAllStandardOutput(), process.readAllStandardError()};
}

} // namespace

// Records the backend's signals.
class SignalLog : public QObject
{
    Q_OBJECT

public:
    QList<quint64> generations;
    QList<QJsonObject> deltas;
    QList<QJsonObject> runners;
    QList<QJsonObject> settings;
    QList<QJsonArray> integrations;

    void subscribe()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        const QString s = aqtruncat::api::ServiceName;
        const QString p = aqtruncat::api::ObjectPath;
        const QString i = aqtruncat::api::InterfaceName;
        QVERIFY(bus.connect(s, p, i, QStringLiteral("MetricsChanged"), this, SLOT(metricsChanged(qulonglong,QString))));
        QVERIFY(bus.connect(s, p, i, QStringLiteral("RunnerChanged"), this, SLOT(runnerChanged(QString))));
        QVERIFY(bus.connect(s, p, i, QStringLiteral("SettingsChanged"), this, SLOT(settingsChanged(QString))));
        QVERIFY(bus.connect(s, p, i, QStringLiteral("IntegrationsChanged"), this, SLOT(integrationsChanged(QString))));
    }

public slots:
    void metricsChanged(qulonglong generation, const QString &changes)
    {
        generations.append(generation);
        deltas.append(object(changes));
    }
    void runnerChanged(const QString &runner) { runners.append(object(runner)); }
    void settingsChanged(const QString &s) { settings.append(object(s)); }
    void integrationsChanged(const QString &json) { integrations.append(QJsonDocument::fromJson(json.toUtf8()).array()); }
};

class TestBackendIpc : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QProcess *m_backend = nullptr;
    QString m_metricsDir;
    QString m_settingsFile;

    void startBackend()
    {
        QVERIFY(!backendRegistered());
        m_backend = new QProcess(this);
        m_backend->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_backend->start(daemonPath(), {});
        QVERIFY(m_backend->waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(backendRegistered(), 10000);
    }

    void stopBackend()
    {
        if (!m_backend)
            return;
        m_backend->terminate();
        QVERIFY(m_backend->waitForFinished(10000));
        QCOMPARE(m_backend->exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_backend->exitCode(), 0);
        delete m_backend;
        m_backend = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(!backendRegistered(), 10000);
    }

    void stopForeignBackend()
    {
        // A backend this test did not start as a QProcess (activation, fallback).
        const uint pid = backendPid();
        QVERIFY(pid > 0);
        QCOMPARE(::kill(pid_t(pid), SIGTERM), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!backendRegistered(), 10000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QVERIFY2(QDBusConnection::sessionBus().isConnected(), "run this test under dbus-run-session");
        const QString home = m_root.filePath(QStringLiteral("home"));
        QDir().mkpath(home);
        qputenv("HOME", home.toUtf8());
        qputenv("XDG_CONFIG_HOME", m_root.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", m_root.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CACHE_HOME", m_root.filePath(QStringLiteral("cache")).toUtf8());
        qputenv("XDG_RUNTIME_DIR", m_root.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("CLAUDE_CONFIG_DIR", m_root.filePath(QStringLiteral("no claude")).toUtf8());
        qputenv("CODEX_HOME", m_root.filePath(QStringLiteral("no codex")).toUtf8());
        QDir().mkpath(m_root.filePath(QStringLiteral("runtime")));
        QFile::setPermissions(m_root.filePath(QStringLiteral("runtime")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        m_metricsDir = m_root.filePath(QStringLiteral("data/AQTRunCat/metrics"));
        m_settingsFile = m_root.filePath(QStringLiteral("config/AQTRunCat/AQTRunCat.ini"));
        QDir().mkpath(m_metricsDir);
        // F5: no real package-manager or Flatpak checks from these tests
        // (tst_updatesservice covers them with fake tools).
        writeFile(m_settingsFile, "[updates]\nintervalMinutes=0\n");
        QVERIFY(!backendRegistered());
    }

    void cleanup()
    {
        stopBackend();
        if (backendRegistered())
            stopForeignBackend();
    }

    void registrationVersionAndSingleInstance()
    {
        startBackend();
        const QJsonObject info = callObject(QStringLiteral("GetInfo"));
        QCOMPARE(info.value(QStringLiteral("api")).toInt(), aqtruncat::api::ApiVersion);
        QCOMPARE(info.value(QStringLiteral("schema")).toInt(), aqtruncat::api::SchemaVersion);
        QCOMPARE(info.value(QStringLiteral("service")).toString(), QStringLiteral("aqtruncatd"));
        QVERIFY(info.value(QStringLiteral("capabilities")).toArray().contains(QStringLiteral("metrics-delta")));
        // Introspection documents the interface for any D-Bus tooling.
        const QDBusMessage introspect = QDBusConnection::sessionBus().call(QDBusMessage::createMethodCall(
            aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath, QStringLiteral("org.freedesktop.DBus.Introspectable"),
            QStringLiteral("Introspect")));
        const QString xml = introspect.arguments().value(0).toString();
        QVERIFY(xml.contains(aqtruncat::api::InterfaceName));
        QVERIFY(xml.contains(QStringLiteral("MetricsChanged")));
        QVERIFY(xml.contains(QStringLiteral("SetSetting")));

        // A second backend leaves at once without sampling anything.
        const uint owner = backendPid();
        QProcess second;
        second.start(daemonPath(), {});
        QVERIFY(second.waitForFinished(10000));
        QCOMPARE(second.exitCode(), 0);
        QVERIFY(second.readAllStandardError().contains("already owns"));
        QCOMPARE(backendPid(), owner);
    }

    void stateDeltasAreConsistent()
    {
        startBackend();
        SignalLog log;
        log.subscribe();
        QTRY_VERIFY_WITH_TIMEOUT(log.generations.size() >= 3, 15000);
        const QJsonObject state = callObject(QStringLiteral("GetState"));
        const QJsonArray metrics = state.value(QStringLiteral("metrics")).toArray();
        QCOMPARE(metricById(metrics, QStringLiteral("cpu")).value(QStringLiteral("category")).toString(), QStringLiteral("builtin"));
        QVERIFY(state.value(QStringLiteral("runner")).toObject().contains(QStringLiteral("fps")));
        QVERIFY(state.value(QStringLiteral("settings")).toObject().contains(QStringLiteral("speedMode")));
        QCOMPARE(state.value(QStringLiteral("integrations")).toArray().size(), 2);
        // Generations are consecutive; a delta only carries what changed.
        for (int i = 1; i < log.generations.size(); ++i)
            QCOMPARE(log.generations.at(i), log.generations.at(i - 1) + 1);
        for (const QJsonObject &delta : std::as_const(log.deltas))
            QVERIFY(delta.value(QStringLiteral("metrics")).toArray().size() <= metrics.size());
        // The time of the system sample travels with state and deltas.
        QVERIFY(state.value(QStringLiteral("sampleTime")).isDouble());
        QVERIFY(log.deltas.last().value(QStringLiteral("sampleTime")).toDouble()
                > log.deltas.first().value(QStringLiteral("sampleTime")).toDouble());

        // Applying the deltas to an old state gives the current state.
        QJsonObject base = callObject(QStringLiteral("GetMetrics"));
        const quint64 from = quint64(base.value(QStringLiteral("generation")).toDouble());
        log.deltas.clear();
        log.generations.clear();
        QTRY_VERIFY_WITH_TIMEOUT(log.generations.size() >= 2, 15000);
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue &value : base.value(QStringLiteral("metrics")).toArray())
            byId.insert(value.toObject().value(QStringLiteral("id")).toString(), value.toObject());
        quint64 at = from;
        for (int i = 0; i < log.deltas.size(); ++i) {
            if (log.generations.at(i) <= at)
                continue;
            QCOMPARE(log.generations.at(i), at + 1);
            for (const QJsonValue &value : log.deltas.at(i).value(QStringLiteral("metrics")).toArray())
                byId.insert(value.toObject().value(QStringLiteral("id")).toString(), value.toObject());
            at = log.generations.at(i);
        }
        const QJsonObject now = callObject(QStringLiteral("GetMetrics"));
        if (quint64(now.value(QStringLiteral("generation")).toDouble()) == at) {
            for (const QJsonValue &value : now.value(QStringLiteral("metrics")).toArray()) {
                const QJsonObject metric = value.toObject();
                QCOMPARE(byId.value(metric.value(QStringLiteral("id")).toString()), metric);
            }
        }
    }

    // v2F1.2 (schema 3): SetCustomMetricEnabled writes the file and every
    // client sees "enabled" in GetMetrics and in MetricsChanged at once.
    void customMetricEnabledThroughTheApi()
    {
        const QString file = m_metricsDir + QStringLiteral("/sensors.json");
        writeFile(file, R"({"title":"Sensors","metricsBarValue":"48°","metrics":[{"title":"T","formattedValue":"48 °C"}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        startBackend();
        SignalLog log;
        log.subscribe();
        const QString id = aqtruncat::metricsbar::customId(QFileInfo(file).canonicalFilePath());
        QJsonObject metric = metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(), id);
        QCOMPARE(metric.value(QStringLiteral("enabled")), QJsonValue(true));
        QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));

        QDBusMessage reply = call(QStringLiteral("SetCustomMetricEnabled"), {file, false});
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        metric = metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(), id);
        QCOMPARE(metric.value(QStringLiteral("enabled")), QJsonValue(false));
        QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
        QVERIFY(metric.value(QStringLiteral("items")).toArray().isEmpty()); // no stale value
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QJsonObject &delta : std::as_const(log.deltas)) {
                for (const QJsonValue &value : delta.value(QStringLiteral("metrics")).toArray()) {
                    if (value.toObject().value(QStringLiteral("id")) == id
                        && value.toObject().value(QStringLiteral("enabled")) == QJsonValue(false))
                        return true;
                }
            }
            return false;
        }(), 5000);
        QFile stored(file);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QVERIFY(stored.readAll().startsWith("{\"enabled\": false,\"title\":\"Sensors\""));
        stored.close();

        QCOMPARE(call(QStringLiteral("SetCustomMetricEnabled"), {m_root.filePath(QStringLiteral("none.json")), true}).errorName(),
                 aqtruncat::api::ErrorNotFound);
        reply = call(QStringLiteral("SetCustomMetricEnabled"), {file, true});
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        metric = metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(), id);
        QCOMPARE(metric.value(QStringLiteral("enabled")), QJsonValue(true));
        QCOMPARE(metric.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QFile::remove(file);
    }

    // v2F1.3: the backend's classification reaches every client as is:
    // exit 1 → "error" with the code and reason, exit 69 → "unavailable",
    // a success clears the message; no value is sent while it fails.
    void commandFailuresThroughTheApi()
    {
        const QString package = m_metricsDir + QStringLiteral("/failing");
        const QString control = package + QStringLiteral("/control");
        writeFile(control, "1\n\nsensor offline\n");
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/custommetric_test_helper");
        writeFile(package + QStringLiteral("/metric.json"),
                  QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("failing")},
                                            {QStringLiteral("title"), QStringLiteral("Failing")},
                                            {QStringLiteral("source"), QJsonObject{
                                                 {QStringLiteral("type"), QStringLiteral("command")},
                                                 {QStringLiteral("executable"), helper},
                                                 {QStringLiteral("arguments"), QJsonArray{QStringLiteral("control"), control}}}}})
                      .toJson());
        startBackend();
        const QString id = aqtruncat::metricsbar::customId(QFileInfo(package + QStringLiteral("/metric.json")).canonicalFilePath());
        auto metric = [&] { return metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(), id); };
        QTRY_COMPARE_WITH_TIMEOUT(metric().value(QStringLiteral("state")).toString(), QStringLiteral("error"), 10000);
        QCOMPARE(metric().value(QStringLiteral("message")).toString(), QStringLiteral("Command exited with status 1. sensor offline"));
        QVERIFY(metric().value(QStringLiteral("items")).toArray().isEmpty());

        writeFile(control, "69\n\nnot running\n");
        QTRY_COMPARE_WITH_TIMEOUT(metric().value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"), 10000);
        QCOMPARE(metric().value(QStringLiteral("message")).toString(), QStringLiteral("not running"));

        writeFile(control, "0\n7\n");
        QTRY_COMPARE_WITH_TIMEOUT(metric().value(QStringLiteral("state")).toString(), QStringLiteral("ok"), 10000);
        QVERIFY(metric().value(QStringLiteral("message")).toString().isEmpty());
        QCOMPARE(metric().value(QStringLiteral("formattedValue")).toString(), QStringLiteral("7"));

        // Failing again after a value: the payload keeps the last snapshot
        // (state decides), the CLI prints only the error, never "7".
        writeFile(control, "2\n\nsensor offline again\n");
        QTRY_COMPARE_WITH_TIMEOUT(metric().value(QStringLiteral("state")).toString(), QStringLiteral("error"), 10000);
        QString line;
        for (const QString &text : QString::fromUtf8(ctl({QStringLiteral("metrics")}).out).split(u'\n')) {
            if (text.contains(QLatin1String("Failing")))
                line = text;
        }
        QVERIFY2(line.contains(QLatin1String("(error: Command exited with status 2. sensor offline again)")), qPrintable(line));
        QVERIFY2(!line.contains(QLatin1String(" 7")), qPrintable(line));
        QDir(package).removeRecursively();
    }

    // v2F1.4: display.showBar reaches the clients; values and normalized
    // data are the same as with a bar.
    void showBarThroughTheApi()
    {
        const QString package = m_metricsDir + QStringLiteral("/dice");
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/custommetric_test_helper");
        writeFile(package + QStringLiteral("/metric.json"),
                  QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("dice")},
                                            {QStringLiteral("title"), QStringLiteral("D20")},
                                            {QStringLiteral("source"), QJsonObject{
                                                 {QStringLiteral("type"), QStringLiteral("command")},
                                                 {QStringLiteral("executable"), helper},
                                                 {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), QStringLiteral("10")}}}},
                                            {QStringLiteral("display"), QJsonObject{
                                                 {QStringLiteral("normalizedRange"), QJsonObject{{QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 20}}},
                                                 {QStringLiteral("showBar"), false}}}})
                      .toJson());
        startBackend();
        const QString id = aqtruncat::metricsbar::customId(QFileInfo(package + QStringLiteral("/metric.json")).canonicalFilePath());
        auto metric = [&] { return metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(), id); };
        QTRY_COMPARE_WITH_TIMEOUT(metric().value(QStringLiteral("state")).toString(), QStringLiteral("ok"), 10000);
        QCOMPARE(metric().value(QStringLiteral("showBar")), QJsonValue(false));
        QCOMPARE(metric().value(QStringLiteral("formattedValue")).toString(), QStringLiteral("10"));
        QCOMPARE(metric().value(QStringLiteral("items")).toArray().first().toObject().value(QStringLiteral("normalized")).toDouble(), 0.5);
        QDir(package).removeRecursively();
    }

    // v2F1.5 (schema 5): the group structure reaches the clients: the
    // container (kind "group", never a value), its children with
    // data.groupId and hierarchical configuredId, each with its own result;
    // the Qt client rebuilds the same tree.
    void groupsThroughTheApi()
    {
        const QString group = m_metricsDir + QStringLiteral("/dice");
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/custommetric_test_helper");
        writeFile(group + QStringLiteral("/metric.json"),
                  QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("dice")}, {QStringLiteral("title"), QStringLiteral("Dice")},
                                            {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                                                   {QStringLiteral("executable"), helper}}}}).toJson());
        for (const int sides : {6, 20})
            writeFile(group + QStringLiteral("/d%1/metric.json").arg(sides),
                      QJsonDocument(QJsonObject{{QStringLiteral("title"), QStringLiteral("D%1").arg(sides)},
                                                {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                                       {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), sides}}}}}).toJson());
        startBackend();
        const QString groupId = QFileInfo(group + QStringLiteral("/metric.json")).canonicalFilePath();
        auto metrics = [&] { return callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(); };
        QTRY_VERIFY_WITH_TIMEOUT(metricById(metrics(), aqtruncat::metricsbar::customId(QFileInfo(group + QStringLiteral("/d20/metric.json")).canonicalFilePath()))
                                     .value(QStringLiteral("state")) == QLatin1String("ok"), 10000);
        const QJsonObject container = metricById(metrics(), aqtruncat::metricsbar::customId(groupId));
        QCOMPARE(container.value(QStringLiteral("kind")).toString(), QStringLiteral("group"));
        QCOMPARE(container.value(QStringLiteral("title")).toString(), QStringLiteral("Dice"));
        QVERIFY(container.value(QStringLiteral("items")).toArray().isEmpty());
        for (const int sides : {6, 20}) {
            const QJsonObject child = metricById(metrics(), aqtruncat::metricsbar::customId(
                QFileInfo(group + QStringLiteral("/d%1/metric.json").arg(sides)).canonicalFilePath()));
            QCOMPARE(child.value(QStringLiteral("data")).toObject().value(QStringLiteral("groupId")).toString(), groupId);
            QCOMPARE(child.value(QStringLiteral("data")).toObject().value(QStringLiteral("configuredId")).toString(),
                     QStringLiteral("dice/d%1").arg(sides));
            QTRY_COMPARE_WITH_TIMEOUT(metricById(metrics(), child.value(QStringLiteral("id")).toString())
                                          .value(QStringLiteral("formattedValue")).toString(), QString::number(sides), 10000);
        }
        QDir(group).removeRecursively();
    }

    // v2F1.6 (schema 6): ReloadCustomMetrics through D-Bus, the Qt client
    // and aqtruncatctl: the report, the new metric set delivered before the
    // reply, simultaneous requests served one after the other without a
    // second run of an unchanged command, and a refused reload (unreadable
    // directory) as a Failed error that leaves every metric as it was.
    void reloadThroughTheApi()
    {
        const QString helper = QCoreApplication::applicationDirPath() + QStringLiteral("/custommetric_test_helper");
        const QString counter = m_root.filePath(QStringLiteral("reload.count"));
        auto commandJson = [&](const QString &id, const QJsonArray &arguments) {
            return QJsonDocument(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("title"), id.toUpper()},
                                             {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                                    {QStringLiteral("executable"), helper},
                                                                                    {QStringLiteral("arguments"), arguments},
                                                                                    {QStringLiteral("intervalSeconds"), 3600}}}}).toJson();
        };
        const QString a = m_metricsDir + QStringLiteral("/reload-a.json");
        writeFile(a, commandJson(QStringLiteral("a"), {QStringLiteral("counter"), counter}));
        startBackend();
        QVERIFY(callObject(QStringLiteral("GetInfo")).value(QStringLiteral("capabilities")).toArray()
                    .contains(QStringLiteral("custom-metrics-reload")));
        const QString aId = aqtruncat::metricsbar::customId(QFileInfo(a).canonicalFilePath());
        auto metrics = [&] { return callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(); };
        QTRY_COMPARE_WITH_TIMEOUT(metricById(metrics(), aId).value(QStringLiteral("formattedValue")).toString(), QStringLiteral("1"), 10000);

        // Nothing changed: a report with everything unchanged.
        QDBusMessage reply = call(QStringLiteral("ReloadCustomMetrics"));
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QJsonObject report = object(reply.arguments().value(0).toString());
        QCOMPARE(report.value(QStringLiteral("unchanged")).toInt(), 1);
        for (const QString &key : {QStringLiteral("added"), QStringLiteral("changed"), QStringLiteral("removed"), QStringLiteral("errors")})
            QVERIFY2(report.value(key).isArray() && report.value(key).toArray().isEmpty(), qPrintable(key));

        // A new metric: in the delta before the reply arrives.
        SignalLog log;
        log.subscribe();
        const QString b = m_metricsDir + QStringLiteral("/reload-b.json");
        writeFile(b, commandJson(QStringLiteral("b"), {QStringLiteral("value"), QStringLiteral("bee")}));
        reply = call(QStringLiteral("ReloadCustomMetrics"));
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        const QString bId = aqtruncat::metricsbar::customId(QFileInfo(b).canonicalFilePath());
        QCoreApplication::processEvents(); // the signal queued before the reply
        const bool delivered = std::any_of(log.deltas.cbegin(), log.deltas.cend(), [&](const QJsonObject &delta) {
            const QJsonArray changed = delta.value(QStringLiteral("metrics")).toArray();
            return std::any_of(changed.cbegin(), changed.cend(), [&](const QJsonValue &v) { return v.toObject().value(QStringLiteral("id")) == bId; });
        });
        QVERIFY(delivered);
        QTRY_COMPARE_WITH_TIMEOUT(metricById(metrics(), bId).value(QStringLiteral("formattedValue")).toString(), QStringLiteral("bee"), 10000);

        // The Qt client: asynchronous, its sources follow.
        BackendClient client(BackendClient::Options{});
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 10000);
        QSignalSpy finished(&client, &CustomMetricsModel::reloadFinished);
        QVERIFY(QFile::remove(b));
        client.reloadConfigurations();
        client.reloadConfigurations(); // shares the pending request
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QTest::qWait(200);
        QCOMPARE(finished.size(), 1);
        aqtruncat::custommetrics::ReloadReport clientReport = finished.first().first().value<aqtruncat::custommetrics::ReloadReport>();
        QVERIFY(clientReport.ok);
        QTRY_VERIFY_WITH_TIMEOUT(std::none_of(client.sources().cbegin(), client.sources().cend(),
                                              [&](const aqtruncat::custommetrics::Source &s) { return s.path == b; }), 10000);
        QVERIFY(metricById(metrics(), bId).isEmpty());

        // Simultaneous requests: each answered, the unchanged command not run again.
        QList<QDBusPendingCall> pending;
        for (int i = 0; i < 4; ++i) {
            const QDBusMessage message = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                                        aqtruncat::api::InterfaceName, QStringLiteral("ReloadCustomMetrics"));
            pending.append(QDBusConnection::sessionBus().asyncCall(message, 20000));
        }
        for (QDBusPendingCall &call : pending) {
            call.waitForFinished();
            QVERIFY2(!call.isError(), qPrintable(call.error().message()));
        }
        QTest::qWait(300);
        QFile count(counter);
        QVERIFY(count.open(QIODevice::ReadOnly));
        QCOMPARE(count.readAll().trimmed(), QByteArray("1"));
        count.close();

        // The command-line client.
        Run r = ctl({QStringLiteral("custom"), QStringLiteral("reload")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.startsWith("Custom Metrics reloaded: 0 added, 0 changed, 0 removed, 1 unchanged."), r.out.constData());
        r = ctl({QStringLiteral("--json"), QStringLiteral("custom"), QStringLiteral("reload")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(QJsonDocument::fromJson(r.out).object().value(QStringLiteral("unchanged")).toInt(), 1);
        QCOMPARE(ctl({QStringLiteral("custom"), QStringLiteral("reload"), QStringLiteral("extra")}).exitCode, 2);

        // Refused: Failed, nothing changed, and the clients are told why.
        const QFile::Permissions permissions = QFile::permissions(m_metricsDir);
        QVERIFY(QFile::setPermissions(m_metricsDir, QFile::WriteOwner));
        reply = call(QStringLiteral("ReloadCustomMetrics"));
        r = ctl({QStringLiteral("custom"), QStringLiteral("reload")});
        finished.clear();
        client.reloadConfigurations();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(QFile::setPermissions(m_metricsDir, permissions));
        QCOMPARE(reply.errorName(), aqtruncat::api::ErrorFailed);
        QVERIFY2(reply.errorMessage().startsWith(QStringLiteral("Cannot read the directory")), qPrintable(reply.errorMessage()));
        QCOMPARE(r.exitCode, 1);
        QVERIFY(r.err.contains("Cannot read the directory"));
        clientReport = finished.first().first().value<aqtruncat::custommetrics::ReloadReport>();
        QVERIFY(!clientReport.ok);
        QVERIFY(aqtruncat::custommetrics::reloadSummary(clientReport).contains(QStringLiteral("Cannot read the directory")));
        QCOMPARE(metricById(metrics(), aId).value(QStringLiteral("formattedValue")).toString(), QStringLiteral("1"));
        QFile::remove(a);
    }

    void customMetricsAndIntegrationsAreClassified()
    {
        writeFile(m_metricsDir + QStringLiteral("/sensors.json"),
                  R"({"title":"Codex","metricsBarValue":"48°","metrics":[{"title":"Temperature","formattedValue":"48 °C","normalizedValue":0.48}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        const QString registered = m_root.filePath(QStringLiteral("elsewhere/fan.json"));
        writeFile(registered, R"({"title":"Fan","metrics":[{"title":"Fan","formattedValue":"0 rpm"}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        writeFile(m_root.filePath(QStringLiteral("elsewhere/broken.json")), "{ not json");
        startBackend();
        SignalLog log;
        log.subscribe();

        QDBusMessage reply = call(QStringLiteral("RegisterCustomMetric"), {registered});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        reply = call(QStringLiteral("RegisterCustomMetric"), {m_root.filePath(QStringLiteral("elsewhere/broken.json"))});
        QCOMPARE(reply.errorName(), aqtruncat::api::ErrorInvalidArgument);
        reply = call(QStringLiteral("UnregisterCustomMetric"), {m_root.filePath(QStringLiteral("elsewhere/none.json"))});
        QCOMPARE(reply.errorName(), aqtruncat::api::ErrorNotFound);

        // Official integration through the API: the adapter writes its package.
        reply = call(QStringLiteral("SetIntegrationEnabled"), {QStringLiteral("codex"), true});
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QCOMPARE(call(QStringLiteral("SetIntegrationEnabled"), {QStringLiteral("bogus"), true}).errorName(),
                 aqtruncat::api::ErrorNotFound);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const QJsonArray metrics = callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray();
            int integrations = 0;
            int custom = 0;
            for (const QJsonValue &value : metrics) {
                const QString category = value.toObject().value(QStringLiteral("category")).toString();
                integrations += category == QLatin1String("integration");
                custom += category == QLatin1String("custom");
            }
            return integrations == 1 && custom == 2;
        }(), 15000);
        const QJsonArray metrics = callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray();
        for (const QJsonValue &value : metrics) {
            const QJsonObject metric = value.toObject();
            if (metric.value(QStringLiteral("category")) == QLatin1String("integration")) {
                QCOMPARE(metric.value(QStringLiteral("integration")).toString(), QStringLiteral("codex"));
                QCOMPARE(metric.value(QStringLiteral("title")).toString(), QStringLiteral("Codex"));
                QVERIFY(metric.value(QStringLiteral("state")).toString() != QLatin1String("ok")); // no Codex here
            }
            if (metric.value(QStringLiteral("title")) == QLatin1String("Codex")
                && metric.value(QStringLiteral("kind")) == QLatin1String("snapshot")) {
                // The user's own file titled "Codex" stays a Custom Metric.
                QCOMPARE(metric.value(QStringLiteral("category")).toString(), QStringLiteral("custom"));
                QCOMPARE(metric.value(QStringLiteral("items")).toArray().first().toObject()
                             .value(QStringLiteral("normalized")).toDouble(), 0.48);
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!log.integrations.isEmpty() && [&] {
            for (const QJsonValue &value : log.integrations.last()) {
                if (value.toObject().value(QStringLiteral("id")) == QLatin1String("codex"))
                    return value.toObject().value(QStringLiteral("enabled")).toBool();
            }
            return false;
        }(), 15000);
        // No path of a provider settings file or adapter output in the API.
        QVERIFY(!QJsonDocument(callObject(QStringLiteral("GetState"))).toJson().contains("settingsFile"));

        QCOMPARE(call(QStringLiteral("SetIntegrationEnabled"), {QStringLiteral("codex"), false}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(call(QStringLiteral("UnregisterCustomMetric"), {registered}).type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(metricById(callObject(QStringLiteral("GetMetrics")).value(QStringLiteral("metrics")).toArray(),
                                            aqtruncat::metricsbar::customId(QFileInfo(registered).canonicalFilePath())).isEmpty(),
                                 15000);
        QFile::remove(m_metricsDir + QStringLiteral("/sensors.json"));
    }

    void settingsAreSynchronisedBothWays()
    {
        startBackend();
        SignalLog log;
        log.subscribe();
        QCOMPARE(call(QStringLiteral("SetSetting"), {QStringLiteral("speedMode"), QStringLiteral("\"slow-down\"")}).type(),
                 QDBusMessage::ReplyMessage);
        QTRY_VERIFY_WITH_TIMEOUT(!log.settings.isEmpty()
                                     && log.settings.last().value(QStringLiteral("speedMode")) == QLatin1String("slow-down"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!log.runners.isEmpty()
                                     && log.runners.last().value(QStringLiteral("speedMode")) == QLatin1String("slow-down"), 10000);
        QCOMPARE(QSettings(m_settingsFile, QSettings::IniFormat).value(QStringLiteral("runner/speedMode")).toString(),
                 QStringLiteral("slow-down"));
        // A bare word is a string; lists and booleans as JSON.
        QCOMPARE(call(QStringLiteral("SetSetting"), {QStringLiteral("metricsBarItems"), QStringLiteral("[\"cpu\",\"memory\"]")}).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(call(QStringLiteral("SetSetting"), {QStringLiteral("metricsBarEnabled"), QStringLiteral("true")}).type(),
                 QDBusMessage::ReplyMessage);
        for (const auto &[key, value] : QList<QPair<QString, QString>>{
                 {QStringLiteral("speedMode"), QStringLiteral("fast")},
                 {QStringLiteral("dashboardRefreshSeconds"), QStringLiteral("3")},
                 {QStringLiteral("metricsBarItems"), QStringLiteral("[\"cpu\",\"cpu\"]")},
                 {QStringLiteral("metricsBarItems"), QStringLiteral("[\"cpu\",\"memory\",\"swap\",\"disk.read\"]")},
                 {QStringLiteral("metricsBarItems"), QStringLiteral("[\"rm -rf\"]")},
                 {QStringLiteral("networkInterfaces"), QStringLiteral("[\"eth0; reboot\"]")},
                 {QStringLiteral("autostart"), QStringLiteral("\"yes\"")},
                 {QStringLiteral("runnerId"), QStringLiteral("\"../evil\"")},
                 {QStringLiteral("customMetricFiles"), QStringLiteral("[]")},
                 {QStringLiteral("nosuch"), QStringLiteral("1")}}) {
            QCOMPARE(call(QStringLiteral("SetSetting"), {key, value}).errorName(), aqtruncat::api::ErrorInvalidArgument);
        }
        QCOMPARE(QSettings(m_settingsFile, QSettings::IniFormat).value(QStringLiteral("runner/speedMode")).toString(),
                 QStringLiteral("slow-down"));

        // Another process writes the file (a frontend's Settings window):
        // the backend notices and tells every client.
        log.settings.clear();
        {
            aqtruncat::AppSettings frontend(QStringLiteral("cat"), m_settingsFile);
            frontend.setDashboardRefreshSeconds(5);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!log.settings.isEmpty()
                                     && log.settings.last().value(QStringLiteral("dashboardRefreshSeconds")).toInt() == 5, 10000);
        call(QStringLiteral("SetSetting"), {QStringLiteral("speedMode"), QStringLiteral("normal")});
        call(QStringLiteral("SetSetting"), {QStringLiteral("dashboardRefreshSeconds"), QStringLiteral("1")});
        call(QStringLiteral("SetSetting"), {QStringLiteral("metricsBarEnabled"), QStringLiteral("false")});
    }

    void frontendClientsShareOneBackend()
    {
        writeFile(m_metricsDir + QStringLiteral("/sensors.json"),
                  R"({"title":"Sensors","metricsBarValue":"48°","metrics":[{"title":"Temperature","formattedValue":"48 °C","normalizedValue":0.48}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        startBackend();
        const uint backend = backendPid();
        {
            BackendClient first(BackendClient::Options{});
            BackendClient second(BackendClient::Options{});
            QSignalSpy firstConnected(&first, &BackendClient::connectionChanged);
            QSignalSpy snapshots(&first, &BackendClient::systemSnapshot);
            DashboardWindow dashboard(qgetenv("HOME").toStdString());
            dashboard.show();
            // Wired as in trayapp: the presentation throttle needs a time
            // base in the rebuilt snapshots, or only the first one (CPU still
            // pending) would ever reach the Dashboard.
            SnapshotThrottle throttle(1);
            connect(&first, &BackendClient::systemSnapshot, &throttle, &SnapshotThrottle::submit);
            connect(&throttle, &SnapshotThrottle::snapshotReady, &dashboard, &DashboardWindow::updateSnapshot);
            connect(&first, &CustomMetricsModel::sourcesChanged, &dashboard, &DashboardWindow::updateCustomMetrics);
            first.start();
            second.start();
            QTRY_VERIFY_WITH_TIMEOUT(first.connected() && second.connected(), 10000);
            QCOMPARE(firstConnected.first().first().toBool(), true);
            // The frontend's own types, from the shared state.
            QTRY_VERIFY_WITH_TIMEOUT(dashboard.findChild<QLabel *>(QStringLiteral("cpuValue"))->text().endsWith(u'%'), 10000);
            QTRY_COMPARE_WITH_TIMEOUT(first.sources().size(), qsizetype(1), 10000);
            QCOMPARE(first.sources().first().snapshot->title, QStringLiteral("Sensors"));
            QCOMPARE(dashboard.findChild<QLabel *>(QStringLiteral("customTitle_0"))->text(), QStringLiteral("Sensors"));
            QVERIFY(!first.discoveryDirectory().isEmpty());
            // Both clients follow the same generations and see the same data.
            QTRY_VERIFY_WITH_TIMEOUT(first.generation() == second.generation() && first.metrics() == second.metrics(), 10000);
            // Registration through the frontend's model goes to the backend.
            // (A file that does not exist yet may be registered, as since M6;
            // an invalid one is refused, with the backend's reason.)
            const QString broken = m_root.filePath(QStringLiteral("elsewhere/broken-too.json"));
            writeFile(broken, "{ not json");
            QString error;
            QVERIFY(!first.registerFile(broken, &error));
            QVERIFY(!error.isEmpty());

            // v2F1.2: disabled through one frontend's model, every client
            // follows; the Dashboard drops the card and gets it back.
            const QString sensors = m_metricsDir + QStringLiteral("/sensors.json");
            QVERIFY2(first.setEnabled(sensors, false, &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(!second.sources().isEmpty() && !second.sources().first().enabled, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!first.sources().first().enabled, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(!dashboard.findChild<QLabel *>(QStringLiteral("customTitle_0")), 10000);
            QVERIFY(!first.setEnabled(m_root.filePath(QStringLiteral("elsewhere/none.json")), true, &error));
            QVERIFY(error.contains(QLatin1String("not a Custom Metrics source")));
            QVERIFY2(first.setEnabled(sensors, true, &error), qPrintable(error));
            QTRY_VERIFY_WITH_TIMEOUT(second.sources().first().enabled, 10000);
            QTRY_VERIFY_WITH_TIMEOUT(dashboard.findChild<QLabel *>(QStringLiteral("customTitle_0")), 10000);
        }
        // Clients gone: the backend keeps serving.
        QTest::qWait(300);
        QVERIFY(backendRegistered());
        QCOMPARE(backendPid(), backend);
        QFile::remove(m_metricsDir + QStringLiteral("/sensors.json"));
    }

    void frontendRecoversFromABackendRestart()
    {
        startBackend();
        BackendClient::Options options;
        options.graceMs = 500;
        options.retryMs = 300;
        BackendClient client(options);
        QSignalSpy connection(&client, &BackendClient::connectionChanged);
        aqtruncat::metrics::SystemMetricsSnapshot last;
        connect(&client, &BackendClient::systemSnapshot, this,
                [&last](const aqtruncat::metrics::SystemMetricsSnapshot &s) { last = s; });
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(last.cpu.status.ok(), 10000);

        stopBackend(); // crash, upgrade, restart: the client is not told beforehand
        QTRY_VERIFY_WITH_TIMEOUT(!client.connected(), 5000);
        // After the grace period the values are reported unavailable, never stale.
        QTRY_VERIFY_WITH_TIMEOUT(last.cpu.status.state == aqtruncat::metrics::MetricState::Unavailable, 5000);
        QVERIFY(QString::fromStdString(last.cpu.status.message).contains(QStringLiteral("not running")));
        QCOMPARE(connection.last().first().toBool(), false);

        startBackend();
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(last.cpu.status.ok(), 10000);
        QCOMPARE(connection.last().first().toBool(), true);
    }

    void frontendStartsTheBackendWhenActivationIsUnavailable()
    {
        QVERIFY(!backendRegistered());
        BackendClient::Options options;
        options.backendExecutable = daemonPath();
        BackendClient client(options);
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.connected(), 15000);
        QVERIFY(backendRegistered());
        // Another client at the same time does not create a second backend.
        BackendClient other(options);
        other.start();
        QTRY_VERIFY_WITH_TIMEOUT(other.connected(), 15000);
        const uint backend = backendPid();
        QTest::qWait(500);
        QCOMPARE(backendPid(), backend);
        stopForeignBackend();
    }

    void commandLineClient()
    {
        // -v / --version need no backend.
        for (const QString &flag : {QStringLiteral("-v"), QStringLiteral("--version")}) {
            const Run version = ctl({flag});
            QCOMPARE(version.exitCode, 0);
            QVERIFY(version.out.startsWith("aqtruncatctl " AQTRUNCAT_TEST_VERSION " (API 1)"));
        }
        QVERIFY(!backendRegistered());
        // Not running and not allowed to start: unavailable (3).
        Run r = ctl({QStringLiteral("--no-start"), QStringLiteral("status")});
        QCOMPARE(r.exitCode, 3);
        QVERIFY(r.err.contains("not running"));
        // Otherwise the CLI brings the backend up (no activation file here).
        r = ctl({QStringLiteral("status")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.out.contains("Backend:       running (aqtruncatd"));
        QVERIFY(r.out.contains("Integrations:  Claude Code: off"));
        QVERIFY(backendRegistered());
        QTest::qWait(1500); // a sample with values

        r = ctl({QStringLiteral("metrics")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.out.startsWith("Built-in metrics\n"));
        QVERIFY(r.out.contains("  cpu "));
        r = ctl({QStringLiteral("metrics"), QStringLiteral("--json"), QStringLiteral("cpu"), QStringLiteral("memory")});
        QCOMPARE(r.exitCode, 0);
        const QJsonArray two = QJsonDocument::fromJson(r.out).array();
        QCOMPARE(two.size(), 2);
        QCOMPARE(two.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("cpu"));
        r = ctl({QStringLiteral("metrics"), QStringLiteral("--category"), QStringLiteral("custom"), QStringLiteral("--json")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(QJsonDocument::fromJson(r.out).isArray());
        QCOMPARE(ctl({QStringLiteral("metrics"), QStringLiteral("nosuch")}).exitCode, 4);

        r = ctl({QStringLiteral("settings"), QStringLiteral("--json")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(QJsonDocument::fromJson(r.out).object().value(QStringLiteral("speedMode")).toString(), QStringLiteral("normal"));
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("speedMode"), QStringLiteral("slow-down")}).exitCode, 0);
        QCOMPARE(QJsonDocument::fromJson(ctl({QStringLiteral("settings"), QStringLiteral("--json")}).out).object()
                     .value(QStringLiteral("speedMode")).toString(), QStringLiteral("slow-down"));
        r = ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("speedMode"), QStringLiteral("fast")});
        QCOMPARE(r.exitCode, 4);
        QVERIFY(r.err.contains("speedMode must be"));
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("speedMode"), QStringLiteral("normal")}).exitCode, 0);

        r = ctl({QStringLiteral("integrations"), QStringLiteral("--json")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(QJsonDocument::fromJson(r.out).array().size(), 2);
        QCOMPARE(ctl({QStringLiteral("integration"), QStringLiteral("enable"), QStringLiteral("nosuch")}).exitCode, 4);

        r = ctl({QStringLiteral("status"), QStringLiteral("--json")});
        QCOMPARE(QJsonDocument::fromJson(r.out).object().value(QStringLiteral("info")).toObject().value(QStringLiteral("api")).toInt(), 1);

        for (const QStringList &bad : {QStringList{}, QStringList{QStringLiteral("bogus")},
                                       QStringList{QStringLiteral("--frobnicate"), QStringLiteral("status")},
                                       QStringList{QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("x")},
                                       QStringList{QStringLiteral("--category"), QStringLiteral("nope"), QStringLiteral("metrics")}})
            QCOMPARE(ctl(bad).exitCode, 2);
        QVERIFY(ctl({QStringLiteral("--help")}).out.contains("Exit status"));
        QVERIFY(ctl({QStringLiteral("--version")}).out.contains("(API 1)"));

        // watch: one line per update; JSON lines are MetricsChanged objects.
        QProcess watch;
        watch.start(ctlPath(), {QStringLiteral("--json"), QStringLiteral("watch")});
        QTRY_VERIFY_WITH_TIMEOUT(watch.canReadLine(), 10000);
        const QJsonObject line = QJsonDocument::fromJson(watch.readLine()).object();
        QVERIFY(line.value(QStringLiteral("generation")).toDouble() > 0);
        QVERIFY(line.contains(QStringLiteral("metrics")));
        ::kill(pid_t(watch.processId()), SIGINT);
        QVERIFY(watch.waitForFinished(5000));
        QCOMPARE(watch.exitCode(), 0);
        stopForeignBackend();
    }

    // F4.2: the runner catalogue for frontends that cannot load runners
    // themselves (the Plasmoid): files, playback order, colour mode and the
    // same fallback as the Qt frontend for an unusable stored runner.
    void builtInMetricsOfF5()
    {
        startBackend();
        QTest::qWait(1500); // a sample with values
        Run r = ctl({QStringLiteral("--json"), QStringLiteral("metrics"), QStringLiteral("uptime"), QStringLiteral("fps"),
                     QStringLiteral("updates.packages"), QStringLiteral("updates.flatpak"), QStringLiteral("kernel"),
                     QStringLiteral("distribution")});
        QCOMPARE(r.exitCode, 0);
        const QJsonArray metrics = QJsonDocument::fromJson(r.out).array();
        QCOMPARE(metrics.size(), 6);
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue &value : metrics) {
            const QJsonObject metric = value.toObject();
            QCOMPARE(metric.value(QStringLiteral("category")).toString(), QStringLiteral("builtin"));
            byId.insert(metric.value(QStringLiteral("id")).toString(), metric);
        }
        QCOMPARE(byId.value(QStringLiteral("uptime")).value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QVERIFY(byId.value(QStringLiteral("uptime")).value(QStringLiteral("value")).toDouble() > 0);
        QCOMPARE(byId.value(QStringLiteral("kernel")).value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
        QVERIFY(byId.value(QStringLiteral("kernel")).value(QStringLiteral("value")).isNull());
        // The isolated HOME has no MangoHud configuration.
        const QString fpsCondition = byId.value(QStringLiteral("fps")).value(QStringLiteral("data")).toObject()
                                         .value(QStringLiteral("condition")).toString();
        QVERIFY2(fpsCondition == QLatin1String("missing") || fpsCondition == QLatin1String("not-configured"), qPrintable(fpsCondition));
        for (const char *id : {"updates.packages", "updates.flatpak"}) {
            const QJsonObject updates = byId.value(QLatin1String(id));
            QCOMPARE(updates.value(QStringLiteral("state")).toString(), QStringLiteral("unavailable"));
            QCOMPARE(updates.value(QStringLiteral("message")).toString(), QStringLiteral("update checks are turned off"));
            QVERIFY(updates.value(QStringLiteral("value")).isNull());
        }
        // Every GPU is its own metric, with a stable id.
        r = ctl({QStringLiteral("--json"), QStringLiteral("metrics")});
        for (const QJsonValue &value : QJsonDocument::fromJson(r.out).array()) {
            const QString id = value.toObject().value(QStringLiteral("id")).toString();
            if (id.startsWith(QLatin1String("gpu:")))
                QCOMPARE(value.toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("device")).toString(), id.mid(4));
        }
        QCOMPARE(QJsonDocument::fromJson(ctl({QStringLiteral("--json"), QStringLiteral("status")}).out).object()
                     .value(QStringLiteral("info")).toObject().value(QStringLiteral("schema")).toInt(), aqtruncat::api::SchemaVersion);

        // The F5 settings through the API, validated.
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("fpsSource"), QStringLiteral("off")}).exitCode, 0);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("gpuDevice"), QStringLiteral("0000:03:00.0")}).exitCode, 0);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("updatesRemoteRefresh"), QStringLiteral("true")}).exitCode, 0);
        r = ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("updatesIntervalMinutes"), QStringLiteral("7")});
        QCOMPARE(r.exitCode, 4);
        QVERIFY(r.err.contains("updatesIntervalMinutes must be one of"));
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("gpuDevice"), QStringLiteral("../x")}).exitCode, 4);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("fpsSource"), QStringLiteral("inject")}).exitCode, 4);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("updatesRemoteRefresh"), QStringLiteral("yes")}).exitCode, 4);
        const QJsonObject settings = QJsonDocument::fromJson(ctl({QStringLiteral("settings"), QStringLiteral("--json")}).out).object();
        QCOMPARE(settings.value(QStringLiteral("fpsSource")).toString(), QStringLiteral("off"));
        QCOMPARE(settings.value(QStringLiteral("gpuDevice")).toString(), QStringLiteral("0000:03:00.0"));
        QCOMPARE(settings.value(QStringLiteral("updatesIntervalMinutes")).toInt(), 0);
        QVERIFY(settings.value(QStringLiteral("updatesRemoteRefresh")).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(QJsonDocument::fromJson(ctl({QStringLiteral("--json"), QStringLiteral("metrics"), QStringLiteral("fps")}).out)
                                     .array().at(0).toObject().value(QStringLiteral("data")).toObject()
                                     .value(QStringLiteral("condition")).toString() == QLatin1String("disabled"),
                                 5000);
        // A source change is published at once, not with the next sample
        // (frontends hide FPS on "disabled"); the API keeps the metric.
        const auto fpsConditionNow = [] {
            const QJsonArray fps = QJsonDocument::fromJson(ctl({QStringLiteral("--json"), QStringLiteral("metrics"), QStringLiteral("fps")}).out).array();
            return fps.size() == 1 ? fps.at(0).toObject().value(QStringLiteral("data")).toObject().value(QStringLiteral("condition")).toString()
                                   : QStringLiteral("<no fps metric>");
        };
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("fpsSource"), QStringLiteral("mangohud")}).exitCode, 0);
        const QString on = fpsConditionNow();
        QVERIFY2(on == QLatin1String("missing") || on == QLatin1String("not-configured"), qPrintable(on));
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("fpsSource"), QStringLiteral("off")}).exitCode, 0);
        QCOMPARE(fpsConditionNow(), QStringLiteral("disabled"));
        // Back to the defaults the other cases expect.
        ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("fpsSource"), QStringLiteral("mangohud")});
        ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("gpuDevice"), QStringLiteral("auto")});
        ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("updatesRemoteRefresh"), QStringLiteral("false")});
    }

    void runnerCatalogForOtherFrontends()
    {
        const QDir runners(m_root.filePath(QStringLiteral("data/AQTRunCat/runners")));
        fixture::writeFrames(QDir(runners.filePath(QStringLiteral("mine"))),
                             fixture::galleryNames(QStringLiteral("mine"), 3), {40, 36}, Qt::red);
        QVERIFY(fixture::writeFile(runners.filePath(QStringLiteral("mine/metadata.json")),
                                   R"({"displayName": "Mine", "frameOrder": [2, 0, 1, 0]})"));
        QDir().mkpath(runners.filePath(QStringLiteral("broken")));
        QVERIFY(fixture::writeFile(runners.filePath(QStringLiteral("broken/frame-0.png")), "not a png"));

        startBackend();
        QVERIFY(callObject(QStringLiteral("GetInfo")).value(QStringLiteral("capabilities")).toArray()
                    .contains(QStringLiteral("runners")));
        QJsonObject catalog = callObject(QStringLiteral("GetRunners"));
        QCOMPARE(catalog.value(QStringLiteral("schema")).toInt(), aqtruncat::api::SchemaVersion);
        QCOMPARE(catalog.value(QStringLiteral("defaultRunnerId")).toString(), QStringLiteral("cat"));
        QCOMPARE(catalog.value(QStringLiteral("selected")).toString(), QStringLiteral("cat"));
        QHash<QString, QJsonObject> byId;
        for (const QJsonValue &value : catalog.value(QStringLiteral("runners")).toArray())
            byId.insert(value[QStringLiteral("id")].toString(), value.toObject());
        QVERIFY(byId.contains(QStringLiteral("cat")) && byId.contains(QStringLiteral("coffee")));
        // F7 and v2F1: the new built-in runners, listed like Cat and Coffee.
        for (const auto &[id, name, frames, width] : {std::tuple{QStringLiteral("walkinggoose"), QStringLiteral("Walking Goose"), 6, 24},
                                                      std::tuple{QStringLiteral("partyparrot"), QStringLiteral("Party Parrot"), 10, 50},
                                                      std::tuple{QStringLiteral("engine"), QStringLiteral("Engine"), 10, 83},
                                                      std::tuple{QStringLiteral("rickroll"), QStringLiteral("Rickroll"), 31, 33}}) {
            const QJsonObject runner = byId.value(id);
            QCOMPARE(runner.value(QStringLiteral("source")).toString(), QStringLiteral("bundled"));
            QCOMPARE(runner.value(QStringLiteral("displayName")).toString(), name);
            QVERIFY(runner.value(QStringLiteral("valid")).toBool());
            QVERIFY(runner.value(QStringLiteral("monochrome")).toBool());
            QCOMPARE(runner.value(QStringLiteral("frameWidth")).toInt(), width);
            QCOMPARE(runner.value(QStringLiteral("frameHeight")).toInt(), 36);
            const QJsonArray files = runner.value(QStringLiteral("frames")).toArray();
            QCOMPARE(files.size(), frames);
            QJsonArray sequence;
            for (int i = 0; i < frames; ++i) {
                sequence.append(i);
                QVERIFY(QFileInfo::exists(files.at(i).toString()));
                QVERIFY(files.at(i).toString().contains(QStringLiteral("/share/aqtruncat/runners/%1/").arg(id)));
            }
            QCOMPARE(runner.value(QStringLiteral("sequence")).toArray(), sequence);
        }

        const QJsonObject cat = byId.value(QStringLiteral("cat"));
        QCOMPARE(cat.value(QStringLiteral("source")).toString(), QStringLiteral("bundled"));
        QVERIFY(cat.value(QStringLiteral("valid")).toBool());
        QVERIFY(cat.value(QStringLiteral("monochrome")).toBool());
        QCOMPARE(cat.value(QStringLiteral("frameHeight")).toInt(), 36);
        const QJsonArray catFrames = cat.value(QStringLiteral("frames")).toArray();
        QCOMPARE(catFrames.size(), 5);
        for (int i = 0; i < catFrames.size(); ++i) {
            const QString frame = catFrames.at(i).toString();
            QVERIFY2(QFileInfo(frame).isAbsolute() && QFileInfo::exists(frame), qPrintable(frame));
            QVERIFY(frame.endsWith(QStringLiteral("/share/aqtruncat/runners/cat/cat-frame-%1.png").arg(i)));
        }
        QCOMPARE(cat.value(QStringLiteral("sequence")).toArray(), (QJsonArray{0, 1, 2, 3, 4}));

        const QJsonObject mine = byId.value(QStringLiteral("mine"));
        QCOMPARE(mine.value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QCOMPARE(mine.value(QStringLiteral("displayName")).toString(), QStringLiteral("Mine"));
        QVERIFY(mine.value(QStringLiteral("valid")).toBool());
        QVERIFY(!mine.value(QStringLiteral("monochrome")).toBool()); // red frames: a colour runner
        QCOMPARE(mine.value(QStringLiteral("frameWidth")).toInt(), 40);
        QCOMPARE(mine.value(QStringLiteral("sequence")).toArray(), (QJsonArray{2, 0, 1, 0}));
        QVERIFY(mine.value(QStringLiteral("frames")).toArray().at(2).toString()
                    .startsWith(runners.filePath(QStringLiteral("mine/"))));

        const QJsonObject broken = byId.value(QStringLiteral("broken"));
        QVERIFY(!broken.value(QStringLiteral("valid")).toBool());
        QVERIFY(!broken.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(broken.value(QStringLiteral("frames")).toArray().isEmpty());

        // The stored runner decides "selected"; an unusable one falls back.
        SignalLog log;
        log.subscribe();
        QCOMPARE(call(QStringLiteral("SetSetting"), {QStringLiteral("runnerId"), QStringLiteral("\"mine\"")}).type(),
                 QDBusMessage::ReplyMessage);
        QTRY_VERIFY(!log.runners.isEmpty() && log.runners.last().value(QStringLiteral("runnerId")) == QLatin1String("mine"));
        QCOMPARE(callObject(QStringLiteral("GetRunners")).value(QStringLiteral("selected")).toString(), QStringLiteral("mine"));
        for (const QString &unusable : {QStringLiteral("broken"), QStringLiteral("gone")}) {
            call(QStringLiteral("SetSetting"), {QStringLiteral("runnerId"), QStringLiteral("\"%1\"").arg(unusable)});
            catalog = callObject(QStringLiteral("GetRunners"));
            QCOMPARE(catalog.value(QStringLiteral("requested")).toString(), unusable);
            QCOMPARE(catalog.value(QStringLiteral("selected")).toString(), QStringLiteral("cat"));
        }
        call(QStringLiteral("SetSetting"), {QStringLiteral("runnerId"), QStringLiteral("\"cat\"")});
        QDir(runners.path()).removeRecursively();
    }

    void activationThroughTheBus()
    {
        // A private bus whose service directory holds the activation file
        // the installer ships, pointing at the build's backend.
        QTemporaryDir bus;
        writeFile(bus.filePath(QStringLiteral("services/io.github.psykedady.AQTRunCat.service")),
                  QStringLiteral("[D-BUS Service]\nName=io.github.psykedady.AQTRunCat\nExec=%1\n").arg(daemonPath()).toUtf8());
        writeFile(bus.filePath(QStringLiteral("session.conf")), QStringLiteral(
            "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
            " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
            "<busconfig><type>session</type><listen>unix:dir=%1</listen>\n"
            "<servicedir>%2</servicedir>\n"
            "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
            "<allow own=\"*\"/></policy></busconfig>\n").arg(bus.path(), bus.filePath(QStringLiteral("services"))).toUtf8());
        QProcess daemon;
        daemon.start(QStringLiteral("dbus-daemon"), {QStringLiteral("--config-file=") + bus.filePath(QStringLiteral("session.conf")),
                                                     QStringLiteral("--print-address"), QStringLiteral("--nofork")});
        if (!daemon.waitForStarted(5000))
            QSKIP("dbus-daemon is not available");
        QTRY_VERIFY_WITH_TIMEOUT(daemon.canReadLine(), 5000);
        const QString address = QString::fromUtf8(daemon.readLine()).trimmed();
        {
            QDBusConnection connection = QDBusConnection::connectToBus(address, QStringLiteral("activation-test"));
            QVERIFY(connection.isConnected());
            QVERIFY(!connection.interface()->isServiceRegistered(aqtruncat::api::ServiceName));
            QDBusMessage message = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                                  aqtruncat::api::InterfaceName, QStringLiteral("GetInfo"));
            const QDBusMessage reply = connection.call(message, QDBus::Block, 20000); // auto-start
            QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
            QCOMPARE(object(reply.arguments().value(0).toString()).value(QStringLiteral("api")).toInt(), 1);
            const uint pid = connection.interface()->servicePid(aqtruncat::api::ServiceName).value();
            QVERIFY(pid > 0);
            ::kill(pid_t(pid), SIGTERM);
            QTRY_VERIFY_WITH_TIMEOUT(!connection.interface()->isServiceRegistered(aqtruncat::api::ServiceName), 10000);
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("activation-test"));
        daemon.terminate();
        QVERIFY(daemon.waitForFinished(5000));
    }

    void cleanupTestCase()
    {
        stopBackend();
        if (backendRegistered())
            stopForeignBackend();
    }
};

QTEST_MAIN(TestBackendIpc)
#include "tst_backendipc.moc"
