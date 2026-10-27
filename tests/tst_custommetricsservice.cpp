#include "app/appsettings.h"
#include "app/custommetricsservice.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTest>

#include <unistd.h>

using aqtruncat::custommetrics::State;

namespace {
QByteArray snapshot(const QString &value)
{
    return QStringLiteral(R"({"title":"Fixture","symbol":"★","metricsBarValue":"%1",
        "metrics":[{"title":"Value","formattedValue":"%1"}],
        "lastUpdatedDate":"2026-06-05T04:50:40Z"})").arg(value).toUtf8();
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents), contents.size());
}

void atomicWrite(const QString &path, const QByteArray &contents)
{
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(contents), contents.size());
    QVERIFY(file.commit());
}
}

class TestCustomMetricsService : public QObject
{
    Q_OBJECT

private slots:
    void explicitRegistrationPersistsAndDoesNotDeleteFile()
    {
        QTemporaryDir temporary;
        const QString ini = temporary.filePath(QStringLiteral("settings.ini"));
        const QString discovery = temporary.filePath(QStringLiteral("data/AQTRunCat/metrics"));
        const QString file = temporary.filePath(QStringLiteral("outside/metric.json"));
        writeFile(file, snapshot(QStringLiteral("one")));

        aqtruncat::AppSettings settings(QStringLiteral("cat"), ini);
        CustomMetricsService service(&settings, discovery);
        service.start();
        QString error;
        QVERIFY2(service.registerFile(file, &error), qPrintable(error));
        QTRY_COMPARE(service.sources().size(), 1);
        QCOMPARE(service.sources().first().state, State::Available);
        QCOMPARE(service.sources().first().snapshot->metricsBarValue, std::optional<QString>(QStringLiteral("one")));

        aqtruncat::AppSettings reloaded(QStringLiteral("cat"), ini);
        QCOMPARE(reloaded.customMetricFiles(), QStringList{QFileInfo(file).absoluteFilePath()});
        service.unregisterFile(file);
        QVERIFY(QFileInfo::exists(file));
        QTRY_VERIFY(service.sources().isEmpty());
    }

    void discoveryIsSortedAndDeduplicatedWithExplicitPath()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString a = discovery + QStringLiteral("/a.json");
        const QString b = discovery + QStringLiteral("/b.json");
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({a});
        CustomMetricsService service(&settings, discovery);
        service.start();
        QCOMPARE(service.sources().size(), 1);
        QCOMPARE(service.sources().first().state, State::Unavailable);

        writeFile(b, snapshot(QStringLiteral("b")));
        writeFile(a, snapshot(QStringLiteral("a")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), 2, 1500);
        QVERIFY(service.sources().at(0).explicitlyRegistered);
        QVERIFY(service.sources().at(0).discovered);
        QCOMPARE(service.sources().at(0).path, a);
        QCOMPARE(service.sources().at(1).path, b);

