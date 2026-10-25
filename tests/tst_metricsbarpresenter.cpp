#include "app/metricsbarpresenter.h"
#include "app/traypresenter.h"

#include <QApplication>
#include <QPalette>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using namespace aqtruncat::metricsbar;

namespace {

class FakeTrayItem : public MetricsBarTrayItem
{
public:
    explicit FakeTrayItem(QMenu *menu, QList<FakeTrayItem *> *registry)
        : menu(menu)
        , registry(registry)
    {
        registry->append(this);
    }
    ~FakeTrayItem() override { registry->removeAll(this); }

    void setIcon(const QIcon &icon) override
    {
        lastIcon = icon;
        ++icons;
    }
    void setToolTip(const QString &text) override
    {
        toolTip = text;
        ++toolTips;
    }
    void click() { emit activated(); }
    void middleClick() { emit secondaryActivated(); }

    QMenu *menu = nullptr;
    QList<FakeTrayItem *> *registry = nullptr;
    QIcon lastIcon;
    QString toolTip;
    int icons = 0;
    int toolTips = 0;
};

Item item(const QString &id, const QString &value, Status status = Status::Available)
{
    Item result;
    result.id = id;
    result.label = id.toUpper();
    result.shortLabel = id.left(3).toUpper();
    result.shortValue = value;
    result.detail = value + QStringLiteral(" (detail)");
    result.status = status;
    return result;
}

struct Fixture {
    QList<FakeTrayItem *> trays;
    MetricsBarPresenter presenter{[this](QMenu *menu) { return std::make_unique<FakeTrayItem>(menu, &trays); }};
};

} // namespace

class TestMetricsBarPresenter : public QObject
{
    Q_OBJECT

private slots:
    void createsUpdatesAndRemovesItems()
    {
        Fixture f;
        f.presenter.setItems({item("cpu", "23%"), item("memory", "45%")});
        QCOMPARE(f.trays.size(), 2);
        QCOMPARE(f.presenter.itemIds(), (QStringList{"cpu", "memory"}));
        QCOMPARE(f.trays[0]->icons, 1);
        QVERIFY(!f.trays[0]->lastIcon.isNull());
        QVERIFY(f.trays[0]->toolTip.contains(QStringLiteral("CPU: 23% (detail)")));
        QVERIFY(f.trays[0]->menu && f.trays[0]->menu->actions().size() == 5);
        // The running version, informational only, above Quit.
        const QAction *version = f.trays[0]->menu->actions().at(3);
        QCOMPARE(version->text(), QStringLiteral("AQTRunCat %1").arg(QCoreApplication::applicationVersion()));
        QVERIFY(!version->isEnabled());

        f.presenter.setItems({item("cpu", "24%"), item("memory", "45%")});
        QCOMPARE(f.trays[0]->icons, 2);
        QCOMPARE(f.trays[1]->icons, 1);

        f.presenter.setItems({item("memory", "45%")});
        QCOMPARE(f.trays.size(), 1);
        QCOMPARE(f.presenter.itemIds(), QStringList{"memory"});
        QCOMPARE(f.trays[0]->icons, 1); // kept, not re-created
    }

    void unchangedCompactValueSendsNothing()
    {
        Fixture f;
        Item cpu = item("cpu", "23%");
        cpu.detail = QStringLiteral("23.1 %");
        f.presenter.setItems({cpu});
        QCOMPARE(f.trays[0]->icons, 1);
        QCOMPARE(f.trays[0]->toolTips, 1);
        cpu.detail = QStringLiteral("23.4 %");
        f.presenter.setItems({cpu});
        f.presenter.setItems({cpu});
        QCOMPARE(f.presenter.iconUpdates(), 1);
        QCOMPARE(f.trays[0]->toolTips, 1); // precise value alone: no traffic
        QVERIFY(f.trays[0]->toolTip.startsWith(QStringLiteral("CPU: 23.1 %")));

        // A new compact value refreshes icon and tooltip together.
        cpu.shortValue = QStringLiteral("24%");
        cpu.detail = QStringLiteral("23.6 %");
        f.presenter.setItems({cpu});
        QCOMPARE(f.trays[0]->icons, 2);
        QCOMPARE(f.trays[0]->toolTips, 2);
        QVERIFY(f.trays[0]->toolTip.startsWith(QStringLiteral("CPU: 23.6 %")));

        // A change outside the value (status message) updates only the tooltip.
        cpu.statusMessage = QStringLiteral("note");
        cpu.status = Status::Pending;
        cpu.origin = Origin::Custom; // keeps showing its value while pending
        f.presenter.setItems({cpu});
        QCOMPARE(f.trays[0]->icons, 2);
        QCOMPARE(f.trays[0]->toolTips, 3);
        QVERIFY(f.trays[0]->toolTip.contains(QStringLiteral("Waiting: note")));
    }

