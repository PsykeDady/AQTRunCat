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
};

QTEST_MAIN(TestMetricsBarPresenter)
#include "tst_metricsbarpresenter.moc"
