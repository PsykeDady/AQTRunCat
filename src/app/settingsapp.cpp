#include "settingsapp.h"

#include "api/backendclient.h"
#include "appsettings.h"
#include "providerintegration.h"
#include "runnerselection.h"
#include "settingsactivation.h"
#include "settingswindow.h"
#include "runner/runnerservice.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <cstdio>

int runStandaloneSettings(QApplication &app, const aqtruncat::RunnerService &runners,
                          const QString &defaultRunnerId)
{
    aqtruncat::AppSettings settings(defaultRunnerId);
    BackendClient::Options backendOptions;
    const QFileInfo backendExecutable(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("aqtruncatd")));
    if (backendExecutable.isFile() && backendExecutable.isExecutable())
        backendOptions.backendExecutable = backendExecutable.absoluteFilePath();
    BackendClient backend(backendOptions);

    QList<RunnerOption> runnerOptions;
    for (const aqtruncat::ListedRunner &listed : runners.list()) {
        if (listed.result.runner)
            runnerOptions.append({listed.result.runner->id, listed.result.runner->displayName,
                                  listed.result.runner->source});
    }
    ProviderIntegration claude(QStringLiteral("claude-code"),
                               ProviderIntegration::findAdapter(QStringLiteral("aqtruncat-claude-metric")));
    ProviderIntegration codex(QStringLiteral("codex"),
                              ProviderIntegration::findAdapter(QStringLiteral("aqtruncat-codex-metric")));
    SettingsWindow window(&settings, std::move(runnerOptions), &backend, nullptr, &claude, &codex);
    window.setCloseTerminatesApplication(true);
    const aqtruncat::RunnerChoice selected = runners.choose(settings.runnerId(), defaultRunnerId);
    if (selected.runner)
        window.setActiveRunner(*selected.runner);

    SettingsActivation activation(&window);
    QString error;
    if (!activation.acquire(&error)) {
        // A false result with no error means another process accepted Show.
        if (error.isEmpty())
            return 0;
        std::fprintf(stderr, "aqtruncat: cannot open Settings: %s\n", qPrintable(error));
        return 1;
    }

    QObject::connect(&backend, &BackendClient::settingsChanged, &settings, &aqtruncat::AppSettings::reload);
    QObject::connect(&backend, &BackendClient::systemSnapshot, &app,
                     [&window](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                         window.setAvailableInterfaces(snapshot.network.interfaces);
                         if (snapshot.gpu.status.state != aqtruncat::metrics::MetricState::Pending)
                             window.setAvailableGpus(snapshot.gpu.devices);
                     });
    QObject::connect(&settings, &aqtruncat::AppSettings::runnerIdChanged, &app,
                     [&window, &runners, defaultRunnerId](const QString &id) {
                         const aqtruncat::RunnerChoice choice = runners.choose(id, defaultRunnerId);
                         if (choice.runner)
                             window.setActiveRunner(*choice.runner);
                     });

    QApplication::setQuitOnLastWindowClosed(false);
    QObject::connect(&window, &QObject::destroyed, &app, &QCoreApplication::quit);
    window.showAndRaise();
    backend.start();
    return app.exec();
}
