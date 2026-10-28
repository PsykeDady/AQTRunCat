#include "app/appsettings.h"
#include "app/autostartcommand.h"
#include "app/autostartmanager.h"
#include "app/runpreferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using aqtruncat::AppSettings;
using aqtruncat::SpeedMode;
using aqtruncat::metrics::NetworkPolicyMode;

class TestAppSettings : public QObject
{
    Q_OBJECT

private slots:
    void defaultsDoNotCreateSpeculativeBehaviour()
    {
        QTemporaryDir temporary;
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        QCOMPARE(settings.runnerId(), QStringLiteral("cat"));
        QCOMPARE(settings.speedMode(), SpeedMode::Normal);
        QCOMPARE(settings.networkMode(), NetworkPolicyMode::Automatic);
        QVERIFY(settings.networkInterfaces().isEmpty());
        QCOMPARE(settings.dashboardRefreshSeconds(), 1);
        QVERIFY(!settings.autostartEnabled());
        QVERIFY(settings.customMetricFiles().isEmpty());
        QVERIFY(!settings.metricsBarEnabled());
        QCOMPARE(settings.metricsBarItems(), QStringList{QStringLiteral("cpu")});
    }

    void metricsBarPersistsOrderAndEmptySelection()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        const QString custom = QStringLiteral("custom:/data/claude.json");
        {
            AppSettings settings(QStringLiteral("cat"), path);
            QSignalSpy changed(&settings, &AppSettings::metricsBarChanged);
            settings.setMetricsBarEnabled(true);
            settings.setMetricsBarItems({QStringLiteral("network.upload"), custom, QStringLiteral("cpu")});
            QCOMPARE(changed.count(), 2);
            settings.setMetricsBarItems(settings.metricsBarItems());
            QCOMPARE(changed.count(), 2);
        }
        {
            AppSettings loaded(QStringLiteral("cat"), path);
            QVERIFY(loaded.metricsBarEnabled());
            QCOMPARE(loaded.metricsBarItems(), (QStringList{QStringLiteral("network.upload"), custom, QStringLiteral("cpu")}));
            loaded.setMetricsBarItems({});
        }
        AppSettings empty(QStringLiteral("cat"), path);
        QVERIFY(empty.metricsBarItems().isEmpty()); // explicit empty is not the default
        empty.restoreDefaults();
        QVERIFY(!empty.metricsBarEnabled());
        QCOMPARE(empty.metricsBarItems(), QStringList{QStringLiteral("cpu")});
    }

    void metricsBarNeverStoresMoreThanThreeItems()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        AppSettings settings(QStringLiteral("cat"), path);
        settings.setMetricsBarItems({QStringLiteral("cpu"), QStringLiteral("memory"), QStringLiteral("swap"),
                                     QStringLiteral("disk.read")});
        QCOMPARE(settings.metricsBarItems(), (QStringList{"cpu", "memory", "swap"}));
        QSettings stored(path, QSettings::IniFormat);
        QCOMPARE(stored.value(QStringLiteral("metricsBar/items")).toStringList().size(), 3);
    }

    void legacyMetricsBarListsNormaliseDeterministically()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        {
            // Hand-edited / legacy: empty, unknown, duplicate and too many ids.
            QSettings raw(path, QSettings::IniFormat);
            raw.setValue(QStringLiteral("metricsBar/enabled"), true);
            raw.setValue(QStringLiteral("metricsBar/items"),
                         QStringList{QString(), QStringLiteral("future.battery"), QStringLiteral("network.download"),
                                     QStringLiteral("custom:"), QStringLiteral("network.download"),
                                     QStringLiteral("custom:/data/a.json"), QStringLiteral("cpu"),
                                     QStringLiteral("memory"), QStringLiteral("swap")});
        }
        for (int run = 0; run < 2; ++run) { // same result on every load
            AppSettings loaded(QStringLiteral("cat"), path);
            QVERIFY(loaded.metricsBarEnabled());
            QCOMPARE(loaded.metricsBarItems(),
                     (QStringList{"network.download", "custom:/data/a.json", "cpu"}));
        }
    }

    void itemsOfMultiValueSourcesPersist()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        const QStringList wanted{QStringLiteral("custom-item:seven_day:/data/claude-code/metric.json"),
                                 QStringLiteral("custom-item:five_hour:/data/claude-code/metric.json"),
                                 QStringLiteral("cpu.temperature")};
        {
            AppSettings settings(QStringLiteral("cat"), path);
            settings.setMetricsBarItems(wanted + QStringList{QStringLiteral("custom-item:bad id:/x")});
            QCOMPARE(settings.metricsBarItems(), wanted);
        }
        AppSettings loaded(QStringLiteral("cat"), path);
        QCOMPARE(loaded.metricsBarItems(), wanted);
        loaded.setMetricsBarItems({QStringLiteral("custom-item:bad id:/x"), QStringLiteral("custom-item:x:")});
        QVERIFY(loaded.metricsBarItems().isEmpty());
    }

    void valuesPersistAcrossInstances()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        {
            AppSettings settings(QStringLiteral("cat"), path);
            settings.setRunnerId(QStringLiteral("coffee"));
            settings.setSpeedMode(SpeedMode::SlowDown);
            settings.setNetworkMode(NetworkPolicyMode::Manual);
            settings.setNetworkInterfaces({QStringLiteral("tun0"), QStringLiteral("wlan0"), QStringLiteral("tun0")});
            settings.setDashboardRefreshSeconds(5);
            settings.setAutostartEnabled(true);
            settings.setCustomMetricFiles({temporary.filePath(QStringLiteral("first.json")),
                                           temporary.filePath(QStringLiteral("second.json"))});
        }
        AppSettings loaded(QStringLiteral("cat"), path);
        QCOMPARE(loaded.runnerId(), QStringLiteral("coffee"));
        QCOMPARE(loaded.speedMode(), SpeedMode::SlowDown);
        QCOMPARE(loaded.networkMode(), NetworkPolicyMode::Manual);
        QCOMPARE(loaded.networkInterfaces(), (QStringList{QStringLiteral("tun0"), QStringLiteral("wlan0")}));
        QCOMPARE(loaded.dashboardRefreshSeconds(), 5);
        QVERIFY(loaded.autostartEnabled());
        QCOMPARE(loaded.customMetricFiles().size(), 2);
        QCOMPARE(loaded.networkSelection().interfaces,
                 (std::set<std::string>{"tun0", "wlan0"}));
    }

    void invalidAndUnknownValuesFallBackSafely()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("runner/id"), QStringLiteral("../escape"));
        raw.setValue(QStringLiteral("runner/speedMode"), QStringLiteral("future-turbo"));
        raw.setValue(QStringLiteral("network/mode"), QStringLiteral("future-smart"));
        raw.setValue(QStringLiteral("metrics/dashboardRefreshSeconds"), 99);
        raw.sync();

        AppSettings loaded(QStringLiteral("cat"), path);
        QCOMPARE(loaded.runnerId(), QStringLiteral("cat"));
        QCOMPARE(loaded.speedMode(), SpeedMode::Normal);
        QCOMPARE(loaded.networkMode(), NetworkPolicyMode::Automatic);
        QCOMPARE(loaded.dashboardRefreshSeconds(), 1);
        QVERIFY(!AppSettings::isValidStoredRunnerId(QStringLiteral("../escape")));
        QVERIFY(AppSettings::isValidStoredRunnerId(QStringLiteral("my runner")));
    }

    void settersValidateAndResetDefaults()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath(QStringLiteral("settings.ini"));
        AppSettings settings(QStringLiteral("cat"), path);
        settings.setRunnerId(QStringLiteral("../bad"));
        QCOMPARE(settings.runnerId(), QStringLiteral("cat"));
        settings.setRunnerId(QStringLiteral("coffee"));
        settings.setSpeedMode(SpeedMode::SlowDown);
        settings.setNetworkMode(NetworkPolicyMode::Manual);
        settings.setNetworkInterfaces({QStringLiteral("tun0")});
        settings.setDashboardRefreshSeconds(2);
        settings.setAutostartEnabled(true);
        const QString custom = temporary.filePath(QStringLiteral("metric.json"));
        settings.setCustomMetricFiles({custom, custom});
        settings.setMetricsBarEnabled(true);
        settings.setMetricsBarItems({QStringLiteral("memory")});
        settings.restoreDefaults();
        QCOMPARE(settings.runnerId(), QStringLiteral("cat"));
        QCOMPARE(settings.speedMode(), SpeedMode::Normal);
        QCOMPARE(settings.networkMode(), NetworkPolicyMode::Automatic);
        QVERIFY(settings.networkInterfaces().isEmpty());
        QCOMPARE(settings.dashboardRefreshSeconds(), 1);
        QVERIFY(!settings.autostartEnabled());
        QCOMPARE(settings.customMetricFiles(), QStringList{QFileInfo(custom).absoluteFilePath()});
        QSettings raw(path, QSettings::IniFormat);
        QCOMPARE(raw.allKeys(), QStringList{QStringLiteral("customMetrics/files")});
    }

    void f5SettingsDefaultsValidationAndPersistence()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("settings.ini"));
        {
            AppSettings settings(QStringLiteral("cat"), file);
            QCOMPARE(settings.gpuDevice(), QStringLiteral("auto"));
            QCOMPARE(settings.fpsSource(), QStringLiteral("mangohud"));
            QCOMPARE(settings.updatesIntervalMinutes(), 60);
            QVERIFY(!settings.updatesRemoteRefresh()); // remote metadata is opt-in
            QVERIFY(settings.showCpuTemperature());
            QVERIFY(settings.showGpuMemory());
            QVERIFY(settings.showGpuTemperature());
            QVERIFY(settings.showFps());
            QVERIFY(!QFile::exists(file)); // defaults are not written

            QSignalSpy changed(&settings, &AppSettings::systemMetricsSettingsChanged);
            QSignalSpy displayChanged(&settings, &AppSettings::displaySettingsChanged);
            settings.setGpuDevice(QStringLiteral("0000:03:00.0"));
            settings.setGpuDevice(QStringLiteral("../../etc")); // rejected
            settings.setGpuDevice(QStringLiteral("0000:03:00.0")); // unchanged: no signal
            settings.setFpsSource(QStringLiteral("off"));
            settings.setFpsSource(QStringLiteral("gamescope")); // unknown: rejected
            settings.setUpdatesIntervalMinutes(180);
            settings.setUpdatesIntervalMinutes(7); // not offered: rejected
            settings.setUpdatesRemoteRefresh(true);
            settings.setShowCpuTemperature(false);
            settings.setShowGpuMemory(false);
            settings.setShowGpuTemperature(false);
            settings.setShowFps(false);
            QCOMPARE(changed.count(), 4);
            QCOMPARE(displayChanged.count(), 4);
            QCOMPARE(settings.gpuDevice(), QStringLiteral("0000:03:00.0"));
            QCOMPARE(settings.fpsSource(), QStringLiteral("off"));
            QCOMPARE(settings.updatesIntervalMinutes(), 180);
        }
        AppSettings again(QStringLiteral("cat"), file);
        QCOMPARE(again.gpuDevice(), QStringLiteral("0000:03:00.0"));
        QCOMPARE(again.fpsSource(), QStringLiteral("off"));
        QCOMPARE(again.updatesIntervalMinutes(), 180);
        QVERIFY(again.updatesRemoteRefresh());
        QVERIFY(!again.showCpuTemperature());
        QVERIFY(!again.showGpuMemory());
        QVERIFY(!again.showGpuTemperature());
        QVERIFY(!again.showFps());

        // Another writer (CLI, Plasmoid through the backend) and reload.
        AppSettings other(QStringLiteral("cat"), file);
        other.setUpdatesIntervalMinutes(0);
        QSignalSpy reloaded(&again, &AppSettings::systemMetricsSettingsChanged);
        QVERIFY(again.reload());
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(again.updatesIntervalMinutes(), 0);

        again.restoreDefaults();
        QCOMPARE(again.gpuDevice(), QStringLiteral("auto"));
        QCOMPARE(again.fpsSource(), QStringLiteral("mangohud"));
        QCOMPARE(again.updatesIntervalMinutes(), 60);
        QVERIFY(!again.updatesRemoteRefresh());
        QVERIFY(again.showCpuTemperature());
        QVERIFY(again.showGpuMemory());
        QVERIFY(again.showGpuTemperature());
        QVERIFY(again.showFps());
    }

    void f5HandEditedValuesFallBack()
    {
        QTemporaryDir temporary;
        const QString file = temporary.filePath(QStringLiteral("settings.ini"));
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(QStringLiteral("gpu/device"), QStringLiteral("card0; rm -rf"));
            raw.setValue(QStringLiteral("fps/source"), QStringLiteral("inject"));
            raw.setValue(QStringLiteral("updates/intervalMinutes"), 1);
            raw.setValue(QStringLiteral("metricsBar/items"), QStringList{QStringLiteral("gpu.temperature"),
                                                                         QStringLiteral("kernel"), QStringLiteral("fps")});
        }
        AppSettings settings(QStringLiteral("cat"), file);
        QCOMPARE(settings.gpuDevice(), QStringLiteral("auto"));
        QCOMPARE(settings.fpsSource(), QStringLiteral("mangohud"));
        QCOMPARE(settings.updatesIntervalMinutes(), 60);
        // Text-only metrics never take a Metrics Bar slot.
        QCOMPARE(settings.metricsBarItems(), (QStringList{QStringLiteral("gpu.temperature"), QStringLiteral("fps")}));
        QVERIFY(AppSettings::isValidGpuDevice(QStringLiteral("platform:fd5a0000.gpu")));
    }

    void commandLineOverridesDoNotMutatePersistence()
    {
        QTemporaryDir temporary;
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        auto defaults = aqtruncat::resolveRunPreferences({}, 0, settings);
        QCOMPARE(defaults.runnerId, QStringLiteral("cat"));
        QCOMPARE(defaults.speedMode, SpeedMode::Normal);
        QVERIFY(!defaults.runnerFromCommandLine);
        QVERIFY(!defaults.fpsFromCommandLine);

        settings.setRunnerId(QStringLiteral("coffee"));
        settings.setSpeedMode(SpeedMode::SlowDown);
        auto persisted = aqtruncat::resolveRunPreferences({}, 0, settings);
        QCOMPARE(persisted.runnerId, QStringLiteral("coffee"));
        QCOMPARE(persisted.speedMode, SpeedMode::SlowDown);

        const auto overridden = aqtruncat::resolveRunPreferences(QStringLiteral("cat"), 20, settings);
        QCOMPARE(overridden.runnerId, QStringLiteral("cat"));
        QCOMPARE(overridden.fixedFps, 20);
        QVERIFY(overridden.runnerFromCommandLine);
        QVERIFY(overridden.fpsFromCommandLine);
        QCOMPARE(settings.runnerId(), QStringLiteral("coffee"));
        QCOMPARE(settings.speedMode(), SpeedMode::SlowDown);
    }

    void xdgAutostartIsAtomicAndReversible()
    {
        QTemporaryDir temporary;
        AutostartManager autostart(temporary.path(), QStringLiteral("/opt/A Cute Run Cat/aqtruncat"));
        QString error;
        QVERIFY2(autostart.setEnabled(true, &error), qPrintable(error));
        QVERIFY(autostart.isEnabled());
        QCOMPARE(autostart.filePath(), temporary.filePath(QStringLiteral("autostart/aqtruncat.desktop")));
        QFile file(autostart.filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray contents = file.readAll();
        QVERIFY(contents.contains("Name=AQTRunCat"));
        QVERIFY(contents.contains("Comment=A Cute Run Cat for Linux"));
        QVERIFY(contents.contains("Exec=\"/opt/A Cute Run Cat/aqtruncat\""));
        QVERIFY(contents.contains("Icon=aqtruncat"));
        QVERIFY(!contents.contains("RunCat Neo"));
        QVERIFY(autostart.setEnabled(false, &error));
        QVERIFY(!autostart.isEnabled());
    }

    void autostartCommandKeepsEntryAndPreferenceInStep()
    {
        QTemporaryDir temporary;
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        const AutostartManager autostart(temporary.filePath(QStringLiteral("config")),
                                         QStringLiteral("/home/u/.local/bin/aqtruncat"));
        QString outText, errText;
        QTextStream out(&outText), err(&errText);

        QCOMPARE(runAutostartCommand(std::nullopt, settings, autostart, out, err), 0);
        QVERIFY(outText.startsWith(QStringLiteral("disabled: ")));

        QCOMPARE(runAutostartCommand(true, settings, autostart, out, err), 0);
        QVERIFY(autostart.isEnabled());
        QVERIFY(settings.autostartEnabled()); // the app re-applies this at start-up
        QFile file(autostart.filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Exec=\"/home/u/.local/bin/aqtruncat\""));
        file.close();
        QCOMPARE(runAutostartCommand(true, settings, autostart, out, err), 0); // idempotent, one entry
        QCOMPARE(QDir(temporary.filePath(QStringLiteral("config/autostart"))).entryList(QDir::Files).size(), 1);

        QCOMPARE(runAutostartCommand(false, settings, autostart, out, err), 0);
        QVERIFY(!autostart.isEnabled());
        QVERIFY(!settings.autostartEnabled());
        QCOMPARE(runAutostartCommand(false, settings, autostart, out, err), 0); // already disabled is fine
        QVERIFY(errText.isEmpty());
    }

    void autostartCommandReportsFailure()
    {
        QTemporaryDir temporary;
        // A regular file where the config directory should be: cannot create autostart/.
        QFile blocker(temporary.filePath(QStringLiteral("config")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        AppSettings settings(QStringLiteral("cat"), temporary.filePath(QStringLiteral("settings.ini")));
        const AutostartManager autostart(temporary.filePath(QStringLiteral("config")), QStringLiteral("/usr/bin/aqtruncat"));
        QString outText, errText;
        QTextStream out(&outText), err(&errText);
        QCOMPARE(runAutostartCommand(true, settings, autostart, out, err), 1);
        QVERIFY(errText.contains(QStringLiteral("cannot enable autostart")));
        QVERIFY(!settings.autostartEnabled()); // preference unchanged on failure
    }
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
