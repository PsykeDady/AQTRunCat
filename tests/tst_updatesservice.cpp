#include "app/updatesservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <sys/time.h>

using aqtruncat::metrics::MetricState;

namespace {

// Fake package tools in a private PATH and a fake root (/etc, /var). The
// tools only print fixtures and record how they were called; nothing on
// the host is queried and no package database is touched.
struct Fixture {
    QTemporaryDir tmp;
    QString bin() const { return tmp.filePath(QStringLiteral("bin")); }
    QString root() const { return tmp.filePath(QStringLiteral("root")); }
    QString log() const { return tmp.filePath(QStringLiteral("calls.log")); }
    qint64 now = 1'800'000'000;

    void file(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot create test fixture %s", qPrintable(path));
        f.write(content);
    }
    void rootFile(const QString &relative, const QByteArray &content = {}) { file(root() + u'/' + relative, content); }
    void rootDir(const QString &relative) { QDir().mkpath(root() + u'/' + relative); }

    // A tool that logs "name args… LC_ALL=…" and runs body.
    void tool(const QString &name, const QByteArray &body)
    {
        const QString path = bin() + u'/' + name;
        file(path, "#!/bin/sh\necho \"" + name.toUtf8() + " $* LC_ALL=$LC_ALL\" >> '" + log().toUtf8() + "'\n" + body);
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }

    QStringList calls() const
    {
        QFile f(log());
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(u'\n', Qt::SkipEmptyParts);
    }

    void arch()
    {
        rootFile(QStringLiteral("etc/os-release"), "ID=arch\n");
        rootDir(QStringLiteral("var/lib/pacman/local"));
        rootFile(QStringLiteral("var/lib/pacman/sync/core.db"), "db");
        tool(QStringLiteral("pacman"), "exit 0\n");
    }

    void setAge(const QString &relative, qint64 seconds)
    {
        const QByteArray path = (root() + u'/' + relative).toLocal8Bit();
        struct timeval times[2];
        times[0].tv_sec = times[1].tv_sec = time_t(QDateTime::currentSecsSinceEpoch() - seconds);
        times[0].tv_usec = times[1].tv_usec = 0;
        ::utimes(path.constData(), times);
    }

    UpdatesService::Options options()
    {
        UpdatesService::Options o;
        o.searchPath = {bin()};
        o.root = root();
        o.home = tmp.filePath(QStringLiteral("home"));
        o.checkupdatesDatabase = tmp.filePath(QStringLiteral("checkupdates-db"));
        o.firstCheckDelayMs = 0;
        o.policyChangeDelayMs = 0;
        o.changeDebounceMs = 50;
        o.timeoutMs = 5000;
        return o;
    }
};

// The process has exited (gone, or a zombie waiting for its new parent).
bool gone(pid_t pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return true;
    const QByteArray text = stat.readAll();
    const qsizetype close = text.lastIndexOf(')');
    return close < 0 || text.mid(close + 2, 1) == "Z";
}

bool settled(const aqtruncat::metrics::UpdatesMetrics &m)
{
    return !m.checking && m.status.state != MetricState::Pending;
}

} // namespace

