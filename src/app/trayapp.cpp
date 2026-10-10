#include "trayapp.h"

#include "applicationquit.h"
#include "appsettings.h"
#include "autostartmanager.h"
#include "cli/runnercommands.h"
#include "cpumonitor.h"
#include "custommetricsservice.h"
#include "dashboardwindow.h"
#include "logging.h"
#include "metricsampler.h"
#include "metricsbarmodel.h"
#include "metricsbarpresenter.h"
#include "runner/runneranimator.h"
#include "runpreferences.h"
#include "runnerselection.h"
#include "runnerspeedcontroller.h"
#include "settingswindow.h"
#include "snapshotthrottle.h"
#include "storagepaths.h"
#include "traypresenter.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <csignal>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signalPipe[2] = {-1, -1};

void onUnixSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(signalPipe[1], &c, 1);
}

} // namespace

int runTrayApplication(QApplication &app, const aqtruncat::RunnerService &runners, const RunOptions &options)
{
    if (options.verbose)
        QLoggingCategory::setFilterRules(QStringLiteral("aqtruncat.*.debug=true"));

    QStringList discoveryNotes;
    runners.entries(&discoveryNotes);
    for (const QString &note : std::as_const(discoveryNotes))
        qCInfo(lcRunner).noquote() << note;

    aqtruncat::AppSettings settings(options.defaultRunnerId);
    const aqtruncat::EffectiveRunPreferences preferences =
        aqtruncat::resolveRunPreferences(options.runnerId, options.fixedFps, settings);
    const bool cliRunnerOverride = preferences.runnerFromCommandLine;
    const QString requestedRunner = preferences.runnerId;

    QElapsedTimer loadTime;
    loadTime.start();
    StartupRunnerSelection startup = selectStartupRunner(runners, requestedRunner,
                                                         options.defaultRunnerId, cliRunnerOverride);
    aqtruncat::RunnerChoice choice = std::move(startup.choice);
    if (startup.repairPersistedRunner)
        settings.setRunnerId(choice.runner->id);
    for (const QString &note : std::as_const(choice.notes))
        qCWarning(lcRunner).noquote() << note;
    if (!choice.runner) {
        // Straight to stderr: Qt logging may go to journald.
        std::fprintf(stderr, "aqtruncat: %s\n", qPrintable(choice.error));
        if (choice.errorKind == aqtruncat::RunnerError::NotFound)
            std::fprintf(stderr, "Run \"aqtruncat runner list\" to see the available runners.\n");
        return aqtruncat::cli::exitCodeFor(choice.errorKind);
    }
    aqtruncat::Runner activeRunner = std::move(*choice.runner);
    qCInfo(lcRunner).noquote() << QStringLiteral("runner \"%1\" (%2, %3 frames, %4x%5, %6) loaded in %7 ms")
                                      .arg(activeRunner.id, aqtruncat::sourceName(activeRunner.source))
                                      .arg(activeRunner.frames.size())
                                      .arg(activeRunner.frameSize.width())
                                      .arg(activeRunner.frameSize.height())
                                      .arg(activeRunner.monochrome ? QStringLiteral("monochrome") : QStringLiteral("color"))
                                      .arg(loadTime.elapsed());
    for (const QString &warning : activeRunner.warnings)
        qCWarning(lcRunner).noquote() << QStringLiteral("runner \"%1\": %2").arg(activeRunner.id, warning);

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qWarning("no system tray available yet; the icon appears when one is");
    QApplication::setQuitOnLastWindowClosed(false);

    TrayPresenter tray;
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    aqtruncat::metrics::MetricsSources metricSources;
    metricSources.storagePaths = desktopStoragePaths(home.toStdString());
    metricSources.networkSelection = settings.networkSelection();
    MetricSampler metrics(nullptr, std::move(metricSources));
    CustomMetricsService customMetrics(&settings);
    DashboardWindow dashboard(home.toStdString());
    SnapshotThrottle dashboardUpdates(settings.dashboardRefreshSeconds());
    aqtruncat::RunnerAnimator animator;
    QObject::connect(&animator, &aqtruncat::RunnerAnimator::frameChanged, &tray, &TrayPresenter::showFrame);
    QObject::connect(&metrics, &MetricSampler::snapshotReady, &dashboardUpdates, &SnapshotThrottle::submit);
    QObject::connect(&dashboardUpdates, &SnapshotThrottle::snapshotReady, &dashboard, &DashboardWindow::updateSnapshot);
    QObject::connect(&customMetrics, &CustomMetricsService::sourcesChanged,
                     &dashboard, &DashboardWindow::updateCustomMetrics);
    QObject::connect(&tray, &TrayPresenter::dashboardToggleRequested, &dashboard, &DashboardWindow::toggleVisible);
    QObject::connect(&dashboard, &DashboardWindow::visibilityChanged, &tray, &TrayPresenter::setDashboardVisible);
    tray.setRunner(activeRunner);
    animator.setSequence(activeRunner.sequence);

    // Metrics Bar: separate tray items fed by the same snapshots. The runner
    // item above is registered first, so hosts list the bar after it.
    MetricsBarModel metricsBar(home.toStdString());
    MetricsBarPresenter metricsBarTray;
    auto applyMetricsBarSettings = [&settings, &metricsBar] {
        metricsBar.setConfiguration(settings.metricsBarEnabled(), settings.metricsBarItems());
    };
    applyMetricsBarSettings();
    QObject::connect(&settings, &aqtruncat::AppSettings::metricsBarChanged, &metricsBar, applyMetricsBarSettings);
    QObject::connect(&metrics, &MetricSampler::snapshotReady, &metricsBar, &MetricsBarModel::updateSystem);
    QObject::connect(&customMetrics, &CustomMetricsService::sourcesChanged, &metricsBar, &MetricsBarModel::updateCustom);
    QObject::connect(&metricsBar, &MetricsBarModel::itemsChanged, &metricsBarTray, &MetricsBarPresenter::setItems);
    QObject::connect(&metricsBarTray, &MetricsBarPresenter::dashboardToggleRequested, &dashboard, &DashboardWindow::toggleVisible);
    QObject::connect(&dashboard, &DashboardWindow::visibilityChanged, &metricsBarTray, &MetricsBarPresenter::setDashboardVisible);

    QObject::connect(&settings, &aqtruncat::AppSettings::dashboardRefreshSecondsChanged,
                     &dashboardUpdates, &SnapshotThrottle::setIntervalSeconds);
    QObject::connect(&settings, &aqtruncat::AppSettings::networkSettingsChanged, &metrics,
                     [&settings, &metrics] { metrics.setNetworkSelection(settings.networkSelection()); });

    AutostartManager autostart;
    auto applyAutostart = [&settings, &autostart](bool enabled) {
        QString error;
        if (!autostart.setEnabled(enabled, &error)) {
            qWarning().noquote() << QStringLiteral("cannot update autostart: %1").arg(error);
            // Keep the model/UI honest if either creation or removal failed.
            // The nested signal is finite: the value is set to the actual
            // filesystem state, so a second failure cannot change it again.
            settings.setAutostartEnabled(autostart.isEnabled());
        }
    };
    applyAutostart(settings.autostartEnabled());
    QObject::connect(&settings, &aqtruncat::AppSettings::autostartEnabledChanged, &app, applyAutostart);

    std::unique_ptr<SettingsWindow> settingsWindow;
    std::vector<aqtruncat::metrics::InterfaceMetrics> latestInterfaces;
    QObject::connect(&metrics, &MetricSampler::snapshotReady, &app,
                     [&](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                         latestInterfaces = snapshot.network.interfaces;
                         if (settingsWindow)
                             settingsWindow->setAvailableInterfaces(latestInterfaces);
                     });
    auto openSettings = [&] {
        if (!settingsWindow) {
            QList<RunnerOption> options;
            for (const aqtruncat::ListedRunner &listed : runners.list()) {
                if (listed.result.runner) {
                    options.append({listed.result.runner->id, listed.result.runner->displayName,
                                    listed.result.runner->source});
                }
            }
            if (std::none_of(options.cbegin(), options.cend(), [&](const RunnerOption &item) {
                    return item.id == activeRunner.id;
                }))
                options.append({activeRunner.id, activeRunner.displayName, activeRunner.source});
            settingsWindow = std::make_unique<SettingsWindow>(&settings, std::move(options), &customMetrics);
            settingsWindow->setActiveRunner(activeRunner);
            settingsWindow->setAvailableInterfaces(latestInterfaces);
        }
        settingsWindow->showAndRaise();
    };
    QObject::connect(&tray, &TrayPresenter::settingsRequested, &app, openSettings);
    QObject::connect(&metricsBarTray, &MetricsBarPresenter::settingsRequested, &app, openSettings);

    QObject::connect(&settings, &aqtruncat::AppSettings::runnerIdChanged, &app, [&](const QString &id) {
        aqtruncat::RunnerChoice next = runners.choose(id, options.defaultRunnerId);
        if (!next.runner) {
            qCWarning(lcRunner).noquote() << QStringLiteral("cannot switch to saved runner \"%1\": %2").arg(id, next.error);
            return;
        }
        activeRunner = std::move(*next.runner);
        tray.setRunner(activeRunner);
        animator.setSequence(activeRunner.sequence);
        if (settingsWindow)
            settingsWindow->setActiveRunner(activeRunner);
    });

    CpuMonitor cpu;
    RunnerSpeedController speed(preferences.speedMode);
    if (preferences.fpsFromCommandLine) {
        qCInfo(lcSpeed) << "fixed frame rate" << preferences.fixedFps << "fps (CPU load ignored)";
        animator.setFps(preferences.fixedFps);
    } else {
        animator.setFps(speed.currentFps());
        QObject::connect(&metrics, &MetricSampler::snapshotReady, &cpu,
                         [&cpu](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                             cpu.consume(snapshot.cpu);
                         });
        QObject::connect(&cpu, &CpuMonitor::sampled, &speed, &RunnerSpeedController::consume);
        QObject::connect(&speed, &RunnerSpeedController::fpsChanged, &animator,
                         [&](int applied, int target, double raw, double smoothed) {
            animator.setFps(applied);
            qCDebug(lcSpeed, "cpu raw=%5.1f%% smoothed=%5.1f%% target=%2d fps applied=%2d fps",
                    raw, smoothed, target, applied);
        });
        QObject::connect(&settings, &aqtruncat::AppSettings::speedModeChanged,
                         &speed, &RunnerSpeedController::setMode);
    }
    // Always active: Dashboard visibility never changes sampler or runner
    // lifecycle. In fixed-FPS mode the runner simply ignores CPU snapshots.
    metrics.start();
    customMetrics.start();

    // Clean shutdown on SIGINT/SIGTERM.
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe);
    QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [] { requestApplicationQuit(); });
    std::signal(SIGINT, onUnixSignal);
    std::signal(SIGTERM, onUnixSignal);

    return app.exec();
}
