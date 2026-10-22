// F4.2: the Plasmoid's D-Bus client (plasmoid/package/contents/ui/
// BackendClient.qml) against the real aqtruncatd of the build, on a private
// session bus (dbus-run-session) whose only service file activates that
// backend through a wrapper the test can switch off. HOME and every XDG
// directory point into the build tree (CMake sets them for the bus, so the
// activated backend inherits them); no Plasma shell is involved.

#include "api/apischema.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QSignalSpy>
#include <QTest>

#include <csignal>
#include <memory>
#include <utility>

namespace {

QString packageDir() { return QStringLiteral(AQTRUNCAT_PLASMOID_PACKAGE); }
QString daemonPath() { return QStringLiteral(AQTRUNCAT_DAEMON); }
QString ctlPath() { return QStringLiteral(AQTRUNCAT_CTL); }
QString testHome() { return QStringLiteral(AQTRUNCAT_TEST_HOME); }
QString activationFlag() { return testHome() + QStringLiteral("/activation-disabled"); }

QDBusConnectionInterface *busInterface() { return QDBusConnection::sessionBus().interface(); }
bool backendRegistered() { return busInterface()->isServiceRegistered(aqtruncat::api::ServiceName); }
uint backendPid() { return busInterface()->servicePid(aqtruncat::api::ServiceName).value(); }

QJsonObject backendCall(const QString &method)
{
    QDBusMessage message = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                          aqtruncat::api::InterfaceName, method);
    message.setAutoStartService(false);
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 10000);
    return QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
}

// A QML "var" property as JSON.
QJsonValue jsonProperty(QObject *object, const char *name)
{
    QVariant value = object->property(name);
    if (value.canConvert<QJSValue>())
        value = value.value<QJSValue>().toVariant();
    return QJsonValue::fromVariant(value);
}

QString status(QObject *client) { return client->property("status").toString(); }

// Backends of this test: the build's aqtruncatd with the test's HOME (other
// suites may run their own at the same time).
int runningBackends()
{
    int count = 0;
    const QByteArray home = "HOME=" + testHome().toUtf8();
    for (const QString &entry : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
        if (QFileInfo(QStringLiteral("/proc/%1/exe").arg(entry)).symLinkTarget() != QFileInfo(daemonPath()).canonicalFilePath())
            continue;
        QFile environ(QStringLiteral("/proc/%1/environ").arg(entry));
        if (environ.open(QIODevice::ReadOnly) && environ.readAll().split('\0').contains(home))
            ++count;
    }
    return count;
}

int ctl(const QStringList &arguments)
{
    QProcess process;
    process.start(ctlPath(), QStringList{QStringLiteral("--no-start")} + arguments);
    process.waitForFinished(20000);
    return process.exitCode();
}

} // namespace

// Stands in for the backend: an older one (no runner catalogue) or one of
// another major API version.
class FakeBackend : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.psykedady.AQTRunCat1")

public:
    QJsonObject state;
    int getStateCalls = 0;
    int getRunnersCalls = 0;

public slots:
    Q_SCRIPTABLE QString GetState()
    {
        ++getStateCalls;
        return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
    }
    Q_SCRIPTABLE QString GetRunners()
    {
        ++getRunnersCalls;
        return QStringLiteral("{}");
    }
};

class TestPlasmoidClient : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;
    std::unique_ptr<QQmlComponent> m_component;
    QString m_skip;
    QStringList m_warnings; // QML warnings from the package's files

    std::unique_ptr<QObject> createClient()
    {
        std::unique_ptr<QObject> client(m_component->createWithInitialProperties(
            {{QStringLiteral("graceMs"), 800}, {QStringLiteral("retryMinMs"), 300}, {QStringLiteral("retryMaxMs"), 1000}}));
        if (!client)
            qWarning() << m_component->errorString();
        return client;
    }

    void stopBackend()
    {
        if (!backendRegistered())
            return;
        const uint pid = backendPid();
        QVERIFY(pid > 0);
        QCOMPARE(::kill(pid_t(pid), SIGTERM), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!backendRegistered(), 10000);
    }

    void setActivation(bool enabled)
    {
        if (enabled) {
            QFile::remove(activationFlag());
        } else {
            QFile flag(activationFlag());
            QVERIFY(flag.open(QIODevice::WriteOnly));
        }
    }

    // The client's metrics equal the backend's at the same generation.
    bool matchesBackend(QObject *client)
    {
        const QJsonObject backend = backendCall(QStringLiteral("GetMetrics"));
        return backend.value(QStringLiteral("generation")).toDouble() == client->property("generation").toDouble()
            && backend.value(QStringLiteral("metrics")).toArray() == jsonProperty(client, "metrics").toArray();
    }

