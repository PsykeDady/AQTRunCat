// v2F1.5: Custom Metric groups — inheritance (deep merge, arrays replaced,
// path origins), child ids, discovery limits, and the service: the group
// never runs, every child runs on its own with its own result, enabled
// inheritance, errors, malformed files.
#include "app/appsettings.h"
#include "app/custommetricsservice.h"
#include "custommetrics/commandmetric.h"
#include "custommetrics/metricgroup.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

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
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

void writeJson(const QString &path, const QJsonObject &object)
{
    writeFile(path, QJsonDocument(object).toJson());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The dice group of the roadmap: the parent owns the program (copied next to
// it as ./random-metric), its icon and the schedule; each child sets its
// title, arguments (a number) and display.
QString writeDice(const QString &discovery, bool groupEnabled = true)
{
    const QString group = discovery + QStringLiteral("/dice-metrics");
    QDir().mkpath(group);
    QFile::copy(helperPath(), group + QStringLiteral("/random-metric"));
    QFile::setPermissions(group + QStringLiteral("/random-metric"),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    writeFile(group + QStringLiteral("/dice.svg"), "<svg xmlns='http://www.w3.org/2000/svg'/>");
    QJsonObject parent{
        {QStringLiteral("id"), QStringLiteral("dice-metrics")},
        {QStringLiteral("title"), QStringLiteral("Dice Metrics")},
        {QStringLiteral("symbol"), QJsonObject{{QStringLiteral("type"), QStringLiteral("file")},
                                               {QStringLiteral("value"), QStringLiteral("./dice.svg")}}},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                               {QStringLiteral("executable"), QStringLiteral("./random-metric")},
                                               {QStringLiteral("arguments"), QJsonArray{QStringLiteral("arguments"), QStringLiteral("parent")}},
                                               {QStringLiteral("intervalSeconds"), 1},
                                               {QStringLiteral("timeoutSeconds"), 3}}},
        {QStringLiteral("display"), QJsonObject{{QStringLiteral("showBar"), false}}}};
    if (!groupEnabled)
        parent.insert(QStringLiteral("enabled"), false);
    writeJson(group + QStringLiteral("/metric.json"), parent);
    for (const int sides : {6, 20, 100}) {
        QJsonObject display{{QStringLiteral("suffix"), QStringLiteral(" d%1").arg(sides)},
                            {QStringLiteral("normalizedRange"), QJsonObject{{QStringLiteral("minimum"), 1}, {QStringLiteral("maximum"), sides}}}};
        if (sides == 20)
            display.insert(QStringLiteral("showBar"), true); // overrides the group's false
        writeJson(group + QStringLiteral("/d%1/metric.json").arg(sides),
                  QJsonObject{{QStringLiteral("title"), QStringLiteral("D%1").arg(sides)},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                     {QStringLiteral("arguments"), QJsonArray{QStringLiteral("arguments"), sides}}}},
                              {QStringLiteral("display"), display}});
    }
    return group;
}

const Source *byConfiguredId(const CustomMetricsService &service, const QString &id)
{
    for (const Source &source : service.sources()) {
        if (source.configuredId == id)
            return &source;
    }
    return nullptr;
}

} // namespace

class TestMetricGroups : public QObject
{
    Q_OBJECT

private slots:
    void deepMergeReplacesArraysAndScalars()
    {
        const QJsonObject base{
            {QStringLiteral("title"), QStringLiteral("Parent")},
            {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                   {QStringLiteral("executable"), QStringLiteral("./run")},
                                                   {QStringLiteral("arguments"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")}},
                                                   {QStringLiteral("intervalSeconds"), 5}}},
            {QStringLiteral("display"), QJsonObject{{QStringLiteral("normalizedRange"),
                                                     QJsonObject{{QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10}}}}}};
        const QJsonObject child{
            {QStringLiteral("title"), QStringLiteral("Child")},
            {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                   {QStringLiteral("arguments"), QJsonArray{QStringLiteral("c")}}}},
            {QStringLiteral("display"), QJsonObject{{QStringLiteral("normalizedRange"), QJsonObject{{QStringLiteral("maximum"), 20}}}}}};
        const QJsonObject merged = deepMerge(base, child);
        QCOMPARE(merged.value(QStringLiteral("title")).toString(), QStringLiteral("Child"));
        const QJsonObject source = merged.value(QStringLiteral("source")).toObject();
        QCOMPARE(source.value(QStringLiteral("type")).toString(), QStringLiteral("command"));
        QCOMPARE(source.value(QStringLiteral("executable")).toString(), QStringLiteral("./run")); // inherited
        QCOMPARE(source.value(QStringLiteral("intervalSeconds")).toInt(), 5);
        QCOMPARE(source.value(QStringLiteral("arguments")).toArray(), QJsonArray{QStringLiteral("c")}); // replaced, not concatenated
        const QJsonObject range = merged.value(QStringLiteral("display")).toObject().value(QStringLiteral("normalizedRange")).toObject();
        QCOMPARE(range.value(QStringLiteral("minimum")).toInt(), 0);  // nested objects merged
        QCOMPARE(range.value(QStringLiteral("maximum")).toInt(), 20);
    }

