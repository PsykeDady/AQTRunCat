#include "app/metricsbarmodel.h"

#include <QSignalSpy>
#include <QTest>

using namespace aqtruncat::metricsbar;
using aqtruncat::metrics::MetricStatus;
using aqtruncat::metrics::SystemMetricsSnapshot;
namespace cm = aqtruncat::custommetrics;

namespace {

SystemMetricsSnapshot snapshot(double cpu = 23.4)
{
    SystemMetricsSnapshot s;
    s.cpu.status = MetricStatus::okStatus();
    s.cpu.usagePercent = cpu;
    s.memory.status = MetricStatus::okStatus();
    aqtruncat::metrics::MemoryInfo memory;
    memory.totalBytes = 16ull << 30;
    memory.usedBytes = 4ull << 30;
    memory.usagePercent = 25.0;
    memory.swapPresent = false;
    s.memory.info = memory;
    s.network.status = MetricStatus::okStatus();
    s.network.downloadBytesPerSecond = 1.24 * 1024 * 1024;
    s.network.uploadBytesPerSecond = 850.0 * 1024;
    s.diskIo.status = MetricStatus::pending("needs another sample");
    aqtruncat::metrics::StorageMetrics root;
    root.path = "/";
    root.status = MetricStatus::okStatus();
    aqtruncat::metrics::StorageInfo info;
    info.usagePercent = 61.6;
    info.availableBytes = 100ull << 30;
    root.info = info;
    s.storage.push_back(root);
    return s;
}

cm::Source source(const QString &id, std::optional<QString> barValue = QStringLiteral("5.4%"),
                  cm::State state = cm::State::Available)
{
    cm::Source result;
    result.id = id;
    result.path = id;
    result.state = state;
    cm::Snapshot snap;
    snap.title = QStringLiteral("Claude Code");
    snap.symbol = QStringLiteral("staroflife");
    snap.metricsBarValue = barValue;
    snap.metrics.append({QStringLiteral("Context"), QStringLiteral("5.4% of 200k"), 0.054});
    snap.lastUpdatedDate = QDateTime(QDate(2026, 6, 5), QTime(4, 50, 40), QTimeZone::UTC);
    result.snapshot = snap;
    return result;
}

QStringList ids(const QList<Item> &items)
{
    QStringList result;
    for (const Item &item : items)
        result.append(item.id);
    return result;
}

const Item *find(const QList<Item> &items, const QString &id)
{
    for (const Item &item : items)
        if (item.id == id)
            return &item;
    return nullptr;
}

} // namespace

class TestMetricsBarModel : public QObject
{
    Q_OBJECT

private slots:
    void stableBuiltInIds()
    {
        QStringList all;
        for (const BuiltInMetric &metric : builtInMetrics())
            all.append(metric.id);
        QCOMPARE(all, (QStringList{"cpu", "memory", "swap", "network.download", "network.upload",
                                   "storage.root", "storage.home", "disk.read", "disk.write"}));
        QVERIFY(isBuiltInId(QStringLiteral("cpu")));
        QVERIFY(!isBuiltInId(QStringLiteral("battery")));
        QCOMPARE(customId(QStringLiteral("/a/b.json")), QStringLiteral("custom:/a/b.json"));
        QCOMPARE(sourceIdFromCustomId(QStringLiteral("custom:/a/b.json")), QStringLiteral("/a/b.json"));
        QVERIFY(!isCustomId(QStringLiteral("custom:")));
    }

    void disabledOrEmptySelectionHasNoItems()
    {
        MetricsBarModel model("/home/test");
        QSignalSpy changed(&model, &MetricsBarModel::itemsChanged);
        model.setConfiguration(false, {QStringLiteral("cpu")});
        model.updateSystem(snapshot());
        QTest::qWait(10);
        QVERIFY(model.items().isEmpty());
        QCOMPARE(changed.count(), 0);
        model.setConfiguration(true, {});
        QTest::qWait(10);
        QVERIFY(model.items().isEmpty());
    }