private slots:
    void initTestCase()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(), "run this test under dbus-run-session");
        QVERIFY2(qEnvironmentVariable("HOME") == testHome(), "HOME must be the test home (set by CMake)");
        QDir(testHome()).removeRecursively();
        for (const char *dir : {"config", "data", "cache", "state", "runtime"})
            QDir().mkpath(testHome() + u'/' + QLatin1String(dir));
        QFile::setPermissions(testHome() + QStringLiteral("/runtime"),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        // F5: no real package-manager or Flatpak checks from this test.
        QDir().mkpath(testHome() + QStringLiteral("/config/AQTRunCat"));
        QFile ini(testHome() + QStringLiteral("/config/AQTRunCat/AQTRunCat.ini"));
        QVERIFY(ini.open(QIODevice::WriteOnly));
        ini.write("[updates]\nintervalMinutes=0\n");
        ini.close();
        QVERIFY(!backendRegistered());

        connect(&m_engine, &QQmlEngine::warnings, this, [this](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                if (warning.url().toLocalFile().startsWith(packageDir()))
                    m_warnings.append(warning.toString());
            }
        });
        m_component = std::make_unique<QQmlComponent>(
            &m_engine, QUrl::fromLocalFile(packageDir() + QStringLiteral("/contents/ui/BackendClient.qml")));
        if (m_component->isError()) {
            m_skip = m_component->errorString();
            if (!m_skip.contains(QLatin1String("org.kde.plasma.workspace.dbus")))
                QFAIL(qPrintable(m_skip));
        }
    }

    void init()
    {
        if (!m_skip.isEmpty())
            QSKIP(qPrintable(QStringLiteral("Plasma's D-Bus QML module is not installed: ") + m_skip));
        setActivation(true);
    }

    void cleanup()
    {
        const QStringList warnings = std::exchange(m_warnings, {});
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(u'\n')));
    }

    void activatesTheBackendOnDemand()
    {
        QVERIFY(!backendRegistered());
        auto first = createClient();
        QVERIFY(first);
        QTRY_COMPARE_WITH_TIMEOUT(status(first.get()), QStringLiteral("connected"), 15000);
        QVERIFY(backendRegistered());
        const uint pid = backendPid();
        QCOMPARE(jsonProperty(first.get(), "info").toObject().value(QStringLiteral("api")).toInt(), 1);

        // More widgets: the same backend, no second one.
        auto second = createClient();
        auto third = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(second.get()), QStringLiteral("connected"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(status(third.get()), QStringLiteral("connected"), 10000);
        QCOMPARE(backendPid(), pid);
        QCOMPARE(runningBackends(), 1);
        // A widget going away does not stop the backend.
        first.reset();
        second.reset();
        QTest::qWait(500);
        QCOMPARE(backendPid(), pid);
    }

    void classifiesMetricsAndListsRunners()
    {
        auto client = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 15000);
        QVERIFY(client->property("hasRunnerCatalog").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(jsonProperty(client.get(), "runnerCatalog").isObject(), 5000);
        const QJsonObject catalog = jsonProperty(client.get(), "runnerCatalog").toObject();
        QCOMPARE(catalog.value(QStringLiteral("selected")).toString(), QStringLiteral("cat"));

        // A Custom Metrics file registered from the widget: category custom.
        const QString file = testHome() + QStringLiteral("/producer/weather.json");
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile snapshot(file);
        QVERIFY(snapshot.open(QIODevice::WriteOnly));
        snapshot.write(R"({"title": "Outside", "metrics": [{"title": "Temperature", "formattedValue": "14 °C"}],
                          "lastUpdatedDate": "2026-10-21T08:00:00Z"})");
        snapshot.close();
        QSignalSpy replied(client.get(), SIGNAL(replied(QString,bool,QString)));
        QVERIFY(QMetaObject::invokeMethod(client.get(), "registerCustomMetric", Q_ARG(QVariant, file)));
        QTRY_COMPARE_WITH_TIMEOUT(replied.size(), 1, 5000);
        QVERIFY(replied.at(0).at(1).toBool());
        const QString customId = QStringLiteral("custom:") + QFileInfo(file).canonicalFilePath();
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QJsonValue &m : jsonProperty(client.get(), "metrics").toArray()) {
                if (m[QStringLiteral("id")] == customId)
                    return m[QStringLiteral("category")] == QLatin1String("custom") && m[QStringLiteral("state")] == QLatin1String("ok");
            }
            return false;
        }(), 10000);
        const QJsonArray metrics = jsonProperty(client.get(), "metrics").toArray();
        for (const QJsonValue &m : metrics) {
            const QString id = m[QStringLiteral("id")].toString();
            if (!id.startsWith(QLatin1String("custom:")))
                QCOMPARE(m[QStringLiteral("category")].toString(), QStringLiteral("builtin"));
        }
        QVERIFY(QMetaObject::invokeMethod(client.get(), "unregisterCustomMetric", Q_ARG(QVariant, file)));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const QJsonValue &m : jsonProperty(client.get(), "metrics").toArray()) {
                if (m[QStringLiteral("id")] == customId)
                    return false;
            }
            return true;
        }(), 10000);
    }

    void followsDeltasLikeTheBackend()
    {
        auto client = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 15000);
        const double start = client->property("generation").toDouble();
        QTRY_VERIFY_WITH_TIMEOUT(client->property("generation").toDouble() >= start + 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(matchesBackend(client.get()), 10000);
    }

    void skipsDeltasWhileHidden()
    {
        auto client = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 15000);
        client->setProperty("wantMetrics", false);
        const QJsonArray before = jsonProperty(client.get(), "metrics").toArray();
        const double generation = client->property("generation").toDouble();
        QTRY_VERIFY_WITH_TIMEOUT(client->property("generation").toDouble() >= generation + 2, 10000);
        QCOMPARE(jsonProperty(client.get(), "metrics").toArray(), before); // nothing parsed
        client->setProperty("wantMetrics", true);
        QTRY_VERIFY_WITH_TIMEOUT(matchesBackend(client.get()), 10000);
    }

    void settingsStayInSync()
    {
        auto widget = createClient();
        auto other = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(widget.get()), QStringLiteral("connected"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(status(other.get()), QStringLiteral("connected"), 15000);
        QSignalSpy replied(widget.get(), SIGNAL(replied(QString,bool,QString)));

        // Widget → backend → every client and the shared INI file.
        QVERIFY(QMetaObject::invokeMethod(widget.get(), "setSetting", Q_ARG(QVariant, QStringLiteral("speedMode")),
                                          Q_ARG(QVariant, QStringLiteral("slow-down"))));
        QTRY_COMPARE_WITH_TIMEOUT(jsonProperty(other.get(), "settings")[QStringLiteral("speedMode")].toString(),
                                  QStringLiteral("slow-down"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(jsonProperty(widget.get(), "runner")[QStringLiteral("speedMode")].toString(),
                                  QStringLiteral("slow-down"), 5000);
        QFile ini(testHome() + QStringLiteral("/config/AQTRunCat/AQTRunCat.ini"));
        QVERIFY(ini.open(QIODevice::ReadOnly));
        QVERIFY(ini.readAll().contains("speedMode=slow-down"));

        // CLI (any other writer) → widget, including the runner it shows.
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("speedMode"), QStringLiteral("normal")}), 0);
        QTRY_COMPARE_WITH_TIMEOUT(jsonProperty(widget.get(), "settings")[QStringLiteral("speedMode")].toString(),
                                  QStringLiteral("normal"), 5000);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("runnerId"), QStringLiteral("coffee")}), 0);
        QTRY_COMPARE_WITH_TIMEOUT(jsonProperty(widget.get(), "runnerCatalog")[QStringLiteral("selected")].toString(),
                                  QStringLiteral("coffee"), 5000);
        QCOMPARE(jsonProperty(widget.get(), "runner")[QStringLiteral("runnerId")].toString(), QStringLiteral("coffee"));

        // Rejected writes come back as errors, nothing changes.
        QVERIFY(QMetaObject::invokeMethod(widget.get(), "setSetting", Q_ARG(QVariant, QStringLiteral("speedMode")),
                                          Q_ARG(QVariant, QStringLiteral("fast"))));
        QTRY_VERIFY_WITH_TIMEOUT(!replied.isEmpty() && !replied.last().at(1).toBool(), 5000);
        QVERIFY(replied.last().at(2).toString().contains(QLatin1String("normal")));
        QVERIFY(QMetaObject::invokeMethod(widget.get(), "setIntegrationEnabled", Q_ARG(QVariant, QStringLiteral("bogus")),
                                          Q_ARG(QVariant, true)));
        QTRY_VERIFY_WITH_TIMEOUT(replied.size() >= 3 && !replied.last().at(1).toBool(), 5000);
        QCOMPARE(ctl({QStringLiteral("settings"), QStringLiteral("set"), QStringLiteral("runnerId"), QStringLiteral("cat")}), 0);
    }

    void recoversFromABackendRestart()
    {
        auto client = createClient();
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 15000);
        const uint pid = backendPid();

        setActivation(false);
        QCOMPARE(::kill(pid_t(pid), SIGTERM), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!backendRegistered(), 10000);
        // Grace period: no flicker for a quick restart…
        QCOMPARE(status(client.get()), QStringLiteral("connected"));
        // … then an explicit unavailable state, while it keeps retrying.
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("unavailable"), 5000);
        QVERIFY(!client->property("statusMessage").toString().isEmpty());
        QTest::qWait(1500);
        QCOMPARE(status(client.get()), QStringLiteral("unavailable"));

        setActivation(true);
        QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 15000);
        QVERIFY(backendPid() != pid);
        QTRY_COMPARE_WITH_TIMEOUT(runningBackends(), 1, 10000);
        // The new backend counts generations from 1 again: deltas still apply.
        QTRY_VERIFY_WITH_TIMEOUT(matchesBackend(client.get()), 10000);
        const double generation = client->property("generation").toDouble();
        QTRY_VERIFY_WITH_TIMEOUT(client->property("generation").toDouble() > generation, 10000);
    }

    void olderOrIncompatibleBackend()
    {
        setActivation(false);
        stopBackend();
        QDBusConnection bus = QDBusConnection::sessionBus();
        FakeBackend fake;
        QVERIFY(bus.registerObject(aqtruncat::api::ObjectPath, &fake, QDBusConnection::ExportScriptableSlots));
        QVERIFY(bus.registerService(aqtruncat::api::ServiceName));

        fake.state = QJsonObject{{QStringLiteral("api"), 2}, {QStringLiteral("schema"), 1}};
        {
            auto client = createClient();
            QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("incompatible"), 10000);
            QVERIFY(client->property("statusMessage").toString().contains(QLatin1String("API 2")));
            const int calls = fake.getStateCalls;
            QTest::qWait(1500);
            QCOMPARE(fake.getStateCalls, calls); // no polling of an incompatible backend
        }

        // API 1 without the "runners" capability (AQTRunCat 1.4.1).
        fake.getStateCalls = 0;
        fake.state = QJsonObject{
            {QStringLiteral("api"), 1}, {QStringLiteral("schema"), 1}, {QStringLiteral("generation"), 7},
            {QStringLiteral("info"), QJsonObject{{QStringLiteral("version"), QStringLiteral("1.4.1")},
                                                 {QStringLiteral("capabilities"), QJsonArray{QStringLiteral("metrics")}}}},
            {QStringLiteral("metrics"), QJsonArray{}},
            {QStringLiteral("runner"), QJsonObject{{QStringLiteral("runnerId"), QStringLiteral("cat")}, {QStringLiteral("fps"), 10}}},
            {QStringLiteral("settings"), QJsonObject{}},
            {QStringLiteral("integrations"), QJsonArray{}}};
        {
            auto client = createClient();
            QTRY_COMPARE_WITH_TIMEOUT(status(client.get()), QStringLiteral("connected"), 10000);
            QVERIFY(!client->property("hasRunnerCatalog").toBool());
            QVERIFY(jsonProperty(client.get(), "runnerCatalog").isNull());
            QTest::qWait(500);
            QCOMPARE(fake.getRunnersCalls, 0);
        }
        QVERIFY(bus.unregisterService(aqtruncat::api::ServiceName));
        bus.unregisterObject(aqtruncat::api::ObjectPath);
        setActivation(true);
    }

    // The widget's configuration pages, as Plasma's dialog creates them:
    // the shared settings page is a client of its own, the appearance page
    // maps every option of the widget's schema.
    void configurationPagesLoad()
    {
        QQmlComponent general(&m_engine, QUrl::fromLocalFile(packageDir() + QStringLiteral("/contents/ui/configGeneral.qml")));
        std::unique_ptr<QObject> page(general.create());
        QVERIFY2(page, qPrintable(general.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(jsonProperty(page.get(), "runners").toArray().size() >= 2, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(jsonProperty(page.get(), "networkInterfaces").isArray(), 5000);

        QQmlComponent appearance(&m_engine, QUrl::fromLocalFile(packageDir() + QStringLiteral("/contents/ui/configAppearance.qml")));
        std::unique_ptr<QObject> options(appearance.create());
        QVERIFY2(options, qPrintable(appearance.errorString()));
        for (const char *option : {"cfg_animate", "cfg_showBuiltin", "cfg_showIntegrations", "cfg_showCustom", "cfg_showDetails"}) {
            QVERIFY2(options->setProperty(option, false), option);
            QCOMPARE(options->property(option).toBool(), false);
        }
    }

    void cleanupTestCase()
    {
        setActivation(true);
        stopBackend();
        // The name goes before the process has quite finished exiting.
        QTRY_COMPARE_WITH_TIMEOUT(runningBackends(), 0, 10000);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestPlasmoidClient test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_plasmoidclient.moc"