    void childIds()
    {
        QString id, error;
        QVERIFY(childId(QJsonObject{}, QStringLiteral("/m/dice/d20"), id, error));
        QCOMPARE(id, QStringLiteral("d20"));
        QVERIFY(childId(QJsonObject{{QStringLiteral("id"), QStringLiteral("twenty")}}, QStringLiteral("/m/dice/d20"), id, error));
        QCOMPARE(id, QStringLiteral("twenty"));
        QVERIFY(!childId(QJsonObject{}, QStringLiteral("/m/dice/d 20"), id, error));
        QVERIFY(error.contains(QLatin1String("set \"id\"")));
        QVERIFY(!childId(QJsonObject{{QStringLiteral("id"), QStringLiteral("a/b")}}, QStringLiteral("/m/x"), id, error));
    }

    // Each relative path resolves against the file that declared it.
    void pathsKeepTheirOrigin()
    {
        const QJsonObject group{
            {QStringLiteral("id"), QStringLiteral("g")}, {QStringLiteral("title"), QStringLiteral("G")},
            {QStringLiteral("symbol"), QJsonObject{{QStringLiteral("type"), QStringLiteral("file")}, {QStringLiteral("value"), QStringLiteral("./icon.svg")}}},
            {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                   {QStringLiteral("executable"), QStringLiteral("./random-metric.sh")}}}};
        const Inheritance root = rootInheritance(group, QStringLiteral("/m/dice"), QStringLiteral("g"), true);
        // Inherited: the group's directory, not the child's.
        QJsonObject child{{QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}}}};
        QJsonObject effective = effectiveChild(root, child, QStringLiteral("/m/dice/d20"), QStringLiteral("d20"), true);
        QCOMPARE(effective.value(QStringLiteral("source")).toObject().value(QStringLiteral("executable")).toString(),
                 QStringLiteral("/m/dice/random-metric.sh"));
        QCOMPARE(effective.value(QStringLiteral("symbol")).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("/m/dice/icon.svg"));
        QCOMPARE(effective.value(QStringLiteral("id")).toString(), QStringLiteral("d20")); // never the parent's id
        // Overridden: the child's own directory, ../ allowed.
        child = QJsonObject{{QStringLiteral("symbol"), QJsonObject{{QStringLiteral("value"), QStringLiteral("own.svg")}}},
                            {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                   {QStringLiteral("executable"), QStringLiteral("../other-script.sh")}}}};
        effective = effectiveChild(root, child, QStringLiteral("/m/dice/d20"), QStringLiteral("d20"), true);
        QCOMPARE(effective.value(QStringLiteral("source")).toObject().value(QStringLiteral("executable")).toString(),
                 QStringLiteral("/m/dice/other-script.sh"));
        QCOMPARE(effective.value(QStringLiteral("symbol")).toObject().value(QStringLiteral("value")).toString(),
                 QStringLiteral("/m/dice/d20/own.svg"));
        QCOMPARE(effective.value(QStringLiteral("symbol")).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("file")); // the type still inherited
        // Through a nested group the origin is kept as well.
        const QJsonObject nested{{QStringLiteral("title"), QStringLiteral("Nested")}};
        const Inheritance middle = childInheritance(root, nested, QStringLiteral("/m/dice/more"), QStringLiteral("more"), true);
        QCOMPARE(middle.idPrefix, QStringLiteral("g/more"));
        effective = effectiveChild(middle, QJsonObject{{QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}}}},
                                   QStringLiteral("/m/dice/more/d4"), QStringLiteral("d4"), true);
        QCOMPARE(effective.value(QStringLiteral("source")).toObject().value(QStringLiteral("executable")).toString(),
                 QStringLiteral("/m/dice/random-metric.sh"));
    }

    // The whole group through the service: the container never runs, each
    // child runs with its own arguments and has its own value and ids;
    // icon, schedule and showBar inherited or overridden.
    void diceGroupRunsEachChild()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString group = writeDice(discovery);
        writeFile(group + QStringLiteral("/notes/readme.txt"), "not a child"); // unrelated subdirectory
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QCOMPARE(service.sources().size(), 4); // the group and three children, no "notes"
        const Source &container = service.sources().first();
        QCOMPARE(container.type, SourceType::Group);
        QCOMPARE(container.configuredId, QStringLiteral("dice-metrics"));
        QCOMPARE(container.snapshot->title, QStringLiteral("Dice Metrics"));
        QCOMPARE(container.snapshot->icon->resolvedValue, QDir::cleanPath(group + QStringLiteral("/dice.svg")));
        QTRY_VERIFY_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("dice-metrics/d100"))
                                     && byConfiguredId(service, QStringLiteral("dice-metrics/d100"))->state == State::Available, 3000);
        for (const int sides : {6, 20, 100}) {
            const Source *child = byConfiguredId(service, QStringLiteral("dice-metrics/d%1").arg(sides));
            QVERIFY(child);
            QTRY_COMPARE_WITH_TIMEOUT(child->state, State::Available, 3000);
            child = byConfiguredId(service, QStringLiteral("dice-metrics/d%1").arg(sides));
            QCOMPARE(child->type, SourceType::Command);
            QCOMPARE(child->parentId, container.id);
            QCOMPARE(child->id, QFileInfo(group + QStringLiteral("/d%1/metric.json").arg(sides)).canonicalFilePath());
            QCOMPARE(child->snapshot->title, QStringLiteral("D%1").arg(sides));
            // Its own arguments (the number as text), the parent's program.
            QCOMPARE(child->snapshot->metrics.first().formattedValue, QStringLiteral("%1 d%1").arg(sides));
            QCOMPARE(child->snapshot->icon->resolvedValue, QDir::cleanPath(group + QStringLiteral("/dice.svg")));
            QCOMPARE(child->snapshot->showBar, sides == 20);
            QCOMPARE(*child->snapshot->metrics.first().normalizedValue, 1.0);
        }
        QCOMPARE(service.sources().first().state, State::Available); // the container, never run
        QVERIFY(service.sources().first().snapshot->metrics.isEmpty());
        service.stop();
    }

    void groupInsidePlainContainerKeepsGroupSemantics()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString collection = discovery + QStringLiteral("/ExampleMetrics");
        const QString group = writeDice(collection);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();

        QCOMPARE(service.sources().size(), 4);
        QCOMPARE(service.sources().first().path, group + QStringLiteral("/metric.json"));
        QCOMPARE(service.sources().first().type, SourceType::Group);
        QCOMPARE(service.sources().first().configuredId, QStringLiteral("dice-metrics"));
        for (const int sides : {6, 20, 100})
            QVERIFY(byConfiguredId(service, QStringLiteral("dice-metrics/d%1").arg(sides)));
        service.stop();
    }

    // enabled: a disabled group disables every child (even enabled: true),
    // its files untouched; enabling it again keeps a child's own false.
    void enabledInheritance()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString group = writeDice(discovery, false);
        QJsonObject d6 = QJsonDocument::fromJson(readFile(group + QStringLiteral("/d6/metric.json"))).object();
        d6.insert(QStringLiteral("enabled"), true);
        writeJson(group + QStringLiteral("/d6/metric.json"), d6);
        QJsonObject d20 = QJsonDocument::fromJson(readFile(group + QStringLiteral("/d20/metric.json"))).object();
        d20.insert(QStringLiteral("enabled"), false);
        writeJson(group + QStringLiteral("/d20/metric.json"), d20);
        const QByteArray d6Bytes = readFile(group + QStringLiteral("/d6/metric.json"));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTest::qWait(1500);
        for (const int sides : {6, 20, 100}) {
            const Source *child = byConfiguredId(service, QStringLiteral("dice-metrics/d%1").arg(sides));
            QVERIFY(!child->enabled);
            QVERIFY(child->snapshot->metrics.isEmpty()); // never ran
        }
        QVERIFY(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->configuredEnabled);
        QCOMPARE(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->message, QStringLiteral("Disabled by its group."));
        QVERIFY(!byConfiguredId(service, QStringLiteral("dice-metrics/d20"))->configuredEnabled);
        QVERIFY(!byConfiguredId(service, QStringLiteral("dice-metrics"))->enabled);

        QString error;
        QVERIFY2(service.setEnabled(group + QStringLiteral("/metric.json"), true, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->state, State::Available, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("dice-metrics/d100"))->state, State::Available, 3000);
        QVERIFY(!byConfiguredId(service, QStringLiteral("dice-metrics/d20"))->enabled); // its own false stays
        QCOMPARE(readFile(group + QStringLiteral("/d6/metric.json")), d6Bytes);      // children untouched
        QVERIFY(readFile(group + QStringLiteral("/metric.json")).contains("\"enabled\": true"));

        // A child switched on its own.
        QVERIFY2(service.setEnabled(group + QStringLiteral("/d20/metric.json"), true, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("dice-metrics/d20"))->state, State::Available, 3000);
        service.stop();
    }

    // Children fail on their own: error, timeout and exit 69 side by side,
    // a sibling unaffected; a child without a program is an error.
    void childrenFailIndependently()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString group = discovery + QStringLiteral("/probes");
        writeJson(group + QStringLiteral("/metric.json"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("probes")}, {QStringLiteral("title"), QStringLiteral("Probes")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                                     {QStringLiteral("executable"), helperPath()},
                                                                     {QStringLiteral("timeoutSeconds"), 1}}}});
        auto child = [&](const QString &name, const QJsonArray &arguments) {
            writeJson(group + u'/' + name + QStringLiteral("/metric.json"),
                      QJsonObject{{QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")},
                                                                         {QStringLiteral("arguments"), arguments}}}});
        };
        child(QStringLiteral("ok"), {QStringLiteral("value"), QStringLiteral("5")});
        child(QStringLiteral("fails"), {QStringLiteral("exit"), QStringLiteral("3"), QStringLiteral("boom")});
        child(QStringLiteral("gone"), {QStringLiteral("exit"), QStringLiteral("69"), QStringLiteral("not here")});
        child(QStringLiteral("slow"), {QStringLiteral("sleep"), QStringLiteral("3000"), QStringLiteral("late")});
        // A nested group without a program anywhere: its child is an error.
        writeJson(group + QStringLiteral("/../lonely/metric.json"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("lonely")}, {QStringLiteral("title"), QStringLiteral("Lonely")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")}}}});
        writeJson(discovery + QStringLiteral("/lonely/one/metric.json"),
                  QJsonObject{{QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}}}});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("probes/ok"))->state, State::Available, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("probes/fails"))->state, State::Error, 3000);
        QCOMPARE(byConfiguredId(service, QStringLiteral("probes/fails"))->message, QStringLiteral("Command exited with status 3. boom"));
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("probes/gone"))->state, State::Unavailable, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("probes/slow"))->state, State::Error, 4000);
        QCOMPARE(byConfiguredId(service, QStringLiteral("probes/slow"))->message, QStringLiteral("Command timed out after 1 seconds."));
        QCOMPARE(byConfiguredId(service, QStringLiteral("probes/ok"))->state, State::Available);
        QCOMPARE(byConfiguredId(service, QStringLiteral("lonely/one"))->state, State::Error);
        QVERIFY(byConfiguredId(service, QStringLiteral("lonely/one"))->message.contains(QLatin1String("executable")));
        service.stop();
    }

    void malformedFilesAndDuplicates()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        const QString group = writeDice(discovery);
        writeFile(group + QStringLiteral("/d6/metric.json"), "{ not json");                   // malformed child
        writeJson(group + QStringLiteral("/d100/metric.json"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("d20")},                    // duplicate of d20
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("command")}}}});
        writeJson(group + QStringLiteral("/snap/metric.json"),                                   // not a command
                  QJsonObject{{QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")}}}});
        writeFile(discovery + QStringLiteral("/broken/metric.json"), "{\"source\": {\"type\": \"group\"}}"); // no id
        QDir().mkpath(discovery + QStringLiteral("/broken/child"));
        writeJson(discovery + QStringLiteral("/broken/child/metric.json"), QJsonObject{});
        writeJson(discovery + QStringLiteral("/empty/metric.json"),                                 // no children
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("empty")}, {QStringLiteral("title"), QStringLiteral("Empty")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")}}}});
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        const Source *broken = nullptr;
        for (const Source &source : service.sources()) {
            if (source.path.endsWith(QLatin1String("/broken/metric.json")))
                broken = &source;
            QVERIFY(!source.path.endsWith(QLatin1String("/broken/child/metric.json"))); // not expanded
        }
        QVERIFY(broken);
        QCOMPARE(broken->type, SourceType::Group);
        QCOMPARE(broken->state, State::Error);
        QVERIFY(broken->message.contains(QLatin1String("\"id\"")));
        const Source *empty = byConfiguredId(service, QStringLiteral("empty"));
        QVERIFY(empty && empty->type == SourceType::Group && empty->state == State::Available);
        QCOMPARE(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->state, State::Error);
        QVERIFY(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->message.contains(QLatin1String("invalid JSON")));
        // Two children resolve to "d20": the first in natural order (d20)
        // keeps it, the later one (d100, with that explicit id) is an error.
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const Source &source : service.sources()) {
                if (source.configuredId == QLatin1String("dice-metrics/d20") && source.state == State::Available)
                    return source.path.endsWith(QLatin1String("/d20/metric.json"));
            }
            return false;
        }(), 3000);
        int duplicates = 0;
        for (const Source &source : service.sources())
            duplicates += source.message.contains(QLatin1String("Duplicate id \"dice-metrics/d20\""));
        QCOMPARE(duplicates, 1);
        QVERIFY(byConfiguredId(service, QStringLiteral("dice-metrics/snap"))->message.contains(QLatin1String("must be a command")));
        service.stop();
    }

    // Nesting is bounded and a symlink back to an ancestor is not followed.
    void recursionIsBounded()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        QString directory = discovery + QStringLiteral("/deep");
        writeJson(directory + QStringLiteral("/metric.json"),
                  QJsonObject{{QStringLiteral("id"), QStringLiteral("deep")}, {QStringLiteral("title"), QStringLiteral("Deep")},
                              {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("group")},
                                                                     {QStringLiteral("executable"), helperPath()}}}});
        for (int level = 1; level <= MaxGroupDepth + 2; ++level) {
            directory += QStringLiteral("/l%1").arg(level);
            writeJson(directory + QStringLiteral("/metric.json"), QJsonObject{{QStringLiteral("title"), QStringLiteral("L%1").arg(level)}});
        }
        QVERIFY(QFile::link(discovery + QStringLiteral("/deep"), discovery + QStringLiteral("/deep/l1/loop")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        int groups = 0;
        bool tooDeep = false;
        for (const Source &source : service.sources()) {
            groups += source.type == SourceType::Group;
            tooDeep = tooDeep || source.message == QStringLiteral("Groups are nested too deeply.");
            QVERIFY(!source.path.contains(QLatin1String("/loop/")));
        }
        QVERIFY(tooDeep);
        QCOMPARE(groups, MaxGroupDepth + 1); // the top group and the levels it may nest
        service.stop();
    }

    void groupRegistrationRules()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        QDir().mkpath(discovery);
        const QString elsewhere = temporary.filePath(QStringLiteral("elsewhere"));
        const QString group = writeDice(elsewhere);
        writeFile(elsewhere + QStringLiteral("/group.json"), readFile(group + QStringLiteral("/metric.json")));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        QString error;
        QVERIFY(!service.registerFile(elsewhere + QStringLiteral("/group.json"), &error));
        QVERIFY(error.contains(QLatin1String("package's metric.json")));
        QVERIFY2(service.registerFile(group + QStringLiteral("/metric.json"), &error), qPrintable(error));
        QCOMPARE(service.sources().size(), 4);
        QVERIFY(service.sources().first().explicitlyRegistered);
        QTRY_VERIFY_WITH_TIMEOUT(byConfiguredId(service, QStringLiteral("dice-metrics/d6"))->state == State::Available, 3000);
        service.stop();
    }
};

QTEST_GUILESS_MAIN(TestMetricGroups)
#include "tst_metricgroups.moc"
