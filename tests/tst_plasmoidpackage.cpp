// F4.2: the Plasma package plasmoid/package as Plasma 6 expects it
// (metadata, structure, configuration schema and pages), the frontend-only
// rules it must keep (no command execution, no network, no provider files,
// only the backend's API), and — when the Plasma SDK's plasmoidviewer is
// installed — loading it for real in a panel (horizontal and vertical) and
// on the desktop, offscreen, on a private bus with the build's backend,
// without a single QML error or warning from its files.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

namespace {

QString package() { return QStringLiteral(AQTRUNCAT_PLASMOID_PACKAGE); }

QByteArray read(const QString &relative)
{
    QFile file(package() + u'/' + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Every text file of the package: relative path → contents.
QHash<QString, QString> packageFiles()
{
    QHash<QString, QString> files;
    QDirIterator it(package(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        files.insert(QDir(package()).relativeFilePath(path), QString::fromUtf8(read(QDir(package()).relativeFilePath(path))));
    }
    return files;
}

QStringList configEntries()
{
    QStringList entries;
    QXmlStreamReader xml(read(QStringLiteral("contents/config/main.xml")));
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("entry"))
            entries.append(xml.attributes().value(QLatin1String("name")).toString());
    }
    return entries;
}

QSet<QString> matches(const QString &text, const QString &pattern)
{
    QSet<QString> found;
    auto it = QRegularExpression(pattern).globalMatch(text);
    while (it.hasNext())
        found.insert(it.next().captured(1));
    return found;
}

} // namespace

class TestPlasmoidPackage : public QObject
{
    Q_OBJECT

private slots:
    void metadata()
    {
        QJsonParseError error;
        const QJsonObject metadata = QJsonDocument::fromJson(read(QStringLiteral("metadata.json")), &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(metadata.value(QStringLiteral("KPackageStructure")).toString(), QStringLiteral("Plasma/Applet"));
        QVERIFY(metadata.value(QStringLiteral("X-Plasma-API-Minimum-Version")).toString().startsWith(u'6'));
        const QJsonObject plugin = metadata.value(QStringLiteral("KPlugin")).toObject();
        // The id is the installed directory name (share/plasma/plasmoids/<Id>).
        QCOMPARE(plugin.value(QStringLiteral("Id")).toString(), QStringLiteral("io.github.psykedady.aqtruncat"));
        QCOMPARE(plugin.value(QStringLiteral("Version")).toString(), QStringLiteral(AQTRUNCAT_TEST_VERSION));
        QCOMPARE(plugin.value(QStringLiteral("Name")).toString(), QStringLiteral("AQTRunCat"));
        QCOMPARE(plugin.value(QStringLiteral("License")).toString(), QStringLiteral("Apache-2.0"));
        QCOMPARE(plugin.value(QStringLiteral("Icon")).toString(), QStringLiteral("aqtruncat"));
        QVERIFY(!plugin.value(QStringLiteral("Description")).toString().isEmpty());
        QVERIFY(!plugin.value(QStringLiteral("Authors")).toArray().isEmpty());
        QVERIFY(!plugin.value(QStringLiteral("Category")).toString().isEmpty());
    }

    void structure()
    {
        // Plasma 6 loads contents/ui/main.qml; its root is a PlasmoidItem.
        const QString main = QString::fromUtf8(read(QStringLiteral("contents/ui/main.qml")));
        QVERIFY(main.contains(QLatin1String("\nPlasmoidItem {")));
        QVERIFY(main.contains(QLatin1String("compactRepresentation:")));
        QVERIFY(main.contains(QLatin1String("fullRepresentation:")));
        const QString full = QString::fromUtf8(read(QStringLiteral("contents/ui/FullRepresentation.qml")));
        QVERIFY(full.contains(QLatin1String("Open AQTRunCat Settings")));
        QVERIFY(full.contains(QLatin1String("io.github.psykedady.AQTRunCat.Settings")));
        // Configuration pages named by config.qml exist.
        const QString config = QString::fromUtf8(read(QStringLiteral("contents/config/config.qml")));
        const QSet<QString> pages = matches(config, QStringLiteral("source:\\s*\"([^\"]+)\""));
        QVERIFY(!pages.isEmpty());
        for (const QString &page : pages)
            QVERIFY2(QFile::exists(package() + QStringLiteral("/contents/ui/") + page), qPrintable(page));
        // Components used across files exist as files of the package.
        const QHash<QString, QString> files = packageFiles();
        for (const QString &type : {QStringLiteral("BackendClient"), QStringLiteral("RunnerView"),
                                    QStringLiteral("CompactRepresentation"), QStringLiteral("FullRepresentation"),
                                    QStringLiteral("MetricDelegate"), QStringLiteral("RefreshArea")})
            QVERIFY2(files.contains(QStringLiteral("contents/ui/%1.qml").arg(type)), qPrintable(type));
        // Only QML, JavaScript, JSON and XML: nothing to build, no binaries.
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString suffix = QFileInfo(it.key()).suffix();
            QVERIFY2(QStringList({QStringLiteral("qml"), QStringLiteral("js"), QStringLiteral("json"), QStringLiteral("xml")})
                         .contains(suffix), qPrintable(it.key()));
        }
    }