    void builtInsFollowSnapshotAndOrder()
    {
        MetricsBarModel model("/home/test");
        model.setConfiguration(true, {QStringLiteral("network.upload"), QStringLiteral("cpu"),
                                      QStringLiteral("memory")});
        QTRY_COMPARE(model.items().size(), 3);
        // Before the first snapshot every built-in waits, with no value.
        for (const Item &item : model.items()) {
            QCOMPARE(item.status, Status::Pending);
            QVERIFY(item.shortValue.isEmpty());
        }
        model.updateSystem(snapshot());
        QTRY_COMPARE(model.items().first().status, Status::Available);
        QCOMPARE(ids(model.items()), (QStringList{"network.upload", "cpu", "memory"}));
        const Item *cpu = find(model.items(), QStringLiteral("cpu"));
        QCOMPARE(cpu->shortValue, QStringLiteral("23%"));
        QCOMPARE(cpu->detail, QStringLiteral("23.4 %"));
        QCOMPARE(cpu->shortLabel, QStringLiteral("CPU"));
        QCOMPARE(find(model.items(), QStringLiteral("memory"))->shortValue, QStringLiteral("25%"));
        QCOMPARE(find(model.items(), QStringLiteral("network.upload"))->shortValue, QStringLiteral("850K"));
        model.setConfiguration(true, {QStringLiteral("network.download"), QStringLiteral("storage.root")});
        QTRY_COMPARE(model.items().size(), 2);
        QCOMPARE(find(model.items(), QStringLiteral("network.download"))->shortValue, QStringLiteral("1.2M"));
        QCOMPARE(find(model.items(), QStringLiteral("network.download"))->detail, QStringLiteral("1.2 MiB/s"));
        QCOMPARE(find(model.items(), QStringLiteral("storage.root"))->shortValue, QStringLiteral("62%"));
    }

    void neverMoreThanThreeItems()
    {
        QCOMPARE(MaxItems, 3);
        MetricsBarModel model("/home/test");
        const QString path = QStringLiteral("/data/claude.json");
        // The limit holds even when a caller bypasses AppSettings.
        model.setConfiguration(true, {QStringLiteral("cpu"), QStringLiteral("memory"), customId(path),
                                      QStringLiteral("network.download"), QStringLiteral("network.upload")});
        model.updateSystem(snapshot());
        model.updateCustom({source(path)});
        QTRY_COMPARE(ids(model.items()), (QStringList{"cpu", "memory", customId(path)}));
        // A selected but absent custom source keeps its slot: no fourth item
        // moves up to replace it.
        model.updateCustom({});
        QTRY_COMPARE(ids(model.items()), (QStringList{"cpu", "memory"}));
    }

    void unavailableIsNeverZero()
    {
        MetricsBarModel model("/home/test");
        model.setConfiguration(true, {QStringLiteral("swap"), QStringLiteral("disk.read"), QStringLiteral("cpu")});
        SystemMetricsSnapshot s = snapshot();
        s.cpu.status = MetricStatus::error("cannot parse /proc/stat");
        s.cpu.usagePercent.reset();
        model.updateSystem(s);
        QTRY_COMPARE(model.items().size(), 3);
        const Item *swap = find(model.items(), QStringLiteral("swap"));
        QCOMPARE(swap->status, Status::Unavailable);
        QVERIFY(swap->shortValue.isEmpty());
        const Item *disk = find(model.items(), QStringLiteral("disk.read"));
        QCOMPARE(disk->status, Status::Pending);
        QVERIFY(disk->shortValue.isEmpty());
        const Item *cpu = find(model.items(), QStringLiteral("cpu"));
        QCOMPARE(cpu->status, Status::Error);
        QCOMPARE(cpu->statusMessage, QStringLiteral("cannot parse /proc/stat"));
        QVERIFY(cpu->shortValue.isEmpty());
    }

    void homeOnlyWhenSeparateFilesystem()
    {
        MetricsBarModel model("/home/test");
        model.setConfiguration(true, {QStringLiteral("storage.home"), QStringLiteral("cpu")});
        model.updateSystem(snapshot());
        QTRY_COMPARE(ids(model.items()), QStringList{"cpu"});
        SystemMetricsSnapshot s = snapshot();
        auto home = s.storage.front();
        home.path = "/home/test";
        home.info->usagePercent = 40.0;
        s.storage.push_back(home);
        model.updateSystem(s);
        QTRY_COMPARE(ids(model.items()), (QStringList{"storage.home", "cpu"}));
        QCOMPARE(model.items().first().shortValue, QStringLiteral("40%"));
    }

    void unknownIdsAreIgnored()
    {
        MetricsBarModel model("/home/test");
        model.setConfiguration(true, {QStringLiteral("battery"), QStringLiteral("cpu"), QStringLiteral("gpu.temp")});
        model.updateSystem(snapshot());
        QTRY_COMPARE(ids(model.items()), QStringList{"cpu"});
    }

