#include "custommetrics/commandmetric.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::custommetrics;

namespace {

QByteArray configuration(const QString &executable)
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("temperature")},
        {QStringLiteral("title"), QStringLiteral("Temperature")},
        {QStringLiteral("symbol"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("theme")},
             {QStringLiteral("value"), QStringLiteral("temperature")}}},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("command")},
             {QStringLiteral("executable"), executable},
             {QStringLiteral("arguments"), QJsonArray{QStringLiteral("value"), QStringLiteral("23.4")}},
             {QStringLiteral("intervalSeconds"), 5},
             {QStringLiteral("timeoutSeconds"), 2}}},
        {QStringLiteral("display"), QJsonObject{
             {QStringLiteral("prefix"), QStringLiteral("~")},
             {QStringLiteral("suffix"), QStringLiteral(" °C")},
             {QStringLiteral("normalizedRange"), QJsonObject{
                  {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 100.0}}}}}
    }).toJson(QJsonDocument::Compact);
}

} // namespace

class TestCommandMetric : public QObject
{
    Q_OBJECT

private slots:
    void parsesSmallCommandSchemaAndResolvesRelativePaths()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("package/metric.json"));
        const CommandParseResult result = parseCommandJson(configuration(QStringLiteral("./metric-helper")), path);
        QVERIFY2(result.definition, qPrintable(result.error));
        QVERIFY(result.recognized);
        const CommandDefinition &definition = *result.definition;
        QCOMPARE(definition.id, QStringLiteral("temperature"));
        QCOMPARE(definition.title, QStringLiteral("Temperature"));
        QCOMPARE(definition.executable, QDir::cleanPath(temporary.filePath(QStringLiteral("package/metric-helper"))));
        QCOMPARE(definition.workingDirectory, QDir::cleanPath(temporary.filePath(QStringLiteral("package"))));
        QCOMPARE(definition.arguments, (QStringList{QStringLiteral("value"), QStringLiteral("23.4")}));
        QCOMPARE(definition.intervalSeconds, 5);
        QCOMPARE(definition.timeoutSeconds, 2);
        QVERIFY(definition.icon);
        QCOMPARE(definition.icon->type, IconType::Theme);
        QCOMPARE(definition.icon->resolvedValue, QStringLiteral("temperature"));
        QVERIFY(definition.normalizedRange);
    }

    void resolvesAbsoluteAndRelativeFileSymbols()
    {
        QTemporaryDir temporary;
        const QString configPath = temporary.filePath(QStringLiteral("package/metric.json"));
        QJsonDocument document = QJsonDocument::fromJson(configuration(QStringLiteral("/bin/true")));
        QJsonObject object = document.object();
        object.insert(QStringLiteral("symbol"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("file")},
            {QStringLiteral("value"), QStringLiteral("./icons/value.svg")}});
        auto result = parseCommandJson(QJsonDocument(object).toJson(), configPath);
        QVERIFY2(result.definition, qPrintable(result.error));
        QCOMPARE(result.definition->icon->resolvedValue,
                 QDir::cleanPath(temporary.filePath(QStringLiteral("package/icons/value.svg"))));

        const QString absolute = temporary.filePath(QStringLiteral("absolute.svg"));
        object[QStringLiteral("symbol")] = QJsonObject{
            {QStringLiteral("type"), QStringLiteral("file")},
            {QStringLiteral("value"), absolute}};
        result = parseCommandJson(QJsonDocument(object).toJson(), configPath);
        QVERIFY(result.definition);
        QCOMPARE(result.definition->icon->resolvedValue, QDir::cleanPath(absolute));
    }

    void formatsScalarAndNormalizedValue()
    {
        const auto parsed = parseCommandJson(configuration(QStringLiteral("/bin/true")),
                                             QStringLiteral("/tmp/metric.json"));
        QVERIFY(parsed.definition);
        const ScalarResult scalar = snapshotFromCommandOutput(*parsed.definition, QByteArray(" 23.4\n"),
            QDateTime::fromString(QStringLiteral("2026-10-13T12:00:00Z"), Qt::ISODate));
        QVERIFY2(scalar.snapshot, qPrintable(scalar.error));
        QCOMPARE(scalar.snapshot->metrics.first().formattedValue, QString::fromUtf8("~23.4 °C"));
        QCOMPARE(*scalar.snapshot->metricsBarValue, QString::fromUtf8("~23.4 °C"));
        QCOMPARE(*scalar.snapshot->metrics.first().normalizedValue, 0.234);
    }

    void rejectsMalformedScalarOutput_data()
    {
        QTest::addColumn<QByteArray>("output");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << QByteArray(" \n") << QStringLiteral("empty");
        QTest::newRow("multiline") << QByteArray("one\ntwo\n") << QStringLiteral("one scalar line");
        QTest::newRow("invalid utf8") << QByteArray("\xff", 1) << QStringLiteral("UTF-8");
        QTest::newRow("not numeric") << QByteArray("warm\n") << QStringLiteral("numeric");
    }

    void rejectsMalformedScalarOutput()
    {
        QFETCH(QByteArray, output);
        QFETCH(QString, message);
        const auto parsed = parseCommandJson(configuration(QStringLiteral("/bin/true")),
                                             QStringLiteral("/tmp/metric.json"));
        const ScalarResult scalar = snapshotFromCommandOutput(*parsed.definition, output);
        QVERIFY(!scalar.snapshot);
        QVERIFY2(scalar.error.contains(message), qPrintable(scalar.error));
    }

    void recognizesInvalidCommandWithoutFallingBackToSnapshot()
    {
        const QByteArray invalid = R"({"id":"bad","title":"Bad","source":{"type":"command","intervalSeconds":0}})";
        const CommandParseResult result = parseCommandJson(invalid, QStringLiteral("/tmp/metric.json"));
        QVERIFY(result.recognized);
        QVERIFY(!result.definition);
        QVERIFY(!result.error.isEmpty());

        const CommandParseResult snapshot = parseCommandJson(
            R"({"title":"Old","metrics":[],"lastUpdatedDate":"2026-01-01T00:00:00Z"})",
            QStringLiteral("/tmp/snapshot.json"));
        QVERIFY(!snapshot.recognized);
    }
};

QTEST_GUILESS_MAIN(TestCommandMetric)
#include "tst_commandmetric.moc"