        QVERIFY(QFile::remove(b));
        QTRY_COMPARE(service.sources().size(), 1);
    }

    void recursiveDiscoveryUsesPlainContainerDirectories()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString collection = discovery + QStringLiteral("/ExampleMetrics");
        const QString standalone = collection + QStringLiteral("/legacy.json");
        const QString package = collection + QStringLiteral("/d10/metric.json");
        const QString hiddenByPackage = collection + QStringLiteral("/d10/nested/metric.json");
        const QString emptyContainer = collection + QStringLiteral("/more/examples");
        writeFile(standalone, snapshot(QStringLiteral("legacy")));
        writeFile(package, snapshot(QStringLiteral("d10")));
        writeFile(hiddenByPackage, snapshot(QStringLiteral("not-top-level")));
        QDir().mkpath(emptyContainer);

        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QCOMPARE(service.sources().size(), 2);
        QCOMPARE(service.sources().at(0).path, standalone);
        QCOMPARE(service.sources().at(1).path, package);
        QVERIFY(service.sources().at(0).discovered);
        QVERIFY(service.sources().at(1).discovered);

        // Empty organisational directories are watched too: turning one
        // into a package is detected without a manual reload.
        const QString added = emptyContainer + QStringLiteral("/metric.json");
        writeFile(added, snapshot(QStringLiteral("new")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().size(), 3, 1500);
        QCOMPARE(service.sources().at(2).path, added);
        service.stop();
    }

    void watchesModifyAtomicReplaceRemoveAndRecreate()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString file = temporary.filePath(QStringLiteral("outside.json"));
        writeFile(file, snapshot(QStringLiteral("one")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({file});
        CustomMetricsService service(&settings, discovery);
        service.start();
        QCOMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("one"));

        writeFile(file, snapshot(QStringLiteral("two")));
        QTRY_COMPARE_WITH_TIMEOUT(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("two"), 1500);
        atomicWrite(file, snapshot(QStringLiteral("three")));
        QTRY_COMPARE_WITH_TIMEOUT(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("three"), 1500);

        QVERIFY(QFile::remove(file));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Unavailable, 2000);
        QCOMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("three"));
        atomicWrite(file, snapshot(QStringLiteral("four")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Available, 1500);
        QCOMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("four"));
    }

    void transientFailureKeepsLastGoodThenPermanentErrorRecovers()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString file = temporary.filePath(QStringLiteral("metric.json"));
        writeFile(file, snapshot(QStringLiteral("good")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({file});
        CustomMetricsService service(&settings, discovery);
        service.start();

        writeFile(file, QByteArray("{"));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Pending, 1000);
        QVERIFY(service.sources().first().snapshot);
        QCOMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("good"));
        atomicWrite(file, snapshot(QStringLiteral("recovered")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Available, 1500);
        QCOMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("recovered"));

        writeFile(file, QByteArray("not json"));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Error, 2500);
        QVERIFY(service.sources().first().snapshot); // last known good remains available to the UI
        atomicWrite(file, snapshot(QStringLiteral("again")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Available, 1500);
    }

    void temporarilyUnreadableFileErrorsQuietlyAndRecovers()
    {
        if (::geteuid() == 0)
            QSKIP("file permissions are not enforced for root");
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("metric.json"));
        writeFile(file, snapshot(QStringLiteral("good")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setCustomMetricFiles({file});
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("metrics")));
        service.start();
        QTRY_COMPARE(service.sources().first().state, State::Available);

        // No "inotify_add_watch ... Permission denied" warning per retry (M8).
        QTest::failOnWarning(QRegularExpression(QStringLiteral("inotify_add_watch")));
        QVERIFY(QFile::setPermissions(file, QFileDevice::Permissions()));
        service.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Error, 2500);
        QVERIFY(service.sources().first().snapshot); // last known good kept
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Available, 7000);
    }

    void rapidProducerUpdatesAreCoalescedAndEndOnTheLastValue()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString file = discovery + QStringLiteral("/fast.json");
        writeFile(file, snapshot(QStringLiteral("0")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_COMPARE(service.sources().size(), 1);
        int published = 0;
        connect(&service, &CustomMetricsService::sourcesChanged, this, [&] { ++published; });
        for (int i = 1; i <= 200; ++i)
            atomicWrite(file, snapshot(QString::number(i)));
        QTRY_COMPARE(*service.sources().first().snapshot->metricsBarValue, QStringLiteral("200"));
        QCOMPARE(service.sources().first().state, State::Available);
        QVERIFY2(published < 20, qPrintable(QString::number(published))); // debounced, not one reload per write
    }

    void missingExplicitTargetCanBeRegisteredForFutureProducer()
    {
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("metrics")));
        service.start();
        const QString future = temporary.filePath(QStringLiteral("producer/future.json"));
        QString error;
        QVERIFY(service.registerFile(future, &error));
        QTRY_COMPARE(service.sources().size(), 1);
        QTRY_COMPARE(service.sources().first().state, State::Unavailable);
        writeFile(future, snapshot(QStringLiteral("born")));
        QTRY_COMPARE_WITH_TIMEOUT(service.sources().first().state, State::Available, 1500);
    }

    void invalidExistingFileIsRejectedWithDiagnostic()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("broken.json"));
        writeFile(file, QByteArray("not json"));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, temporary.filePath(QStringLiteral("metrics")));
        service.start();
        QString error;
        QVERIFY(!service.registerFile(file, &error));
        QVERIFY(error.contains(QStringLiteral("invalid JSON")));
        QVERIFY(settings.customMetricFiles().isEmpty());
    }

    // v2F1.2: a snapshot file with "enabled": false is listed without values
    // (never presented); the flag is written by setEnabled and applied at once.
    void disabledSnapshotIsListedWithoutValues()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString file = discovery + QStringLiteral("/fixture.json");
        QByteArray disabled = snapshot(QStringLiteral("5"));
        disabled.insert(1, "\"enabled\":false,");
        writeFile(file, disabled);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        QString error;
        QVERIFY2(service.registerFile(file, &error), qPrintable(error)); // a disabled file is a valid one
        service.start();
        QCOMPARE(service.sources().size(), 1);
        QVERIFY(!service.sources().first().enabled);
        QCOMPARE(service.sources().first().snapshot->title, QStringLiteral("Fixture")); // name only
        QVERIFY(service.sources().first().snapshot->metrics.isEmpty());
        QCOMPARE(service.sources().first().state, State::Unavailable);

        QVERIFY2(service.setEnabled(file, true, &error), qPrintable(error));
        QVERIFY(service.sources().first().enabled);
        QCOMPARE(service.sources().first().state, State::Available);
        QCOMPARE(service.sources().first().snapshot->metrics.first().formattedValue, QStringLiteral("5"));

        // A producer rewriting the file without the flag: enabled (missing = true).
        atomicWrite(file, snapshot(QStringLiteral("6")));
        QTRY_COMPARE(service.sources().first().snapshot->metrics.first().formattedValue, QStringLiteral("6"));
        QVERIFY2(service.setEnabled(file, false, &error), qPrintable(error));
        QVERIFY(service.sources().first().snapshot->metrics.isEmpty());
        QFile stored(file);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        QVERIFY(stored.readAll().startsWith("{\"enabled\": false,\"title\":\"Fixture\""));
    }
};

QTEST_GUILESS_MAIN(TestCustomMetricsService)
#include "tst_custommetricsservice.moc"
