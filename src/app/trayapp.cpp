#include "trayapp.h"

#include "api/backendclient.h"
#include "applicationquit.h"
#include "appsettings.h"
#include "cli/runnercommands.h"
#include "dashboardwindow.h"
#include "logging.h"
#include "metricsbarmodel.h"
#include "metricsbarpresenter.h"
#include "providerintegration.h"
#include "runner/runneranimator.h"
#include "runpreferences.h"
#include "runnerselection.h"
#include "settingswindow.h"
#include "snapshotthrottle.h"
#include "traypresenter.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
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
    // F4.1: the metrics, Custom Metrics (with the Claude Code/Codex
    // adapters), the runner speed and autostart live in the backend
    // (aqtruncatd); this frontend only presents them. The backend is
    // D-Bus activated, or started from next to this executable.
    BackendClient::Options backendOptions;
    const QFileInfo backendExecutable(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("aqtruncatd")));
    if (backendExecutable.isFile() && backendExecutable.isExecutable())
        backendOptions.backendExecutable = backendExecutable.absoluteFilePath();
    BackendClient backend(backendOptions);
    DashboardWindow dashboard(home.toStdString());
    SnapshotThrottle dashboardUpdates(settings.dashboardRefreshSeconds());
    aqtruncat::RunnerAnimator animator;
    QObject::connect(&animator, &aqtruncat::RunnerAnimator::frameChanged, &tray, &TrayPresenter::showFrame);
    QObject::connect(&backend, &BackendClient::systemSnapshot, &dashboardUpdates, &SnapshotThrottle::submit);
    QObject::connect(&dashboardUpdates, &SnapshotThrottle::snapshotReady, &dashboard, &DashboardWindow::updateSnapshot);
    QObject::connect(&backend, &CustomMetricsModel::sourcesChanged, &dashboard, &DashboardWindow::updateCustomMetrics);
    // One settings file: whatever the backend reports changed is re-read.
    QObject::connect(&backend, &BackendClient::settingsChanged, &settings, &aqtruncat::AppSettings::reload);
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
    QObject::connect(&backend, &BackendClient::systemSnapshot, &metricsBar, &MetricsBarModel::updateSystem);
    QObject::connect(&backend, &CustomMetricsModel::sourcesChanged, &metricsBar, &MetricsBarModel::updateCustom);
    QObject::connect(&metricsBar, &MetricsBarModel::itemsChanged, &metricsBarTray, &MetricsBarPresenter::setItems);
    QObject::connect(&metricsBarTray, &MetricsBarPresenter::dashboardToggleRequested, &dashboard, &DashboardWindow::toggleVisible);
    QObject::connect(&dashboard, &DashboardWindow::visibilityChanged, &metricsBarTray, &MetricsBarPresenter::setDashboardVisible);

    QObject::connect(&settings, &aqtruncat::AppSettings::dashboardRefreshSecondsChanged,
                     &dashboardUpdates, &SnapshotThrottle::setIntervalSeconds);
    // Network selection and autostart are applied by the backend.

    // Created with the (lazy) Settings window, destroyed after it.
    std::unique_ptr<ProviderIntegration> claudeIntegration;
    std::unique_ptr<ProviderIntegration> codexIntegration;
    std::unique_ptr<SettingsWindow> settingsWindow;
    std::vector<aqtruncat::metrics::InterfaceMetrics> latestInterfaces;
    std::vector<aqtruncat::metrics::GpuDevice> latestGpus;
    QObject::connect(&backend, &BackendClient::systemSnapshot, &app,
                     [&](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                         latestInterfaces = snapshot.network.interfaces;
                         if (snapshot.gpu.status.state != aqtruncat::metrics::MetricState::Pending)
                             latestGpus = snapshot.gpu.devices;
                         if (settingsWindow) {
                             settingsWindow->setAvailableInterfaces(latestInterfaces);
                             settingsWindow->setAvailableGpus(latestGpus);
                         }
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
            claudeIntegration = std::make_unique<ProviderIntegration>(
                QStringLiteral("claude-code"), ProviderIntegration::findAdapter(QStringLiteral("aqtruncat-claude-metric")));
            codexIntegration = std::make_unique<ProviderIntegration>(
                QStringLiteral("codex"), ProviderIntegration::findAdapter(QStringLiteral("aqtruncat-codex-metric")));
            settingsWindow = std::make_unique<SettingsWindow>(&settings, std::move(options), &backend, nullptr,
                                                              claudeIntegration.get(), codexIntegration.get());
            settingsWindow->setActiveRunner(activeRunner);
            settingsWindow->setAvailableInterfaces(latestInterfaces);
            settingsWindow->setAvailableGpus(latestGpus);
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

    if (preferences.fpsFromCommandLine) {
        qCInfo(lcSpeed) << "fixed frame rate" << preferences.fixedFps << "fps (CPU load ignored)";
        animator.setFps(preferences.fixedFps);
    } else {
        // The backend computes the speed from CPU load (one computation for
        // every frontend); frames are animated here, with no IPC per frame.
        animator.setFps(aqtruncat::FpsStabilizer().current());
        QObject::connect(&backend, &BackendClient::runnerChanged, &animator, [&animator](int fps) {
            animator.setFps(fps);
            qCDebug(lcSpeed, "backend runner speed %2d fps", fps);
        });
    }
    // Closing this frontend never stops the backend: other clients may use it.
    backend.start();

    // Clean shutdown on SIGINT/SIGTERM.
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe);
    QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, [] { requestApplicationQuit(); });
    std::signal(SIGINT, onUnixSignal);
    std::signal(SIGTERM, onUnixSignal);

    return app.exec();
}