    void identicalContentReusesTheSameIcon()
    {
        Fixture f;
        f.presenter.setItems({item("cpu", "23%"), item("memory", "45%")});
        const qint64 first = f.trays[0]->lastIcon.cacheKey();
        f.presenter.setItems({item("cpu", "24%"), item("memory", "45%")});
        QVERIFY(f.trays[0]->lastIcon.cacheKey() != first);
        f.presenter.setItems({item("cpu", "23%"), item("memory", "45%")});
        QCOMPARE(f.trays[0]->icons, 3);
        QCOMPARE(f.trays[0]->lastIcon.cacheKey(), first); // same QIcon, not a new one
        QCOMPARE(f.presenter.iconsRendered(), 3);
    }

    void statusChangesUpdateIconAndToolTip()
    {
        Fixture f;
        f.presenter.setItems({item("cpu", "23%")});
        Item failed = item("cpu", QString(), Status::Error);
        failed.detail.clear();
        failed.statusMessage = QStringLiteral("cannot parse /proc/stat");
        f.presenter.setItems({failed});
        QCOMPARE(f.trays[0]->icons, 2);
        QVERIFY(f.trays[0]->toolTip.contains(QStringLiteral("Error: cannot parse /proc/stat")));
    }

    void reorderRecreatesOnlyAfterFirstDifference()
    {
        Fixture f;
        f.presenter.setItems({item("a", "1"), item("b", "2"), item("c", "3")});
        QCOMPARE(f.presenter.itemsCreated(), 3);
        QPointer<FakeTrayItem> first = f.trays[0];
        f.presenter.setItems({item("a", "1"), item("c", "3"), item("b", "2")});
        QCOMPARE(f.presenter.itemIds(), (QStringList{"a", "c", "b"}));
        QCOMPARE(f.presenter.itemsCreated(), 5);
        QVERIFY(first); // the unchanged prefix survives
        // Appending never re-creates existing items.
        f.presenter.setItems({item("a", "1"), item("c", "3"), item("b", "2"), item("d", "4")});
        QCOMPARE(f.presenter.itemsCreated(), 6);
    }

    void disableAndShutdownLeaveNoItems()
    {
        QList<FakeTrayItem *> trays;
        auto presenter = std::make_unique<MetricsBarPresenter>(
            [&trays](QMenu *menu) { return std::make_unique<FakeTrayItem>(menu, &trays); });
        presenter->setItems({item("cpu", "23%"), item("memory", "45%")});
        presenter->setItems({}); // bar disabled at runtime
        QVERIFY(trays.isEmpty());
        presenter->setItems({item("cpu", "23%")});
        QCOMPARE(trays.size(), 1);
        presenter->clear();
        QVERIFY(trays.isEmpty());
        presenter->setItems({item("cpu", "23%"), item("memory", "45%")});
        presenter.reset(); // application quit
        QVERIFY(trays.isEmpty());
    }

