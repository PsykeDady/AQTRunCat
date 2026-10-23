#include "custommetrics/custommetric.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <unistd.h>

using namespace aqtruncat::custommetrics;

namespace {
QByteArray validJson()
{
    return R"({
      "title":"Claude Code",
      "symbol":"staroflife",
      "metricsBarValue":"5.4%",
      "metrics":[
        {"title":"Model","formattedValue":"Opus 4.7","future":true},
        {"title":"Context","formattedValue":"5.4%","normalizedValue":0.054}
      ],
      "lastUpdatedDate":"2026-06-05T04:50:40Z",
      "futureMetadata":{"anything":1}
    })";
}
}

class TestCustomMetric : public QObject
{
    Q_OBJECT

private slots:
    void parsesCompatibilityFixtureUnmodified()
    {
        QFile file(QFINDTESTDATA("fixtures/custommetrics/claude-code.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const ParseResult result = parseJson(file.readAll());
        QVERIFY2(result.snapshot, qPrintable(result.error));
        QCOMPARE(result.snapshot->title, QStringLiteral("Claude Code"));
        QCOMPARE(result.snapshot->metrics.size(), 2);
    }

    void parsesUpstreamCompatibleSnapshot()
    {
        const ParseResult result = parseJson(validJson());
        QVERIFY2(result.snapshot, qPrintable(result.error));
        QCOMPARE(result.snapshot->title, QStringLiteral("Claude Code"));
        QCOMPARE(*result.snapshot->symbol, QStringLiteral("staroflife"));
        QCOMPARE(*result.snapshot->metricsBarValue, QStringLiteral("5.4%"));
        QCOMPARE(result.snapshot->metrics.size(), 2);
        QCOMPARE(result.snapshot->metrics.at(0).title, QStringLiteral("Model"));
        QCOMPARE(result.snapshot->metrics.at(1).formattedValue, QStringLiteral("5.4%"));
        QCOMPARE(*result.snapshot->metrics.at(1).normalizedValue, 0.054);
        QCOMPARE(result.snapshot->lastUpdatedDate.toUTC(),
                 QDateTime::fromString(QStringLiteral("2026-06-05T04:50:40Z"), Qt::ISODate).toUTC());
    }

    void optionalNullEmptyMetricsUnicodeAndFractionalDate()
    {
        const auto result = parseJson(QString::fromUtf8(R"({"title":"温度 🌡","symbol":null,"metricsBarValue":null,
            "metrics":[],"lastUpdatedDate":"2026-06-05T04:50:40.123+02:00"})").toUtf8());
        QVERIFY2(result.snapshot, qPrintable(result.error));
        QVERIFY(!result.snapshot->symbol);
        QVERIFY(!result.snapshot->metricsBarValue);
        QVERIFY(result.snapshot->metrics.isEmpty());
        QCOMPARE(result.snapshot->lastUpdatedDate.offsetFromUtc(), 7200);
    }

    void preservesEntryOrderAndOpaqueFormatting()
    {
        const auto result = parseJson(R"({"title":"Sensors","metrics":[
            {"title":"Temperature","formattedValue":"48.5 °C"},
            {"title":"Fan","formattedValue":"0 rpm"},
            {"title":"Power","formattedValue":"—"}],
            "lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        QVERIFY(result.snapshot);
        QCOMPARE(result.snapshot->metrics.at(0).title, QStringLiteral("Temperature"));
        QCOMPARE(result.snapshot->metrics.at(1).title, QStringLiteral("Fan"));
        QCOMPARE(result.snapshot->metrics.at(2).formattedValue, QString::fromUtf8("—"));
    }

    void structuredThemeAndFileSymbolsExtendLegacySnapshots()
    {
        const auto theme = parseJson(R"({"title":"Theme","symbol":{"type":"theme","value":"temperature"},
            "metrics":[],"lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        QVERIFY2(theme.snapshot, qPrintable(theme.error));
        QVERIFY(theme.snapshot->icon);
        QCOMPARE(theme.snapshot->icon->type, IconType::Theme);
        QCOMPARE(theme.snapshot->icon->resolvedValue, QStringLiteral("temperature"));

        QTemporaryDir temporary;
        const auto file = parseJson(R"({"title":"File","symbol":{"type":"file","value":"./icons/value.svg"},
            "metrics":[],"lastUpdatedDate":"2026-06-05T04:50:40Z"})",
            temporary.filePath(QStringLiteral("package")));
        QVERIFY2(file.snapshot, qPrintable(file.error));
        QCOMPARE(file.snapshot->icon->type, IconType::File);
        QCOMPARE(file.snapshot->icon->resolvedValue,
                 QDir::cleanPath(temporary.filePath(QStringLiteral("package/icons/value.svg"))));
    }

    void rejectsMalformedMissingAndWrongTypes_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << QByteArray() << QStringLiteral("invalid JSON");
        QTest::newRow("malformed") << QByteArray("{") << QStringLiteral("invalid JSON");
        QTest::newRow("not-object") << QByteArray("[]") << QStringLiteral("top-level");
        QTest::newRow("title-missing")
            << QByteArray(R"({"metrics":[],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("title");
        QTest::newRow("metrics-missing")
            << QByteArray(R"({"title":"x","lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("metrics");
        QTest::newRow("entry-wrong")
            << QByteArray(R"({"title":"x","metrics":[1],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("metrics[0]");
        QTest::newRow("formatted-missing")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y"}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("formattedValue");
        QTest::newRow("date-invalid")
            << QByteArray(R"({"title":"x","metrics":[],"lastUpdatedDate":"yesterday"})")
            << QStringLiteral("ISO 8601");
        QTest::newRow("item-id-invalid")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","id":"a b"}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("metrics[0].id");
        QTest::newRow("item-id-colon")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","id":"a:b"}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("metrics[0].id");
        QTest::newRow("item-id-duplicate")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","id":"a"},{"title":"z","formattedValue":"2","id":"a"}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("not unique");
        QTest::newRow("item-detail-type")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","detail":5}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("detail");
        QTest::newRow("item-warning-type")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","warning":"yes"}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("warning");
        QTest::newRow("item-bar-value-type")
            << QByteArray(R"({"title":"x","metrics":[{"title":"y","formattedValue":"1","metricsBarValue":[]}],"lastUpdatedDate":"2026-01-01T00:00:00Z"})")
            << QStringLiteral("metricsBarValue");
    }

    void itemIdsDetailsAndOwnBarValues()
    {
        // AQTRunCat extensions of a snapshot item, all optional.
        const ParseResult result = parseJson(R"({"title":"Usage","metrics":[
            {"title":"Current session","formattedValue":"3% used","normalizedValue":0.03,
             "id":"five_hour","detail":"Resets at 13:30","metricsBarValue":"3%","metricsBarLabel":"5h"},
            {"title":"Plain","formattedValue":"x","detail":null},
            {"title":"Stale","formattedValue":"Out of date","warning":true}],
            "lastUpdatedDate":"2026-01-01T00:00:00Z"})");
        QVERIFY2(result.snapshot, qPrintable(result.error));
        const Item &quota = result.snapshot->metrics.at(0);
        QCOMPARE(quota.id, QStringLiteral("five_hour"));
        QCOMPARE(quota.detail, QStringLiteral("Resets at 13:30"));
        QCOMPARE(*quota.metricsBarValue, QStringLiteral("3%"));
        QCOMPARE(quota.metricsBarLabel, QStringLiteral("5h"));
        const Item &plain = result.snapshot->metrics.at(1);
        QVERIFY(plain.id.isEmpty() && plain.detail.isEmpty() && !plain.metricsBarValue && plain.metricsBarLabel.isEmpty());
        QVERIFY(!plain.warning && !quota.warning);
        QVERIFY(result.snapshot->metrics.at(2).warning);
        QVERIFY(isValidItemId(QStringLiteral("seven_day.v2-x")));
        QVERIFY(!isValidItemId(QStringLiteral("-x")));
        QVERIFY(!isValidItemId(QString(65, u'a')));
    }

    void rejectsMalformedMissingAndWrongTypes()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, message);
        const ParseResult result = parseJson(json);
        QVERIFY(!result.snapshot);
        QVERIFY2(result.error.contains(message), qPrintable(result.error));
    }

    void enforcesInputLimits()
    {
        QCOMPARE(parseJson(QByteArray(MaxJsonBytes + 1, ' ')).snapshot.has_value(), false);

        QJsonArray entries;
        for (int i = 0; i <= MaxMetricEntries; ++i)
            entries.append(QJsonObject{{QStringLiteral("title"), QStringLiteral("x")},
                                       {QStringLiteral("formattedValue"), QStringLiteral("y")}});
        QJsonObject tooMany{{QStringLiteral("title"), QStringLiteral("x")},
                            {QStringLiteral("metrics"), entries},
                            {QStringLiteral("lastUpdatedDate"), QStringLiteral("2026-01-01T00:00:00Z")}};
        QVERIFY(!parseJson(QJsonDocument(tooMany).toJson()).snapshot);

        QJsonObject tooLong{{QStringLiteral("title"), QString(MaxStringCharacters + 1, u'x')},
                            {QStringLiteral("metrics"), QJsonArray{}},
                            {QStringLiteral("lastUpdatedDate"), QStringLiteral("2026-01-01T00:00:00Z")}};
        QVERIFY(!parseJson(QJsonDocument(tooLong).toJson()).snapshot);
    }

    // v2F1.2: the snapshot file's optional top-level "enabled".
    void snapshotEnabledFlag()
    {
        QJsonObject object = QJsonDocument::fromJson(validJson()).object();
        ParseResult result = parseJson(QJsonDocument(object).toJson());
        QVERIFY(result.snapshot);
        QVERIFY(result.enabled); // omitted: enabled, the M6/v2 files are unchanged
        object.insert(QStringLiteral("enabled"), false);
        result = parseJson(QJsonDocument(object).toJson());
        QVERIFY(result.snapshot);
        QVERIFY(!result.enabled);
        object.insert(QStringLiteral("enabled"), QStringLiteral("no"));
        result = parseJson(QJsonDocument(object).toJson());
        QVERIFY(!result.snapshot);
        QCOMPARE(result.error, QStringLiteral("field \"enabled\" must be a boolean"));
    }

    // writeEnabled changes only the flag: key order, formatting and unknown
    // fields stay byte for byte; the key is inserted first when missing.
    void writeEnabledKeepsTheRestOfTheFile()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("metric.json"));
        const QByteArray original =
            "{\n    \"title\": \"T\",\n    \"source\": {\"type\": \"command\", \"enabled\": \"inner\"},\n"
            "    \"zeta\": [1, {\"a\": \"}\\\"{\"}],\n    \"alpha\": null\n}\n";
        auto write = [&](const QByteArray &bytes) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(bytes);
        };
        auto read = [&] {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        write(original);
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup));
        QString error;
        QVERIFY2(writeEnabled(path, false, &error), qPrintable(error));
        QCOMPARE(read(), QByteArray("{\n    \"enabled\": false,\n") + original.mid(2));
        QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup),
                 QFileDevice::ReadGroup); // the file's permissions are kept
        QVERIFY(writeEnabled(path, true, &error));
        QCOMPARE(read(), QByteArray("{\n    \"enabled\": true,\n") + original.mid(2)); // replaced in place
        QVERIFY(writeEnabled(path, true, &error));
        QCOMPARE(read(), QByteArray("{\n    \"enabled\": true,\n") + original.mid(2)); // nothing to change

        write("{\"title\":\"T\",\"enabled\":true}");
        QVERIFY(writeEnabled(path, false, &error));
        QCOMPARE(read(), QByteArray("{\"title\":\"T\",\"enabled\":false}"));
        write("{}");
        QVERIFY(writeEnabled(path, false, &error));
        QCOMPARE(read(), QByteArray("{\"enabled\": false}"));

        // Through a symlink: the target changes, the link stays a link.
        const QString link = directory.filePath(QStringLiteral("link.json"));
        QVERIFY(QFile::link(path, link));
        QVERIFY(writeEnabled(link, true, &error));
        QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(read(), QByteArray("{\"enabled\": true}"));
    }

    void writeEnabledRefusesWithoutDamage()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("metric.json"));
        QString error;
        QVERIFY(!writeEnabled(path, false, &error)); // missing
        QVERIFY(error.contains(QLatin1String("cannot read")));
        for (const QByteArray &bad : {QByteArray("[1, 2]"), QByteArray("{\"title\": "), QByteArray("not json")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(bad);
            file.close();
            error.clear();
            QVERIFY(!writeEnabled(path, false, &error));
            QVERIFY(error.contains(QLatin1String("not a JSON object")));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), bad);
        }
        // A directory the user cannot write: QSaveFile has nowhere to put its
        // temporary file; the original stays as it was.
        if (::geteuid() == 0)
            QSKIP("root ignores directory permissions");
        const QByteArray json = "{\"title\": \"T\"}";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(json);
        }
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        error.clear();
        const bool written = writeEnabled(path, false, &error);
        QVERIFY(QFile::setPermissions(directory.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY(!written);
        QVERIFY(error.contains(QLatin1String("cannot write")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), json);
    }
};

QTEST_GUILESS_MAIN(TestCustomMetric)
#include "tst_custommetric.moc"
