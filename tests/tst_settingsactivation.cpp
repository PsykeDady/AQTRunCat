#include "app/appsettings.h"
#include "app/settingsactivation.h"
#include "app/settingswindow.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <csignal>
#include <unistd.h>

class TestSettingsActivation : public QObject
{
    Q_OBJECT

private slots:
    void oneSessionBusOwnerActivatesTheSharedWindow()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        SettingsWindow window(&settings, {});
        auto primary = std::make_unique<SettingsActivation>(&window);
        QString error;
        QVERIFY2(primary->acquire(&error), qPrintable(error));

        QVERIFY(aqtruncat::settings::requestShow(&error));
        QVERIFY2(window.isVisible(), qPrintable(error));

        QProcess cli;
        cli.start(QStringLiteral(AQTRUNCAT_CTL), {QStringLiteral("settings"), QStringLiteral("open")});
        QTRY_VERIFY_WITH_TIMEOUT(cli.state() == QProcess::NotRunning, 5000);
        QCOMPARE(cli.exitStatus(), QProcess::NormalExit);
        QCOMPARE(cli.exitCode(), 0);
        QVERIFY(QString::fromUtf8(cli.readAllStandardOutput()).contains(QStringLiteral("Settings opened")));

        QProcess secondLaunch;
        secondLaunch.start(QStringLiteral(AQTRUNCAT_APP), {QStringLiteral("settings")});
        QTRY_VERIFY_WITH_TIMEOUT(secondLaunch.state() == QProcess::NotRunning, 5000);
        QCOMPARE(secondLaunch.exitStatus(), QProcess::NormalExit);
        QCOMPARE(secondLaunch.exitCode(), 0);

        SettingsActivation duplicate(&window);
        error.clear();
        QVERIFY(!duplicate.acquire(&error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(window.isVisible());
        primary.reset();
        SettingsActivation recovered(&window);
        error.clear();
        QVERIFY2(recovered.acquire(&error), qPrintable(error));
        window.hide();
    }

    void standaloneLaunchStartsBackendOnDemandAndLeavesItRunning()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QProcess settings;
        settings.start(QStringLiteral(AQTRUNCAT_APP), {QStringLiteral("settings")});
        QVERIFY(settings.waitForStarted(3000));
        QProcess simultaneousLaunch;
        simultaneousLaunch.start(QStringLiteral(AQTRUNCAT_APP), {QStringLiteral("settings")});
        QVERIFY(simultaneousLaunch.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(bus.interface()->isServiceRegistered(
                                    QLatin1String(aqtruncat::settings::ServiceName)), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(settings.state() == QProcess::NotRunning
                                     || simultaneousLaunch.state() == QProcess::NotRunning, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(bus.interface()->isServiceRegistered(
                                    QStringLiteral("io.github.psykedady.AQTRunCat")), 10000);

        QDBusMessage pidMessage = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetConnectionUnixProcessID"));
        pidMessage.setArguments({QStringLiteral("io.github.psykedady.AQTRunCat")});
        const QDBusMessage pidReply = bus.call(pidMessage, QDBus::Block, 3000);
        QCOMPARE(pidReply.type(), QDBusMessage::ReplyMessage);
        const pid_t backendPid = pid_t(pidReply.arguments().first().toUInt());
        QVERIFY(backendPid > 1);

        QProcess &owner = settings.state() == QProcess::NotRunning ? simultaneousLaunch : settings;
        QVERIFY(owner.state() == QProcess::Running);
        owner.terminate();
        QTRY_VERIFY_WITH_TIMEOUT(owner.state() == QProcess::NotRunning, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(settings.state() == QProcess::NotRunning
                                     && simultaneousLaunch.state() == QProcess::NotRunning, 5000);
        QVERIFY(bus.interface()->isServiceRegistered(QStringLiteral("io.github.psykedady.AQTRunCat")));
        QVERIFY(::kill(backendPid, SIGTERM) == 0);
        QTRY_VERIFY_WITH_TIMEOUT(!bus.interface()->isServiceRegistered(
                                     QStringLiteral("io.github.psykedady.AQTRunCat")), 5000);
    }
};

QTEST_MAIN(TestSettingsActivation)
#include "tst_settingsactivation.moc"
