#include "app/dashboardwindow.h"
#include "app/custommetricsservice.h"
#include "app/runnerpreviewwidget.h"
#include "app/settingswindow.h"
#include "app/applicationquit.h"
#include "app/metricsbarpresenter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QMenu>
#include <QMessageBox>

#include <unistd.h>

namespace {

aqtruncat::Runner runner(const QString &id, QColor colour = Qt::black, bool monochrome = true)
{
    aqtruncat::Runner result;
    result.id = id;
    result.displayName = id;
    result.monochrome = monochrome;
    result.frameSize = {56, 36};
    for (int i = 0; i < 3; ++i) {
        QImage frame(result.frameSize, QImage::Format_ARGB32_Premultiplied);
        frame.fill(colour);
        result.frames.append(frame);
        result.sequence.append(i);
    }
    return result;
}

class MenuOnlyTrayItem : public MetricsBarTrayItem
{
public:
    void setIcon(const QIcon &) override {}
    void setToolTip(const QString &) override {}
};

template<typename T>
T *child(SettingsWindow &window, const char *name)
{
    T *result = window.findChild<T *>(QLatin1String(name));
    if (!result)
        qFatal("missing settings child %s", name);
    return result;
}

} // namespace

class TestSettingsWindow : public QObject
{
    Q_OBJECT

private slots:
    void immediateApplyLifecycleAndDefaults()
    {
        QApplication::setQuitOnLastWindowClosed(false);
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled},
                                          {QStringLiteral("coffee"), QStringLiteral("Coffee"), aqtruncat::RunnerSource::Bundled}});
        window.setActiveRunner(runner(QStringLiteral("cat")));
        window.setAvailableInterfaces({{"wlan0", aqtruncat::metrics::InterfaceKind::Physical,
                                        false, std::nullopt, std::nullopt, false},
                                       {"tun0", aqtruncat::metrics::InterfaceKind::Virtual,
                                        false, std::nullopt, std::nullopt, false}});
        window.showAndRaise();
        QTRY_VERIFY(window.isVisible());

        auto *combo = child<QComboBox>(window, "runnerCombo");
        combo->setCurrentIndex(combo->findData(QStringLiteral("coffee")));
        QCOMPARE(settings.runnerId(), QStringLiteral("coffee"));
        child<QRadioButton>(window, "slowSpeed")->click();
        QCOMPARE(settings.speedMode(), aqtruncat::SpeedMode::SlowDown);
        child<QRadioButton>(window, "networkManual")->click();
        QCOMPARE(settings.networkMode(), aqtruncat::metrics::NetworkPolicyMode::Manual);
        auto *interfaces = child<QTreeWidget>(window, "networkInterfaces");
        interfaces->topLevelItem(1)->setCheckState(0, Qt::Checked);
        QVERIFY(settings.networkInterfaces().contains(QStringLiteral("tun0")));
        child<QComboBox>(window, "dashboardRefresh")->setCurrentIndex(2);
        QCOMPARE(settings.dashboardRefreshSeconds(), 5);
        child<QCheckBox>(window, "autostart")->click();
        QVERIFY(settings.autostartEnabled());

        window.close();
        QTRY_VERIFY(!window.isVisible());
        window.showAndRaise();
        QTRY_VERIFY(window.isVisible());
        QCOMPARE(settings.runnerId(), QStringLiteral("coffee"));
        child<QPushButton>(window, "restoreDefaults")->click();
        QCOMPARE(settings.runnerId(), QStringLiteral("cat"));
        QCOMPARE(settings.speedMode(), aqtruncat::SpeedMode::Normal);
        QCOMPARE(settings.networkMode(), aqtruncat::metrics::NetworkPolicyMode::Automatic);
        QVERIFY(!settings.autostartEnabled());
    }

    void dashboardAndSettingsAreIndependentWindows()
    {
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}});
        DashboardWindow dashboard("/home/test");
        window.setActiveRunner(runner(QStringLiteral("cat")));
        window.showAndRaise();
        dashboard.toggleVisible();
        QTRY_VERIFY(window.isVisible() && dashboard.isVisible());
        dashboard.close();
        QTRY_VERIFY(!dashboard.isVisible());
        QVERIFY(window.isVisible());
        window.close();
        QTRY_VERIFY(!window.isVisible());
        dashboard.toggleVisible();
        QVERIFY(dashboard.isVisible());
    }

    void customMetricAddRemoveAndDiscoveryAreServiceBacked()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("data/metrics"));
        QDir().mkpath(discovery);
        const QString file = discovery + QStringLiteral("/fixture.json");
        QFile json(file);
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(R"({"title":"Fixture","metrics":[{"title":"Value","formattedValue":"7"}],
                       "lastUpdatedDate":"2026-06-05T04:50:40Z"})");
        json.close();

        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        SettingsWindow window(&settings,
            {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}}, &service);
        auto *tree = child<QTreeWidget>(window, "customMetricFiles");
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Fixture")); // the metric's own title
        QCOMPARE(tree->topLevelItem(0)->text(1), file);
        QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Discovered"));

        QString error;
        QVERIFY2(window.addCustomMetricFile(file, &error), qPrintable(error));
        QTRY_COMPARE(tree->topLevelItemCount(), 1); // same canonical file, no duplicate
        QTRY_COMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Registered + discovered"));
        tree->setCurrentItem(tree->topLevelItem(0));
        service.rescan(); // republished sources (e.g. every command run) keep the selection
        QVERIFY(tree->currentItem());
        QCOMPARE(tree->currentItem()->text(1), file);
        QVERIFY(child<QPushButton>(window, "removeCustomMetric")->isEnabled());
        child<QPushButton>(window, "removeCustomMetric")->click();
        QTRY_COMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Discovered"));
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(settings.customMetricFiles().isEmpty());
    }

    void metricsBarSelectionOrderAndRuntimeApply()
    {
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setMetricsBarItems({QStringLiteral("cpu"), QStringLiteral("custom:/gone/old.json")});
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}});
        auto *enabled = child<QCheckBox>(window, "metricsBarEnabled");
        auto *list = child<QTreeWidget>(window, "metricsBarItems");
        QVERIFY(!enabled->isChecked()); // disabled by default
        auto row = [list](int index) { return list->topLevelItem(index)->data(0, Qt::UserRole).toString(); };
        // Selected first in their order; the stale custom id stays visible and checked.
        QCOMPARE(row(0), QStringLiteral("cpu"));
        QCOMPARE(row(1), QStringLiteral("custom:/gone/old.json"));
        QCOMPARE(list->topLevelItem(1)->text(1), QStringLiteral("Custom — not currently available"));
        QCOMPARE(list->topLevelItemCount(), 2 + 8 + 7); // + the seven F5 entries
        QVERIFY(child<QLabel>(window, "metricsBarCount")->text().startsWith(QStringLiteral("2 of 3 metrics")));

        enabled->click();
        QVERIFY(settings.metricsBarEnabled());

        int memory = -1;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            if (row(i) == QLatin1String("memory"))
                memory = i;
        QVERIFY(memory > 1);
        list->topLevelItem(memory)->setCheckState(0, Qt::Checked);
        QCOMPARE(settings.metricsBarItems(), (QStringList{"cpu", "custom:/gone/old.json", "memory"}));

        // Move Memory to the top; the list rebuilds asynchronously.
        QTRY_COMPARE(row(2), QStringLiteral("memory"));
        list->setCurrentItem(list->topLevelItem(2));
        child<QPushButton>(window, "metricsBarUp")->click();
        child<QPushButton>(window, "metricsBarUp")->click();
        QCOMPARE(settings.metricsBarItems(), (QStringList{"memory", "cpu", "custom:/gone/old.json"}));
        QTRY_COMPARE(row(0), QStringLiteral("memory"));
        QCOMPARE(list->currentItem()->data(0, Qt::UserRole).toString(), QStringLiteral("memory"));
        child<QPushButton>(window, "metricsBarDown")->click();
        QCOMPARE(settings.metricsBarItems(), (QStringList{"cpu", "memory", "custom:/gone/old.json"}));

        // Unchecking the stale source removes only it.
        QTRY_COMPARE(row(2), QStringLiteral("custom:/gone/old.json"));
        list->topLevelItem(2)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(settings.metricsBarItems(), (QStringList{"cpu", "memory"}));

        // The window is not the source of truth: closing keeps everything.
        window.close();
        QVERIFY(settings.metricsBarEnabled());
    }

    void metricsBarLimitDisablesFurtherSelection()
    {
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        settings.setMetricsBarItems({QStringLiteral("cpu"), QStringLiteral("memory")});
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}});
        auto *list = child<QTreeWidget>(window, "metricsBarItems");
        auto enabledRows = [list] {
            int n = 0;
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                n += (list->topLevelItem(i)->flags() & Qt::ItemIsEnabled) ? 1 : 0;
            return n;
        };
        QCOMPARE(enabledRows(), list->topLevelItemCount()); // 2 of 3: everything selectable

        list->topLevelItem(2)->setCheckState(0, Qt::Checked); // third metric
        QCOMPARE(settings.metricsBarItems().size(), 3);
        QTRY_COMPARE(enabledRows(), 3); // only the selected rows stay usable
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list->topLevelItem(i);
            QCOMPARE(bool(item->flags() & Qt::ItemIsEnabled), item->checkState(0) == Qt::Checked);
        }
        QVERIFY(child<QLabel>(window, "metricsBarCount")->text().startsWith(QStringLiteral("3 of 3 metrics")));

        // Even a forced fourth check cannot exceed the limit.
        list->topLevelItem(5)->setCheckState(0, Qt::Checked);
        QCOMPARE(settings.metricsBarItems().size(), 3);
        QTRY_COMPARE(list->topLevelItem(5)->checkState(0), Qt::Unchecked);

        // Deselecting one makes the others selectable again.
        list->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(settings.metricsBarItems().size(), 2);
        QTRY_COMPARE(enabledRows(), list->topLevelItemCount());
    }

    void builtInMetricsSettingsAreShared()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("settings.ini"));
        aqtruncat::AppSettings settings(QStringLiteral("cat"), file);
        settings.setGpuDevice(QStringLiteral("0000:09:00.0")); // e.g. an eGPU unplugged now
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}});
        auto *gpu = child<QComboBox>(window, "gpuDevice");
        auto *fps = child<QComboBox>(window, "fpsSource");
        auto *interval = child<QComboBox>(window, "updatesInterval");
        auto *remote = child<QCheckBox>(window, "updatesRemote");
        QCOMPARE(gpu->currentData().toString(), QStringLiteral("0000:09:00.0"));
        QVERIFY(gpu->currentText().contains(QLatin1String("not present")));
        QCOMPARE(fps->currentData().toString(), QStringLiteral("mangohud"));
        QCOMPARE(interval->currentData().toInt(), 60);
        QVERIFY(!remote->isChecked());

        aqtruncat::metrics::GpuDevice intel;
        intel.id = "0000:00:02.0";
        intel.model = "Intel Iris Xe";
        window.setAvailableGpus({intel});
        QCOMPARE(gpu->count(), 3); // auto, Intel, the absent choice
        gpu->setCurrentIndex(gpu->findData(QStringLiteral("0000:00:02.0")));
        QCOMPARE(settings.gpuDevice(), QStringLiteral("0000:00:02.0"));
        QCOMPARE(gpu->count(), 2);
        fps->setCurrentIndex(fps->findData(QStringLiteral("off")));
        QCOMPARE(settings.fpsSource(), QStringLiteral("off"));
        interval->setCurrentIndex(interval->findData(0));
        QCOMPARE(settings.updatesIntervalMinutes(), 0);
        QVERIFY(!remote->isEnabled()); // nothing to refresh when checks are off

        // A change from another client (CLI, Plasmoid) shows up here.
        aqtruncat::AppSettings other(QStringLiteral("cat"), file);
        other.setUpdatesIntervalMinutes(180);
        other.setUpdatesRemoteRefresh(true);
        QVERIFY(settings.reload());
        QCOMPARE(interval->currentData().toInt(), 180);
        QVERIFY(remote->isEnabled() && remote->isChecked());
    }

    void metricsBarListsCustomSourcesAsTheyComeAndGo()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        QDir().mkpath(discovery);
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        SettingsWindow window(&settings,
            {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}}, &service);
        auto *list = child<QTreeWidget>(window, "metricsBarItems");
        QCOMPARE(list->topLevelItemCount(), 9 + 7);

        auto write = [&](const QString &name, const QByteArray &bar) {
            QFile json(discovery + u'/' + name);
            QVERIFY(json.open(QIODevice::WriteOnly));
            json.write("{\"title\":\"" + name.toUtf8() + "\",\"metrics\":[]," + bar
                       + "\"lastUpdatedDate\":\"2026-06-05T04:50:40Z\"}");
        };
        write(QStringLiteral("with.json"), "\"metricsBarValue\":\"5%\",");
        write(QStringLiteral("without.json"), "");
        service.rescan();
        QTRY_COMPARE(list->topLevelItemCount(), 11 + 7);
        QStringList notes;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            notes.append(list->topLevelItem(i)->text(1));
        QVERIFY(notes.contains(QStringLiteral("Custom")));
        QVERIFY(notes.contains(QStringLiteral("Custom — provides no Metrics Bar value")));

        // Select it, then delete the discovered file: the preference stays.
        const QString id = QStringLiteral("custom:") + QFileInfo(discovery + QStringLiteral("/with.json")).canonicalFilePath();
        settings.setMetricsBarItems({id});
        QVERIFY(QFile::remove(discovery + QStringLiteral("/with.json")));
        service.rescan();
        QTRY_COMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Custom — not currently available"));
        QCOMPARE(settings.metricsBarItems(), QStringList{id});
        write(QStringLiteral("with.json"), "\"metricsBarValue\":\"6%\",");
        service.rescan();
        QTRY_COMPARE(list->topLevelItem(0)->text(1), QStringLiteral("Custom"));
        QCOMPARE(list->topLevelItem(0)->checkState(0), Qt::Checked);
    }

    void quitEndsTheApplicationWithDashboardAndSettingsVisible()
    {
        // Both windows refuse close (they only hide): QCoreApplication::quit()
        // would be cancelled by them. Regression found in M8.
        QApplication::setQuitOnLastWindowClosed(false);
        QTemporaryDir temporary;
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        SettingsWindow window(&settings, {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}});
        DashboardWindow dashboard("/home/test");
        window.showAndRaise();
        dashboard.toggleVisible();
        QTRY_VERIFY(window.isVisible() && dashboard.isVisible());

        QMenu *menu = nullptr;
        MetricsBarPresenter presenter([&menu](QMenu *m) {
            menu = m;
            return std::make_unique<MenuOnlyTrayItem>();
        });
        aqtruncat::metricsbar::Item cpu;
        cpu.id = QStringLiteral("cpu");
        cpu.status = aqtruncat::metricsbar::Status::Available;
        cpu.shortValue = QStringLiteral("23%");
        presenter.setItems({cpu});
        QVERIFY(menu);
        QAction *quit = menu->actions().last();
        QCOMPARE(quit->text(), QStringLiteral("Quit AQTRunCat"));

        QTimer::singleShot(0, quit, &QAction::trigger);
        QTimer::singleShot(3000, [] { QCoreApplication::exit(7); }); // safety: fail, never hang
        QCOMPARE(QCoreApplication::exec(), 0);

        // The same helper backs the runner's menu and SIGINT/SIGTERM.
        QTimer::singleShot(0, [] { requestApplicationQuit(); });
        QTimer::singleShot(3000, [] { QCoreApplication::exit(7); });
        QCOMPARE(QCoreApplication::exec(), 0);
    }

    // v2F1.2: the checkbox of each Custom Metric is its file's "enabled".
    void customMetricEnableCheckbox()
    {
        QTemporaryDir temporary;
        const QString discovery = temporary.filePath(QStringLiteral("metrics"));
        QDir().mkpath(discovery);
        const QString file = discovery + QStringLiteral("/fixture.json");
        const QByteArray json = R"({"title":"Fixture","metricsBarValue":"7","future":{"kept":true},
            "metrics":[{"title":"Value","formattedValue":"7"}],"lastUpdatedDate":"2026-06-05T04:50:40Z"})";
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(json);
        }
        aqtruncat::AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        CustomMetricsService service(&settings, discovery);
        service.start();
        SettingsWindow window(&settings,
            {{QStringLiteral("cat"), QStringLiteral("Cat"), aqtruncat::RunnerSource::Bundled}}, &service);
        auto *tree = child<QTreeWidget>(window, "customMetricFiles");
        auto *bar = child<QTreeWidget>(window, "metricsBarItems");
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->checkState(0), Qt::Checked); // no "enabled" = on
        QCOMPARE(bar->topLevelItemCount(), 9 + 7 + 1);
        const QString id = QStringLiteral("custom:") + QFileInfo(file).canonicalFilePath();

        // Off: written to the file (other fields kept), listed as Disabled,
        // no longer offered to the Metrics Bar.
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QTRY_COMPARE(tree->topLevelItem(0)->text(3), QStringLiteral("Disabled"));
        QCOMPARE(tree->topLevelItem(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Fixture")); // still its name
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray stored = in.readAll();
        in.close();
        QCOMPARE(stored, QByteArray("{\"enabled\": false,") + json.mid(1));
        QVERIFY(!service.sources().first().enabled);
        QTRY_COMPARE(bar->topLevelItemCount(), 9 + 7);

        // A selection made while it was on stays, marked as disabled.
        settings.setMetricsBarItems({id});
        QTRY_COMPARE(bar->topLevelItem(0)->text(1), QStringLiteral("Custom — disabled"));
        QCOMPARE(bar->topLevelItem(0)->checkState(0), Qt::Checked);

        // On again: running and offered again, the selection unchanged.
        tree->topLevelItem(0)->setCheckState(0, Qt::Checked);
        QTRY_COMPARE(tree->topLevelItem(0)->text(3), QStringLiteral("Available"));
        QTRY_COMPARE(bar->topLevelItem(0)->text(1), QStringLiteral("Custom"));
        QCOMPARE(settings.metricsBarItems(), QStringList{id});

        // A file that cannot be written: a warning, the row shows the truth.
        if (::geteuid() == 0)
            QSKIP("root ignores directory permissions");
        QVERIFY(QFile::setPermissions(discovery, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QString warning;
        QTimer closer; // closes the warning once it is shown
        connect(&closer, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                warning = box->text();
                box->close();
            }
        });
        closer.start(20);
        tree->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY(!warning.isEmpty());
        closer.stop();
        QVERIFY(QFile::setPermissions(discovery, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY2(warning.contains(QLatin1String("cannot write")), qPrintable(warning));
        QTRY_COMPARE(tree->topLevelItem(0)->checkState(0), Qt::Checked);
        QVERIFY(service.sources().first().enabled);
    }
};

QTEST_MAIN(TestSettingsWindow)
#include "tst_settingswindow.moc"