    void customUsesMetricsBarValueVerbatim()
    {
        MetricsBarModel model("/home/test");
        const QString id = customId(QStringLiteral("/data/claude.json"));
        model.setConfiguration(true, {id, QStringLiteral("cpu")});
        model.updateSystem(snapshot());
        model.updateCustom({source(QStringLiteral("/data/claude.json"), QStringLiteral(" 5.4% ctx"))});
        QTRY_COMPARE(model.items().size(), 2);
        const Item &custom = model.items().first();
        QCOMPARE(custom.id, id);
        QCOMPARE(custom.origin, Origin::Custom);
        QCOMPARE(custom.label, QStringLiteral("Claude Code"));
        QCOMPARE(custom.shortLabel, QStringLiteral("✣"));
        QCOMPARE(custom.shortValue, QStringLiteral(" 5.4% ctx")); // never reinterpreted
        QCOMPARE(custom.status, Status::Available);
        QVERIFY(custom.lastUpdated.isValid());
    }

    void customWithoutMetricsBarValueIsNotEligible()
    {
        MetricsBarModel model("/home/test");
        const QString a = QStringLiteral("/data/a.json");
        const QString b = QStringLiteral("/data/b.json");
        model.setConfiguration(true, {customId(a), customId(b), QStringLiteral("cpu")});
        model.updateSystem(snapshot());
        model.updateCustom({source(a, std::nullopt), source(b, QStringLiteral("   "))});
        QTRY_COMPARE(ids(model.items()), QStringList{"cpu"});
    }

    void pendingCommandIsEligibleBeforeItsFirstValueAndCarriesIcon()
    {
        MetricsBarModel model("/home/test");
        cm::Source command = source(QStringLiteral("/data/package/metric.json"), std::nullopt,
                                    cm::State::Pending);
        command.type = cm::SourceType::Command;
        command.snapshot->icon = cm::IconReference{cm::IconType::Theme,
                                                   QStringLiteral("temperature"),
                                                   QStringLiteral("temperature")};
        const QString id = customId(command.id);
        model.setConfiguration(true, {id});
        model.updateCustom({command});
        QTRY_COMPARE(model.items().size(), 1);
        QCOMPARE(model.items().first().status, Status::Pending);
        QVERIFY(model.items().first().shortValue.isEmpty());
        QVERIFY(model.items().first().icon);
        QCOMPARE(model.items().first().icon->type, cm::IconType::Theme);
    }

    void customStatusesKeepLastValue()
    {
        MetricsBarModel model("/home/test");
        const QString path = QStringLiteral("/data/claude.json");
        model.setConfiguration(true, {customId(path)});
        model.updateCustom({source(path, QStringLiteral("5.4%"), cm::State::Error)});
        QTRY_COMPARE(model.items().size(), 1);
        QCOMPARE(model.items().first().status, Status::Error);
        QCOMPARE(model.items().first().shortValue, QStringLiteral("5.4%"));

        // An explicit file that never loaded is still shown, as Unavailable.
        cm::Source missing;
        missing.id = path;
        missing.path = path;
        missing.state = cm::State::Unavailable;
        missing.message = QStringLiteral("File is not available.");
        model.updateCustom({missing});
        QTRY_COMPARE(model.items().first().status, Status::Unavailable);
        QCOMPARE(model.items().first().label, QStringLiteral("claude"));
    }

    void customDisappearsAndReappears()
    {
        MetricsBarModel model("/home/test");
        const QString path = QStringLiteral("/data/claude.json");
        model.setConfiguration(true, {QStringLiteral("cpu"), customId(path)});
        model.updateSystem(snapshot());
        model.updateCustom({source(path)});
        QTRY_COMPARE(model.items().size(), 2);
        model.updateCustom({});
        QTRY_COMPARE(ids(model.items()), QStringList{"cpu"});
        model.updateCustom({source(path, QStringLiteral("6.0%"))});
        QTRY_COMPARE(model.items().size(), 2);
        QCOMPARE(model.items().last().shortValue, QStringLiteral("6.0%"));
    }

    void changesAreCoalescedAndDeduplicated()
    {
        MetricsBarModel model("/home/test");
        QSignalSpy changed(&model, &MetricsBarModel::itemsChanged);
        model.setConfiguration(true, {QStringLiteral("cpu")});
        model.updateSystem(snapshot(10.0));
        model.updateSystem(snapshot(20.0));
        model.updateCustom({});
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(model.items().first().shortValue, QStringLiteral("20%"));
        model.updateSystem(snapshot(20.0)); // identical: nothing to publish
        QTest::qWait(20);
        QCOMPARE(changed.count(), 1);
        model.updateSystem(snapshot(20.2)); // compact unchanged, detail changed
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(model.items().first().shortValue, QStringLiteral("20%"));
        QCOMPARE(model.items().first().detail, QStringLiteral("20.2 %"));
    }