// F5.4 / F5.5: scheduling, read-only commands, failures and timeouts of
// the backend's update checks, with fake tools.
class TestUpdatesService : public QObject
{
    Q_OBJECT

private slots:
    void pacmanLocalCheck()
    {
        Fixture f;
        f.arch();
        const QString privateDb = f.options().checkupdatesDatabase + QStringLiteral("/sync/core.db");
        f.file(privateDb, "db");
        struct timeval oldTime[2];
        oldTime[0].tv_sec = oldTime[1].tv_sec = time_t(QDateTime::currentSecsSinceEpoch() - 10 * 24 * 3600);
        oldTime[0].tv_usec = oldTime[1].tv_usec = 0;
        ::utimes(privateDb.toLocal8Bit().constData(), oldTime);
        f.tool(QStringLiteral("checkupdates"), "echo 'linux 6.17.1-1 -> 6.17.2-1'\necho 'mesa 1:25.2.4-1 -> 1:25.2.5-1'\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
        const auto &p = service.packages();
        QVERIFY2(p.status.ok(), p.status.message.c_str());
        QCOMPARE(*p.count, 2);
        QCOMPARE(p.manager, std::string("pacman"));
        QVERIFY(p.lastSuccess);
        QVERIFY(!p.remote);
        QVERIFY(p.metadataTime);
        QVERIFY(p.metadataStale); // the sync database is ten days old
        QCOMPARE(f.calls(), QStringList{QStringLiteral("checkupdates --nosync --nocolor LC_ALL=C")});
    }

    void remoteRefreshIsOptIn()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "echo 'a 1-1 -> 2-1'\n");
        UpdatesService service(f.options());
        service.setPolicy(60, true);
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
        QCOMPARE(*service.packages().count, 1);
        QVERIFY(service.packages().remote);
        QVERIFY(!service.packages().metadataStale);
        QCOMPARE(f.calls(), QStringList{QStringLiteral("checkupdates --nocolor LC_ALL=C")});
    }

    void exitTwoAndStderrWarningAreZeroUpdates()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "echo 'cached metadata warning' >&2\nexit 2\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
        QVERIFY(service.packages().status.ok());
        QCOMPARE(*service.packages().count, 0);
        QCOMPARE(f.calls().size(), 1);
    }

    void archDerivativeUsesCheckupdatesAndDebianDoesNot()
    {
        {
            Fixture f;
            f.arch();
            f.rootFile(QStringLiteral("etc/os-release"), "ID=garuda\nID_LIKE=arch\n");
            f.tool(QStringLiteral("checkupdates"), "echo 'a 1-1 -> 2-1'\n");
            UpdatesService service(f.options());
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
            QCOMPARE(service.packages().manager, std::string("pacman"));
            QCOMPARE(*service.packages().count, 1);
            QCOMPARE(f.calls(), QStringList{QStringLiteral("checkupdates --nosync --nocolor LC_ALL=C")});
        }
        {
            Fixture f;
            f.rootFile(QStringLiteral("etc/os-release"), "ID=debian\nID_LIKE=debian\n");
            f.rootFile(QStringLiteral("var/lib/dpkg/status"));
            f.rootDir(QStringLiteral("var/lib/pacman/local"));
            f.tool(QStringLiteral("pacman"), "echo forbidden >> '" + f.log().toUtf8() + "'\n");
            f.tool(QStringLiteral("checkupdates"), "echo forbidden >> '" + f.log().toUtf8() + "'\n");
            f.tool(QStringLiteral("apt-get"), "echo 'Inst bash [1] (2 Debian [amd64])'\n");
            UpdatesService service(f.options());
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
            QCOMPARE(service.packages().manager, std::string("apt"));
            QCOMPARE(*service.packages().count, 1);
            QCOMPARE(f.calls(), QStringList{QStringLiteral("apt-get -s -q -o Debug::NoLocking=true dist-upgrade LC_ALL=C")});
        }
    }

    void remoteWithoutCheckupdatesSaysSo()
    {
        Fixture f;
        f.arch();
        UpdatesService service(f.options());
        service.setPolicy(60, true);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.packages().status.state, MetricState::Unavailable, 10000);
        QVERIFY(!service.packages().count);
        QVERIFY(QString::fromStdString(service.packages().status.message).contains(QLatin1String("checkupdates")));
        QVERIFY(QString::fromStdString(service.packages().status.message).contains(QLatin1String("pacman-contrib")));
        QVERIFY(f.calls().isEmpty()); // never falls back to pacman -Qu
    }

    void aptAndDnfByCapability()
    {
        {
            Fixture f;
            f.rootFile(QStringLiteral("etc/os-release"), "ID=ubuntu\nID_LIKE=debian\n");
            f.rootFile(QStringLiteral("var/lib/dpkg/status"));
            f.tool(QStringLiteral("apt-get"), "echo 'Inst bash [5.2-1] (5.2-2 Ubuntu [amd64])'\n");
            UpdatesService service(f.options());
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
            QCOMPARE(service.packages().manager, std::string("apt"));
            QCOMPARE(*service.packages().count, 1);
            QCOMPARE(f.calls(), QStringList{QStringLiteral("apt-get -s -q -o Debug::NoLocking=true dist-upgrade LC_ALL=C")});
        }
        {
            Fixture f;
            f.rootFile(QStringLiteral("etc/os-release"), "ID=fedora\n");
            f.rootDir(QStringLiteral("usr/lib/sysimage/rpm"));
            f.tool(QStringLiteral("dnf"), "echo 'bash.x86_64 5.3-1.fc44 updates'\nexit 100\n");
            UpdatesService service(f.options());
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
            QCOMPARE(service.packages().manager, std::string("dnf"));
            QCOMPARE(*service.packages().count, 1);
            QCOMPARE(f.calls(), QStringList{QStringLiteral("dnf -q -C check-update LC_ALL=C")});
        }
    }

    void noPackageManagerIsUnavailable()
    {
        Fixture f;
        f.rootFile(QStringLiteral("etc/os-release"), "ID=gentoo\n");
        f.tool(QStringLiteral("pacman"), "exit 0\n"); // a binary without its database is not a pacman system
        UpdatesService service(f.options());
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.packages().status.state, MetricState::Unavailable, 10000);
        QVERIFY(!service.packages().count);
        QVERIFY(f.calls().isEmpty());
    }

    void failureIsAnErrorNotZero()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "echo 'error: could not open database' >&2\nexit 1\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.packages().status.state, MetricState::Error, 10000);
        QVERIFY(!service.packages().count);
        QCOMPARE(service.packages().status.message, std::string("error: could not open database"));
    }

    void timeoutKillsTheWholeProcessGroup()
    {
        Fixture f;
        f.arch();
        const QString pidFile = f.tmp.filePath(QStringLiteral("child.pid"));
        f.tool(QStringLiteral("checkupdates"), "sleep 30 &\necho $! > '" + pidFile.toUtf8() + "'\nwait\n");
        UpdatesService::Options options = f.options();
        options.timeoutMs = 500;
        UpdatesService service(options);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.packages().status.state, MetricState::Error, 10000);
        QVERIFY(QString::fromStdString(service.packages().status.message).contains(QLatin1String("timed out")));
        QFile file(pidFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const pid_t child = pid_t(file.readAll().trimmed().toInt());
        QVERIFY(child > 0);
        // The helper the tool started is gone too (zombies are reaped by init).
        QTRY_VERIFY_WITH_TIMEOUT(gone(child), 5000);
    }

    void checksNeverOverlap()
    {
        Fixture f;
        f.arch();
        const QString lock = f.tmp.filePath(QStringLiteral("running"));
        f.tool(QStringLiteral("checkupdates"), "mkdir '" + lock.toUtf8() + "' || echo OVERLAP >> '" + f.log().toUtf8()
                                             + "'\nsleep 0.4\nrmdir '" + lock.toUtf8() + "'\nexit 1\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(service.packages().checking, 10000);
        // Changes while checking: one more check afterwards, never in parallel.
        service.setPolicy(30, false);
        service.setPolicy(15, false);
        QTRY_COMPARE_WITH_TIMEOUT(f.calls().size(), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
        QTest::qWait(600);
        QVERIFY(!f.calls().join(u'\n').contains(QLatin1String("OVERLAP")));
        QCOMPARE(f.calls().size(), 2);
    }

    void turnedOffRunsNothing()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "exit 2\n");
        f.tool(QStringLiteral("flatpak"), "exit 0\n");
        UpdatesService service(f.options());
        service.setPolicy(0, false);
        service.start();
        QTest::qWait(200);
        QCOMPARE(service.packages().status.state, MetricState::Unavailable);
        QCOMPARE(service.flatpak().status.state, MetricState::Unavailable);
        QVERIFY(QString::fromStdString(service.packages().status.message).contains(QLatin1String("turned off")));
        QVERIFY(f.calls().isEmpty());
    }

    void databaseChangeTriggersOneRecheck()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "exit 2\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.packages()), 10000);
        QCOMPARE(f.calls().size(), 1);
        // An upgrade writes many files: one debounced re-check.
        for (int i = 0; i < 5; ++i)
            f.rootDir(QStringLiteral("var/lib/pacman/local/pkg-%1").arg(i));
        QTRY_COMPARE_WITH_TIMEOUT(f.calls().size(), 2, 10000);
        QTest::qWait(300);
        QCOMPARE(f.calls().size(), 2);
    }

    void flatpakAbsentIsUnavailable()
    {
        Fixture f;
        f.arch();
        f.tool(QStringLiteral("checkupdates"), "exit 2\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(service.flatpak().status.state, MetricState::Unavailable, 10000);
        QVERIFY(!service.flatpak().count); // never 0 without Flatpak
        QCOMPARE(service.flatpak().status.message, std::string("Flatpak is not installed"));
    }

    void flatpakCountsAppsAndRuntimesPerInstallation()
    {
        Fixture f;
        f.rootFile(QStringLiteral("etc/flatpak/installations.d/extra.conf"), "[Installation \"extra\"]\nPath=/x\n");
        f.tool(QStringLiteral("flatpak"),
               "case \"$*\" in\n"
               "*--user*--app*) echo 'app/org.a.App/x86_64/stable' ;;\n"
               "*--system*--app*) echo 'app/org.b.App/x86_64/stable'; echo 'app/org.c.App/x86_64/stable' ;;\n"
               "*--system*--runtime*) echo 'runtime/org.freedesktop.Platform/x86_64/25.08' ;;\n"
               "*--installation=extra*--runtime*) echo 'runtime/org.x.Runtime/x86_64/1' ;;\n"
               "esac\n");
        UpdatesService service(f.options());
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.flatpak()), 10000);
        const auto &m = service.flatpak();
        QVERIFY2(m.status.ok(), m.status.message.c_str());
        QCOMPARE(*m.count, 5);
        QCOMPARE(m.apps, 3);
        QCOMPARE(m.runtimes, 2);
        QCOMPARE(m.installations.size(), std::size_t(3));
        QCOMPARE(m.installations[0].name, std::string("user"));
        QCOMPARE(m.installations[2].name, std::string("extra"));
        const QStringList calls = f.calls();
        QCOMPARE(calls.size(), 6);
        for (const QString &call : calls) {
            QVERIFY2(call.contains(QLatin1String("remote-ls")) && call.contains(QLatin1String("--updates"))
                         && call.contains(QLatin1String("--cached")),
                     qPrintable(call));
            const QStringList words = call.split(u' ', Qt::SkipEmptyParts);
            for (const char *verb : {"update", "install", "uninstall", "repair", "-y", "--assumeyes"})
                QVERIFY2(!words.contains(QLatin1String(verb)), qPrintable(call));
        }
    }

    void flatpakPartialFailureIsAnError()
    {
        Fixture f;
        f.tool(QStringLiteral("flatpak"),
               "case \"$*\" in\n"
               "*--system*) echo 'error: Unable to load summary from remote flathub: No cached summary' >&2; exit 1 ;;\n"
               "*) echo 'app/org.a.App/x86_64/stable' ;;\n"
               "esac\n");
        UpdatesService service(f.options());
        service.setPolicy(60, true); // remote: no --cached
        service.start();
        QTRY_VERIFY_WITH_TIMEOUT(settled(service.flatpak()), 10000);
        QCOMPARE(service.flatpak().status.state, MetricState::Error);
        QVERIFY(!service.flatpak().count); // not a misleading partial sum
        QVERIFY(QString::fromStdString(service.flatpak().status.message).contains(QLatin1String("system installation")));
        for (const QString &call : f.calls())
            QVERIFY(!call.contains(QLatin1String("--cached")));
    }

    void stopLeavesNoProcess()
    {
        Fixture f;
        f.arch();
        const QString pidFile = f.tmp.filePath(QStringLiteral("tool.pid"));
        f.tool(QStringLiteral("checkupdates"), "echo $$ > '" + pidFile.toUtf8() + "'\nsleep 30\n");
        {
            UpdatesService service(f.options());
            service.start();
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(pidFile) && QFileInfo(pidFile).size() > 0, 10000);
        } // destroyed while checking
        QFile file(pidFile);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const pid_t pid = pid_t(file.readAll().trimmed().toInt());
        QVERIFY(pid > 0);
        QTRY_VERIFY_WITH_TIMEOUT(gone(pid), 5000);
    }
};

QTEST_GUILESS_MAIN(TestUpdatesService)
#include "tst_updatesservice.moc"
