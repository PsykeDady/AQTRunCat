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
#include <QDir>
#include <QFile>
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