    void clickAndMenuRequestApplicationActions()
    {
        Fixture f;
        QSignalSpy dashboard(&f.presenter, &MetricsBarPresenter::dashboardToggleRequested);
        QSignalSpy settings(&f.presenter, &MetricsBarPresenter::settingsRequested);
        f.presenter.setItems({item("cpu", "23%"), item("memory", "45%")});
        f.trays[1]->click();
        QCOMPARE(dashboard.count(), 1);
        const QList<QAction *> actions = f.trays[0]->menu->actions();
        QCOMPARE(actions[0]->text(), QStringLiteral("Show Dashboard"));
        f.presenter.setDashboardVisible(true);
        QCOMPARE(actions[0]->text(), QStringLiteral("Hide Dashboard"));
        QCOMPARE(f.trays[1]->menu->actions()[0]->text(), QStringLiteral("Hide Dashboard"));
        actions[1]->trigger();
        QCOMPARE(settings.count(), 1);
        actions[0]->trigger();
        QCOMPARE(dashboard.count(), 2);
    }

    // v2F1.7: a Custom Metric's item refreshes on a middle click or its
    // menu's first action; the left click still toggles the Dashboard and
    // built-in items have no refresh at all.
    void middleClickAndMenuRefreshCustomItems()
    {
        Fixture f;
        QSignalSpy refresh(&f.presenter, &MetricsBarPresenter::refreshRequested);
        QSignalSpy dashboard(&f.presenter, &MetricsBarPresenter::dashboardToggleRequested);
        f.presenter.setItems({item(QStringLiteral("cpu"), QStringLiteral("23%")),
                              item(QStringLiteral("custom:/metrics/a.json"), QStringLiteral("7"))});
        f.trays[0]->middleClick();
        QCOMPARE(refresh.count(), 0);
        QCOMPARE(f.trays[0]->menu->actions()[0]->text(), QStringLiteral("Show Dashboard"));

        f.trays[1]->middleClick();
        QCOMPARE(refresh.count(), 1);
        QCOMPARE(refresh.at(0).at(0).toString(), QStringLiteral("/metrics/a.json"));
        QCOMPARE(dashboard.count(), 0);
        QAction *action = f.trays[1]->menu->actions()[0];
        QCOMPARE(action->text(), QStringLiteral("Refresh Now"));
        action->trigger();
        QCOMPARE(refresh.count(), 2);
        QCOMPARE(refresh.at(1).at(0).toString(), QStringLiteral("/metrics/a.json"));
        f.trays[1]->click();
        QCOMPARE(dashboard.count(), 1);
        QCOMPARE(refresh.count(), 2);
    }

    void runnerMenuShowsTheVersion()
    {
        QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
        TrayPresenter tray;
        const QList<QAction *> actions = tray.menu().actions();
        QCOMPARE(actions.size(), 5); // Dashboard, Settings, separator, version, Quit
        QCOMPARE(actions[3]->text(), QStringLiteral("AQTRunCat 1.2.3"));
        QVERIFY(!actions[3]->isEnabled());
        QCOMPARE(actions[4]->text(), QStringLiteral("Quit AQTRunCat"));
        QCoreApplication::setApplicationVersion(QString());
    }

    void paletteChangeRerendersEveryItem()
    {
        Fixture f;
        f.presenter.setItems({item("cpu", "23%"), item("memory", "45%")});
        const QPalette original = QApplication::palette();
        QPalette dark = original;
        dark.setColor(QPalette::WindowText, QColor(0xfc, 0xfc, 0xfc));
        QApplication::setPalette(dark);
        QTRY_COMPARE(f.trays[0]->icons, 2);
        QCOMPARE(f.trays[1]->icons, 2);
        QApplication::setPalette(original);
    }

    void toolTipDescribesCustomSources()
    {
        Item custom = item("custom:/data/claude.json", "5.4%", Status::Unavailable);
        custom.origin = Origin::Custom;
        custom.label = QStringLiteral("Claude Code");
        custom.detail = QStringLiteral("5.4%");
        custom.statusMessage = QStringLiteral("File is not available.");
        custom.lastUpdated = QDateTime(QDate(2026, 6, 5), QTime(4, 50, 40), QTimeZone::UTC);
        const QString text = MetricsBarPresenter::toolTipFor(custom);
        QVERIFY(text.startsWith(QStringLiteral("Claude Code: 5.4%")));
        QVERIFY(text.contains(QStringLiteral("Unavailable: File is not available.")));
        QVERIFY(text.contains(QStringLiteral("Last updated:")));
        QVERIFY(text.endsWith(QStringLiteral("AQTRunCat Metrics Bar")));
    }