    void configuration()
    {
        const QStringList entries = configEntries();
        QVERIFY(entries.contains(QStringLiteral("animate")));
        // Every entry has a default.
        QXmlStreamReader xml(read(QStringLiteral("contents/config/main.xml")));
        int defaults = 0;
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("default"))
                ++defaults;
        }
        QVERIFY(!xml.hasError());
        QCOMPARE(defaults, entries.size());

        const QHash<QString, QString> files = packageFiles();
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            for (const QString &used : matches(it.value(), QStringLiteral("Plasmoid\\.configuration\\.(\\w+)")))
                QVERIFY2(entries.contains(used), qPrintable(it.key() + u':' + used));
        }
        // Pages: their cfg_ properties are entries (or their …Default);
        // a page that does not edit the widget's options still declares
        // all of them, so Plasma has nothing to complain about.
        for (const QString &page : {QStringLiteral("configGeneral.qml"), QStringLiteral("configAppearance.qml")}) {
            const QString text = files.value(QStringLiteral("contents/ui/") + page);
            QVERIFY(!text.isEmpty());
            for (QString used : matches(text, QStringLiteral("property\\s+(?:alias|bool|int|string|var)\\s+cfg_(\\w+)"))) {
                if (used.endsWith(QLatin1String("Default")))
                    used.chop(7);
                QVERIFY2(entries.contains(used), qPrintable(page + u':' + used));
            }
        }
        const QString general = files.value(QStringLiteral("contents/ui/configGeneral.qml"));
        for (const QString &entry : entries) {
            QVERIFY2(general.contains(QStringLiteral("cfg_") + entry + u'\n'), qPrintable(entry));
            QVERIFY2(general.contains(QStringLiteral("cfg_") + entry + QStringLiteral("Default")), qPrintable(entry));
        }
    }

    // A frontend only: it reads the backend's API and nothing else.
    void frontendOnly()
    {
        const QHash<QString, QString> files = packageFiles();
        const QStringList forbidden = {
            QStringLiteral("XMLHttpRequest"), QStringLiteral("WebSocket"), QStringLiteral("WebEngine"),
            QStringLiteral("Plasma5Support"), QStringLiteral("DataSource"), QStringLiteral("executable"),
            QStringLiteral("Qt.openUrlExternally"), QStringLiteral(".claude"), QStringLiteral(".codex"),
            QStringLiteral("CLAUDE_CONFIG_DIR"), QStringLiteral("CODEX_HOME"), QStringLiteral("FolderListModel"),
            QStringLiteral("SystemBus"), QStringLiteral("/proc/"), QStringLiteral("/sys/"), QStringLiteral("console.log"),
        };
        QSet<QString> members;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (it.key() == QLatin1String("metadata.json"))
                continue;
            for (const QString &word : forbidden)
                QVERIFY2(!it.value().contains(word), qPrintable(it.key() + QStringLiteral(" uses ") + word));
            // No URL (main.xml only names its XML schema).
            if (!it.key().endsWith(QLatin1String(".xml")))
                QVERIFY2(!it.value().contains(QLatin1String("http://")) && !it.value().contains(QLatin1String("https://")),
                         qPrintable(it.key()));
            members.unite(matches(it.value(), QStringLiteral("call\\(\"(\\w+)\"")));
        }
        // Only methods of the backend's API (io.github.psykedady.AQTRunCat1).
        const QSet<QString> api = {QStringLiteral("GetState"), QStringLiteral("GetMetrics"), QStringLiteral("GetRunners"),
                                   QStringLiteral("SetSetting"), QStringLiteral("SetIntegrationEnabled"),
                                   QStringLiteral("SetIntegrationLiveUpdates"), QStringLiteral("RegisterCustomMetric"),
                                   QStringLiteral("UnregisterCustomMetric"), QStringLiteral("SetCustomMetricEnabled"),
                                   QStringLiteral("ReloadCustomMetrics"), // v2F1.6
                                   QStringLiteral("RefreshCustomMetric")}; // v2F1.7
        QVERIFY(!members.isEmpty());
        for (const QString &member : members)
            QVERIFY2(api.contains(member), qPrintable(member));
        // The service it talks to is the backend's, on the session bus.
        const QString logic = files.value(QStringLiteral("contents/code/logic.js"));
        QVERIFY(logic.contains(QLatin1String("\"io.github.psykedady.AQTRunCat1\"")));
        QVERIFY(files.value(QStringLiteral("contents/ui/BackendClient.qml")).contains(QLatin1String("DBus.SessionBus.asyncCall")));
    }

    void loadsInPlasma_data()
    {
        QTest::addColumn<QStringList>("arguments");
        // In Plasma's panel containment the widget is compact (the runner)
        // with the popup behind it; on the desktop it is shown in place.
        QTest::newRow("horizontal panel") << QStringList{QStringLiteral("-c"), QStringLiteral("org.kde.panel"),
                                                         QStringLiteral("-f"), QStringLiteral("horizontal"),
                                                         QStringLiteral("-l"), QStringLiteral("bottomedge")};
        QTest::newRow("vertical panel") << QStringList{QStringLiteral("-c"), QStringLiteral("org.kde.panel"),
                                                       QStringLiteral("-f"), QStringLiteral("vertical"),
                                                       QStringLiteral("-l"), QStringLiteral("leftedge")};
        QTest::newRow("desktop") << QStringList{QStringLiteral("-f"), QStringLiteral("planar")};
    }

    void loadsInPlasma()
    {
        QFETCH(QStringList, arguments);
        const QString viewer = QStandardPaths::findExecutable(QStringLiteral("plasmoidviewer"));
        if (viewer.isEmpty())
            QSKIP("plasmoidviewer (Plasma SDK) is not installed");
        if (qEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").isEmpty())
            QSKIP("needs the private session bus of dbus-run-session");

        QTemporaryDir home;
        QVERIFY(home.isValid());
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("WAYLAND_DISPLAY"));
        environment.remove(QStringLiteral("DISPLAY"));
        environment.insert(QStringLiteral("HOME"), home.path());
        for (const char *name : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME"})
            environment.insert(QLatin1String(name), home.filePath(QLatin1String(name)));
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), home.filePath(QStringLiteral("runtime")));
        QDir().mkpath(home.filePath(QStringLiteral("runtime")));
        QFile::setPermissions(home.filePath(QStringLiteral("runtime")),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        environment.insert(QStringLiteral("CLAUDE_CONFIG_DIR"), home.filePath(QStringLiteral("no-claude")));
        environment.insert(QStringLiteral("CODEX_HOME"), home.filePath(QStringLiteral("no-codex")));
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));

        QProcess backend;
        backend.setProcessEnvironment(environment);
        backend.start(QStringLiteral(AQTRUNCAT_DAEMON), {});
        QVERIFY(backend.waitForStarted());

        QProcess plasma;
        plasma.setProcessEnvironment(environment);
        plasma.setProcessChannelMode(QProcess::MergedChannels);
        plasma.start(viewer, QStringList{QStringLiteral("-a"), package()} + arguments);
        QVERIFY(plasma.waitForStarted());
        // Long enough to connect, take the state, get deltas and the runner.
        QTest::qWait(7000);
        plasma.terminate();
        if (!plasma.waitForFinished(10000)) {
            plasma.kill();
            plasma.waitForFinished();
        }
        backend.terminate();
        QVERIFY(backend.waitForFinished(10000));

        const QString output = QString::fromUtf8(plasma.readAll());
        QVERIFY2(output.contains(QLatin1String("New Containment")), qPrintable(output.right(2000)));
        QStringList ours;
        for (const QString &line : output.split(u'\n')) {
            if (line.contains(package()))
                ours.append(line);
        }
        QVERIFY2(ours.isEmpty(), qPrintable(ours.join(u'\n')));
    }
};

QTEST_GUILESS_MAIN(TestPlasmoidPackage)
#include "tst_plasmoidpackage.moc"