    void enableDisableAtRuntime()
    {
        MetricsBarModel model("/home/test");
        model.updateSystem(snapshot());
        model.setConfiguration(true, {QStringLiteral("cpu"), QStringLiteral("memory")});
        QTRY_COMPARE(model.items().size(), 2);
        model.setConfiguration(false, {QStringLiteral("cpu"), QStringLiteral("memory")});
        QTRY_VERIFY(model.items().isEmpty());
        model.setConfiguration(true, {QStringLiteral("memory")});
        QTRY_COMPARE(ids(model.items()), QStringList{"memory"});
    }

    void itemsOfAMultiValueSourceAreSelectableOnTheirOwn()
    {
        cm::Source quotas = source(QStringLiteral("/data/claude/metric.json"));
        quotas.snapshot->icon = cm::IconReference{cm::IconType::Theme, QStringLiteral("x"), QStringLiteral("x")};
        cm::Item session{QStringLiteral("Current session (5h)"), QStringLiteral("3% used"), 0.03};
        session.id = QStringLiteral("five_hour");
        session.detail = QStringLiteral("Resets at 13:30");
        session.metricsBarValue = QStringLiteral("3%");
        session.metricsBarLabel = QStringLiteral("5h");
        cm::Item week{QStringLiteral("Current week (all models)"), QStringLiteral("Not reported"), std::nullopt};
        week.id = QStringLiteral("seven_day");
        quotas.snapshot->metrics = {session, week};

        QVERIFY(isCustomItemId(customItemId(quotas.id, QStringLiteral("five_hour"))));
        QCOMPARE(sourceIdFromCustomItemId(customItemId(quotas.id, QStringLiteral("five_hour"))), quotas.id);
        QCOMPARE(itemIdFromCustomItemId(customItemId(QStringLiteral("/a:b/c.json"), QStringLiteral("x"))), QStringLiteral("x"));
        QCOMPARE(sourceIdFromCustomItemId(customItemId(QStringLiteral("/a:b/c.json"), QStringLiteral("x"))),
                 QStringLiteral("/a:b/c.json"));
        QVERIFY(!isCustomItemId(QStringLiteral("custom-item:bad id:/x")));
        QVERIFY(!isCustomItemId(QStringLiteral("custom-item:x:")));
        QVERIFY(isKnownId(customItemId(quotas.id, QStringLiteral("seven_day"))));

        MetricsBarModel model("/home/alice");
        model.setConfiguration(true, {customItemId(quotas.id, QStringLiteral("five_hour")),
                                      customItemId(quotas.id, QStringLiteral("seven_day")),
                                      customItemId(quotas.id, QStringLiteral("gone"))});
        model.updateCustom({quotas});
        QTRY_COMPARE(model.items().size(), 3);
        const Item &first = model.items().at(0);
        QCOMPARE(first.label, QStringLiteral("Claude Code — Current session (5h)"));
        QCOMPARE(first.shortValue, QStringLiteral("3%"));
        QCOMPARE(first.shortLabel, QStringLiteral("5h"));
        QVERIFY(!first.icon); // its own caption instead of the shared icon
        QCOMPARE(first.detail, QStringLiteral("3% used — Resets at 13:30"));
        QCOMPARE(first.status, Status::Available);
        // No compact value: never empty or zero, an explicit "unavailable".
        QCOMPARE(model.items().at(1).status, Status::Unavailable);
        QVERIFY(model.items().at(1).shortValue.isEmpty());
        QCOMPARE(model.items().at(1).statusMessage, QStringLiteral("Not reported"));
        QCOMPARE(model.items().at(2).status, Status::Unavailable);

        // A refresh in progress keeps the last value; an error shows as such.
        quotas.state = cm::State::Pending;
        model.updateCustom({quotas});
        QTRY_COMPARE(model.items().at(0).status, Status::Pending);
        QCOMPARE(model.items().at(0).shortValue, QStringLiteral("3%"));
        quotas.state = cm::State::Error;
        quotas.message = QStringLiteral("boom");
        model.updateCustom({quotas});
        QTRY_COMPARE(model.items().at(0).status, Status::Error);

        // The source disappears: its items leave the bar, the selection stays.
        model.updateCustom({});
        QTRY_VERIFY(model.items().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMetricsBarModel)
#include "tst_metricsbarmodel.moc"