    // F6: slowly changing hover details sit under the value and refresh the
    // tooltip when they change; a value change alone still sends nothing.
    void toolTipShowsContextLines()
    {
        Fixture f;
        Item cpu = item("cpu", "23%");
        cpu.detail = QStringLiteral("23.1 %");
        cpu.context = QStringLiteral("8 logical CPUs");
        f.presenter.setItems({cpu});
        QCOMPARE(f.trays[0]->toolTip, QStringLiteral("CPU: 23.1 %\n8 logical CPUs\nAQTRunCat Metrics Bar"));
        const int updates = f.presenter.toolTipUpdates();

        cpu.detail = QStringLiteral("23.4 %");
        f.presenter.setItems({cpu});
        QCOMPARE(f.presenter.toolTipUpdates(), updates);

        Item packages = item("updates.packages", "3");
        packages.detail = QStringLiteral("3 updates");
        packages.context = QStringLiteral("Package manager: pacman\nLast successful check: 14:05");
        f.presenter.setItems({cpu, packages});
        QVERIFY(f.trays[1]->toolTip.contains(QStringLiteral("Package manager: pacman\nLast successful check: 14:05")));
        const int afterPackages = f.presenter.toolTipUpdates();
        packages.context = QStringLiteral("Package manager: pacman\nLast successful check: 15:05");
        f.presenter.setItems({cpu, packages});
        QCOMPARE(f.presenter.toolTipUpdates(), afterPackages + 1);
        QVERIFY(f.trays[1]->toolTip.contains(QStringLiteral("15:05")));
    }

    // F6.1: the runner item's tooltip carries the hover summary.
    void runnerToolTipCarriesTheSummary()
    {
        aqtruncat::Runner runner;
        runner.id = QStringLiteral("cat");
        runner.displayName = QStringLiteral("Running Cat");
        QImage frame(18, 18, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::black);
        runner.frames = {frame, frame};
        runner.sequence = {0, 1};

        TrayPresenter tray;
        tray.setRunner(runner);
        QCOMPARE(tray.toolTip(), QStringLiteral("AQTRunCat — Running Cat")); // at once
        const int start = tray.toolTipUpdates();

        const QStringList lines{QStringLiteral("CPU: 18.4 %"), QStringLiteral("RAM: 46.5 % · 7.2 GiB / 15.5 GiB used")};
        tray.setSummary(lines);
        // Sent with the next frame, not on its own.
        QCOMPARE(tray.toolTipUpdates(), start);
        tray.showFrame(1);
        QCOMPARE(tray.toolTipUpdates(), start + 1);
        QCOMPARE(tray.toolTip(), QStringLiteral("AQTRunCat — Running Cat\nCPU: 18.4 %\nRAM: 46.5 % · 7.2 GiB / 15.5 GiB used"));

        // The same summary again (every snapshot) sends nothing.
        tray.setSummary(lines);
        tray.showFrame(0);
        QCOMPARE(tray.toolTipUpdates(), start + 1);

        // Without frames (animation stopped) it still arrives, a bit later.
        tray.setSummary({QStringLiteral("Backend not running")});
        QTRY_COMPARE(tray.toolTip(), QStringLiteral("AQTRunCat — Running Cat\nBackend not running"));
        QCOMPARE(tray.toolTipUpdates(), start + 2);

        // Menu and activation are unchanged.
        QSignalSpy toggled(&tray, &TrayPresenter::dashboardToggleRequested);
        QCOMPARE(tray.menu().actions().size(), 5);
        tray.menu().actions().first()->trigger();
        QCOMPARE(toggled.count(), 1);
    }
};

QTEST_MAIN(TestMetricsBarPresenter)
#include "tst_metricsbarpresenter.moc"
