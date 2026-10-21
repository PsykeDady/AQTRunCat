#include "backendservice.h"

#include "apischema.h"
#include "app/appsettings.h"
#include "app/autostartmanager.h"
#include "app/cpumonitor.h"
#include "app/custommetricsservice.h"
#include "app/metricsampler.h"
#include "app/metricsbarids.h"
#include "app/providerintegration.h"
#include "app/runnerspeedcontroller.h"
#include "app/storagepaths.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {

QString compact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString compact(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// "value" of SetSetting: a JSON text, so that every client passes every type
// the same way ("\"slow-down\"", "true", "[\"cpu\"]"). A bare word that is
// not JSON is taken as a string ("slow-down").
QJsonValue parseValue(const QString &text)
{
    const QJsonDocument wrapped = QJsonDocument::fromJson("[" + text.toUtf8() + "]");
    if (wrapped.isArray() && wrapped.array().size() == 1)
        return wrapped.array().at(0);
    return QJsonValue(text);
}

std::optional<QStringList> stringList(const QJsonValue &value, int limit)
{
    if (!value.isArray() || value.toArray().size() > limit)
        return std::nullopt;
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString())
            return std::nullopt;
        result.append(entry.toString());
    }
    return result;
}

} // namespace

BackendService::BackendService(Config config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
{
    m_settings = m_config.settingsFile.isEmpty()
        ? std::make_unique<aqtruncat::AppSettings>(m_config.defaultRunnerId)
        : std::make_unique<aqtruncat::AppSettings>(m_config.defaultRunnerId, m_config.settingsFile);
    aqtruncat::metrics::MetricsSources sources;
    sources.storagePaths = desktopStoragePaths(m_config.homePath);
    sources.networkSelection = m_settings->networkSelection();
    m_sampler = std::make_unique<MetricSampler>(nullptr, std::move(sources));
    m_custom = std::make_unique<CustomMetricsService>(m_settings.get(), m_config.discoveryDirectory);
    m_cpu = std::make_unique<CpuMonitor>();
    m_speed = std::make_unique<RunnerSpeedController>(m_settings->speedMode());
    m_fps = m_targetFps = m_speed->currentFps();
    if (m_config.manageAutostart && !m_config.frontendExecutable.isEmpty())
        m_autostart = std::make_unique<AutostartManager>(QString(), m_config.frontendExecutable);
    m_claude = std::make_unique<ProviderIntegration>(QStringLiteral("claude-code"), m_config.claudeAdapter);
    m_codex = std::make_unique<ProviderIntegration>(QStringLiteral("codex"), m_config.codexAdapter);

    // Metrics: one coalesced publication for whatever changed in the same
    // event-loop pass (a system sample, a Custom Metrics run).
    m_coalesce.setSingleShot(true);
    m_coalesce.setInterval(0);
    connect(&m_coalesce, &QTimer::timeout, this, &BackendService::publishMetrics);
    connect(m_sampler.get(), &MetricSampler::snapshotReady, this,
            [this](const aqtruncat::metrics::SystemMetricsSnapshot &snapshot) {
                m_system = snapshot;
                m_cpu->consume(snapshot.cpu);
                scheduleMetrics();
            });
    connect(m_custom.get(), &CustomMetricsModel::sourcesChanged, this,
            [this](const QList<aqtruncat::custommetrics::Source> &sources) {
                m_sources = sources;
                scheduleMetrics();
            });

    // Runner speed from CPU load: computed once here for every frontend.
    connect(m_cpu.get(), &CpuMonitor::sampled, m_speed.get(), &RunnerSpeedController::consume);
    connect(m_speed.get(), &RunnerSpeedController::fpsChanged, this, [this](int applied, int target) {
        m_fps = applied;
        m_targetFps = target;
        publishRunner();
    });

    // Settings: one INI file, possibly written by a frontend or by
    // "aqtruncat autostart"; watched and reloaded, then republished.
    connect(m_settings.get(), &aqtruncat::AppSettings::networkSettingsChanged, this,
            [this] { m_sampler->setNetworkSelection(m_settings->networkSelection()); });
    connect(m_settings.get(), &aqtruncat::AppSettings::speedModeChanged, m_speed.get(), &RunnerSpeedController::setMode);
    connect(m_settings.get(), &aqtruncat::AppSettings::autostartEnabledChanged, this, [this] { applyAutostart(); });
    for (auto signal : {&aqtruncat::AppSettings::networkSettingsChanged, &aqtruncat::AppSettings::metricsBarChanged}) {
        connect(m_settings.get(), signal, this, &BackendService::publishSettings);
    }
    connect(m_settings.get(), &aqtruncat::AppSettings::runnerIdChanged, this, [this] {
        publishSettings();
        publishRunner();
    });
    connect(m_settings.get(), &aqtruncat::AppSettings::speedModeChanged, this, [this] {
        publishSettings();
        publishRunner();
    });
    connect(m_settings.get(), &aqtruncat::AppSettings::dashboardRefreshSecondsChanged, this, &BackendService::publishSettings);
    connect(m_settings.get(), &aqtruncat::AppSettings::autostartEnabledChanged, this, &BackendService::publishSettings);
    connect(m_settings.get(), &aqtruncat::AppSettings::customMetricFilesChanged, this, &BackendService::publishSettings);
    m_settingsReload.setSingleShot(true);
    m_settingsReload.setInterval(100);
    connect(&m_settingsReload, &QTimer::timeout, this, [this] {
        m_settings->reload();
        watchSettingsFile();
    });
    connect(&m_settingsWatcher, &QFileSystemWatcher::fileChanged, &m_settingsReload, qOverload<>(&QTimer::start));
    connect(&m_settingsWatcher, &QFileSystemWatcher::directoryChanged, &m_settingsReload, qOverload<>(&QTimer::start));

    // Official integrations: management state (the metric values come from
    // Custom Metrics like any other source).
    for (ProviderIntegration *provider : {m_claude.get(), m_codex.get()}) {
        connect(provider, &ProviderIntegration::statusChanged, this, [this] {
            const QJsonArray current = integrations();
            if (current == m_lastIntegrations)
                return;
            m_lastIntegrations = current;
            emit IntegrationsChanged(compact(current));
        });
        connect(provider, &ProviderIntegration::commandFinished, this, [this, provider](bool ok, const QString &text) {
            for (int i = 0; i < m_pendingReplies.size(); ++i) {
                if (m_pendingReplies.at(i).integration != provider->id())
                    continue;
                const PendingReply pending = m_pendingReplies.takeAt(i);
                pending.connection.send(ok ? pending.message.createReply(text)
                                           : pending.message.createErrorReply(aqtruncat::api::ErrorFailed, text));
                break;
            }
        });
    }
    m_metrics = aqtruncat::api::builtInMetrics(std::nullopt, m_config.homePath);
    for (const QJsonValue &value : std::as_const(m_metrics))
        m_metricsById.insert(value.toObject().value(QStringLiteral("id")).toString(), value.toObject());
    m_lastSettings = aqtruncat::api::settingsObject(*m_settings);
    m_lastRunner = runner();
}

BackendService::~BackendService()
{
    if (m_custom)
        m_custom->stop();
    if (m_sampler)
        m_sampler->stop();
}

bool BackendService::registerOn(QDBusConnection connection, QString *error)
{
    if (!connection.isConnected()) {
        if (error)
            *error = QStringLiteral("no D-Bus session bus: %1").arg(connection.lastError().message());
        return false;
    }
    if (!connection.registerObject(aqtruncat::api::ObjectPath, this,
                                   QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        if (error)
            *error = QStringLiteral("cannot export %1").arg(aqtruncat::api::ObjectPath);
        return false;
    }
    const QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply = connection.interface()->registerService(
        aqtruncat::api::ServiceName, QDBusConnectionInterface::DontQueueService,
        QDBusConnectionInterface::DontAllowReplacement);
    if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
        connection.unregisterObject(aqtruncat::api::ObjectPath);
        if (error) {
            *error = reply.isValid() ? QStringLiteral("another AQTRunCat backend already owns %1").arg(aqtruncat::api::ServiceName)
                                     : reply.error().message();
        }
        return false;
    }
    return true;
}

void BackendService::start()
{
    m_startedAt = QDateTime::currentDateTimeUtc();
    watchSettingsFile();
    applyAutostart();
    m_sampler->start();
    m_custom->start();
    m_claude->refresh();
    m_codex->refresh();
}

void BackendService::watchSettingsFile()
{
    // The file is replaced atomically on every write: watch its directory
    // too, and re-add the file after each change.
    const QFileInfo file(m_settings->fileName());
    const QStringList watched = m_settingsWatcher.files() + m_settingsWatcher.directories();
    if (!watched.isEmpty())
        m_settingsWatcher.removePaths(watched);
    QStringList paths;
    if (file.exists())
        paths << file.absoluteFilePath();
    if (QFileInfo(file.absolutePath()).isDir())
        paths << file.absolutePath();
    if (!paths.isEmpty())
        m_settingsWatcher.addPaths(paths);
}

void BackendService::applyAutostart()
{
    if (!m_autostart)
        return;
    QString error;
    if (!m_autostart->setEnabled(m_settings->autostartEnabled(), &error)) {
        qWarning().noquote() << QStringLiteral("cannot update autostart: %1").arg(error);
        m_settings->setAutostartEnabled(m_autostart->isEnabled());
    }
}

void BackendService::scheduleMetrics()
{
    m_coalesce.start();
}

void BackendService::publishMetrics()
{
    QJsonArray next = aqtruncat::api::builtInMetrics(m_system, m_config.homePath);
    for (const aqtruncat::custommetrics::Source &source : std::as_const(m_sources))
        next.append(aqtruncat::api::customMetric(source));
    QJsonArray changed;
    QHash<QString, QJsonObject> nextById;
    for (const QJsonValue &value : std::as_const(next)) {
        const QJsonObject metric = value.toObject();
        const QString id = metric.value(QStringLiteral("id")).toString();
        nextById.insert(id, metric);
        if (m_metricsById.value(id) != metric)
            changed.append(metric);
    }
    QJsonArray removed;
    for (auto it = m_metricsById.cbegin(); it != m_metricsById.cend(); ++it) {
        if (!nextById.contains(it.key()))
            removed.append(it.key());
    }
    // The order counts too (a source added or moved).
    const bool reordered = next.size() == m_metrics.size() && changed.isEmpty() && removed.isEmpty() && next != m_metrics;
    m_metrics = next;
    m_metricsById = std::move(nextById);
    if (changed.isEmpty() && removed.isEmpty() && !reordered)
        return;
    ++m_generation;
    QJsonArray order;
    for (const QJsonValue &value : std::as_const(m_metrics))
        order.append(value.toObject().value(QStringLiteral("id")));
    emit MetricsChanged(m_generation, compact(QJsonObject{{QStringLiteral("schema"), aqtruncat::api::SchemaVersion},
                                                         {QStringLiteral("generation"), double(m_generation)},
                                                         {QStringLiteral("sampleTime"), sampleTime()},
                                                         {QStringLiteral("metrics"), changed},
                                                         {QStringLiteral("removed"), removed},
                                                         {QStringLiteral("order"), order}}));
}

void BackendService::publishRunner()
{
    const QJsonObject current = runner();
    if (current == m_lastRunner)
        return;
    m_lastRunner = current;
    emit RunnerChanged(compact(current));
}

void BackendService::publishSettings()
{
    const QJsonObject current = aqtruncat::api::settingsObject(*m_settings);
    if (current == m_lastSettings)
        return;
    m_lastSettings = current;
    emit SettingsChanged(compact(current));
}

QJsonObject BackendService::info() const
{
    return QJsonObject{
        {QStringLiteral("api"), aqtruncat::api::ApiVersion},
        {QStringLiteral("schema"), aqtruncat::api::SchemaVersion},
        {QStringLiteral("service"), QStringLiteral("aqtruncatd")},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("capabilities"), capabilities()},
        {QStringLiteral("generation"), double(m_generation)},
        {QStringLiteral("startedAt"), m_startedAt.isValid() ? QJsonValue(m_startedAt.toString(Qt::ISODate)) : QJsonValue()},
        {QStringLiteral("customMetricsDirectory"), m_custom->discoveryDirectory()},
    };
}

QJsonArray BackendService::capabilities() const
{
    QJsonArray list{QStringLiteral("metrics"), QStringLiteral("metrics-delta"), QStringLiteral("runner"),
                    QStringLiteral("settings"), QStringLiteral("set-setting"), QStringLiteral("custom-metrics"),
                    QStringLiteral("integrations")};
    if (m_config.runnerCatalog)
        list.append(QStringLiteral("runners"));
    return list;
}

QJsonValue BackendService::sampleTime() const
{
    return m_system ? QJsonValue(m_system->timeSeconds) : QJsonValue();
}

QJsonObject BackendService::runner() const
{
    return QJsonObject{{QStringLiteral("runnerId"), m_settings->runnerId()},
                       {QStringLiteral("speedMode"), aqtruncat::AppSettings::speedModeName(m_settings->speedMode())},
                       {QStringLiteral("fps"), m_fps},
                       {QStringLiteral("targetFps"), m_targetFps}};
}

QString BackendService::integrationTitle(const QString &id) const
{
    return id == QLatin1String("claude-code") ? QStringLiteral("Claude Code") : QStringLiteral("Codex");
}

QJsonArray BackendService::integrations() const
{
    QJsonArray result;
    for (const ProviderIntegration *provider : {m_claude.get(), m_codex.get()}) {
        const QJsonObject &status = provider->status();
        const QString foundKey = provider->id() == QLatin1String("claude-code") ? QStringLiteral("claudeFound")
                                                                                : QStringLiteral("codexFound");
        const QFileInfo metricFile(status.value(QStringLiteral("metricFile")).toString());
        const bool enabled = status.value(QStringLiteral("enabled")).toBool();
        // Only management state: no settings-file paths, no adapter output.
        result.append(QJsonObject{
            {QStringLiteral("id"), provider->id()},
            {QStringLiteral("title"), integrationTitle(provider->id())},
            {QStringLiteral("adapterInstalled"), provider->installed()},
            {QStringLiteral("statusKnown"), provider->statusKnown()},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("userPackage"), status.value(QStringLiteral("userPackage")).toBool()},
            {QStringLiteral("providerFound"), status.value(foundKey).toBool()},
            {QStringLiteral("liveUpdates"), status.value(QStringLiteral("liveUpdates")).toString(QStringLiteral("off"))},
            {QStringLiteral("lastLiveReport"), status.value(QStringLiteral("lastLiveReport"))},
            {QStringLiteral("liveError"), status.value(QStringLiteral("liveError")).toString()},
            {QStringLiteral("metricId"), enabled && metricFile.exists()
                 ? QJsonValue(aqtruncat::metricsbar::customId(metricFile.canonicalFilePath())) : QJsonValue()},
        });
    }
    return result;
}

QJsonObject BackendService::state() const
{
    return QJsonObject{{QStringLiteral("api"), aqtruncat::api::ApiVersion},
                       {QStringLiteral("schema"), aqtruncat::api::SchemaVersion},
                       {QStringLiteral("info"), info()},
                       {QStringLiteral("generation"), double(m_generation)},
                       {QStringLiteral("sampleTime"), sampleTime()},
                       {QStringLiteral("metrics"), m_metrics},
                       {QStringLiteral("runner"), runner()},
                       {QStringLiteral("settings"), aqtruncat::api::settingsObject(*m_settings)},
                       {QStringLiteral("integrations"), integrations()}};
}

QString BackendService::GetInfo() { return compact(info()); }
QString BackendService::GetState() { return compact(state()); }
QString BackendService::GetMetrics()
{
    return compact(QJsonObject{{QStringLiteral("schema"), aqtruncat::api::SchemaVersion},
                               {QStringLiteral("generation"), double(m_generation)},
                               {QStringLiteral("sampleTime"), sampleTime()},
                               {QStringLiteral("metrics"), m_metrics}});
}
QString BackendService::GetSettings() { return compact(aqtruncat::api::settingsObject(*m_settings)); }
QString BackendService::GetRunner() { return compact(runner()); }
QString BackendService::GetIntegrations() { return compact(integrations()); }

QString BackendService::GetRunners()
{
    if (!m_config.runnerCatalog) {
        fail(aqtruncat::api::ErrorNotFound, QStringLiteral("this backend has no runner catalogue"));
        return {};
    }
    return compact(m_config.runnerCatalog(m_settings->runnerId()));
}

bool BackendService::fail(const QString &name, const QString &message)
{
    if (calledFromDBus())
        sendErrorReply(name, message);
    else
        qWarning().noquote() << message;
    return false;
}

void BackendService::SetSetting(const QString &key, const QString &jsonValue)
{
    using aqtruncat::api::ErrorInvalidArgument;
    const QJsonValue value = parseValue(jsonValue);
    aqtruncat::AppSettings &s = *m_settings;
    if (key == QLatin1String("runnerId")) {
        if (!value.isString() || !aqtruncat::AppSettings::isValidStoredRunnerId(value.toString()))
            return void(fail(ErrorInvalidArgument, QStringLiteral("runnerId must be a runner id")));
        s.setRunnerId(value.toString());
    } else if (key == QLatin1String("speedMode")) {
        const QString mode = value.toString();
        if (mode != QLatin1String("normal") && mode != QLatin1String("slow-down"))
            return void(fail(ErrorInvalidArgument, QStringLiteral("speedMode must be \"normal\" or \"slow-down\"")));
        s.setSpeedMode(mode == QLatin1String("slow-down") ? aqtruncat::SpeedMode::SlowDown : aqtruncat::SpeedMode::Normal);
    } else if (key == QLatin1String("networkMode")) {
        const QString mode = value.toString();
        if (mode != QLatin1String("automatic") && mode != QLatin1String("manual"))
            return void(fail(ErrorInvalidArgument, QStringLiteral("networkMode must be \"automatic\" or \"manual\"")));
        s.setNetworkMode(mode == QLatin1String("manual") ? aqtruncat::metrics::NetworkPolicyMode::Manual
                                                         : aqtruncat::metrics::NetworkPolicyMode::Automatic);
    } else if (key == QLatin1String("networkInterfaces")) {
        static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_.:@-]{1,15}$"));
        const std::optional<QStringList> list = stringList(value, 64);
        if (!list || std::any_of(list->cbegin(), list->cend(), [](const QString &n) { return !name.match(n).hasMatch(); }))
            return void(fail(ErrorInvalidArgument, QStringLiteral("networkInterfaces must be a list of interface names")));
        s.setNetworkInterfaces(*list);
    } else if (key == QLatin1String("dashboardRefreshSeconds")) {
        const int seconds = value.toInt(0);
        if (!value.isDouble() || (seconds != 1 && seconds != 2 && seconds != 5))
            return void(fail(ErrorInvalidArgument, QStringLiteral("dashboardRefreshSeconds must be 1, 2 or 5")));
        s.setDashboardRefreshSeconds(seconds);
    } else if (key == QLatin1String("autostart") || key == QLatin1String("metricsBarEnabled")) {
        if (!value.isBool())
            return void(fail(ErrorInvalidArgument, QStringLiteral("%1 must be true or false").arg(key)));
        if (key == QLatin1String("autostart"))
            s.setAutostartEnabled(value.toBool());
        else
            s.setMetricsBarEnabled(value.toBool());
    } else if (key == QLatin1String("metricsBarItems")) {
        const std::optional<QStringList> list = stringList(value, aqtruncat::metricsbar::MaxItems);
        if (!list || QSet<QString>(list->cbegin(), list->cend()).size() != list->size()
            || std::any_of(list->cbegin(), list->cend(), [](const QString &id) { return !aqtruncat::metricsbar::isKnownId(id); }))
            return void(fail(ErrorInvalidArgument,
                             QStringLiteral("metricsBarItems must be at most %1 distinct metric ids").arg(aqtruncat::metricsbar::MaxItems)));
        s.setMetricsBarItems(*list);
    } else {
        return void(fail(ErrorInvalidArgument, QStringLiteral("unknown or read-only setting \"%1\"").arg(key)));
    }
}

void BackendService::RegisterCustomMetric(const QString &path)
{
    QString error;
    if (!m_custom->registerFile(path, &error))
        fail(aqtruncat::api::ErrorInvalidArgument, error.isEmpty() ? QStringLiteral("cannot register %1").arg(path) : error);
}

void BackendService::UnregisterCustomMetric(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QStringList files = m_settings->customMetricFiles();
    const bool registered = std::any_of(files.cbegin(), files.cend(), [&](const QString &file) {
        return QFileInfo(file).absoluteFilePath() == absolute;
    });
    if (!registered)
        return void(fail(aqtruncat::api::ErrorNotFound, QStringLiteral("%1 is not a registered Custom Metrics file").arg(path)));
    m_custom->unregisterFile(path);
}

ProviderIntegration *BackendService::integration(const QString &id) const
{
    if (id == m_claude->id())
        return m_claude.get();
    if (id == m_codex->id())
        return m_codex.get();
    return nullptr;
}

void BackendService::integrationCommand(const QString &id, const QStringList &arguments)
{
    ProviderIntegration *provider = integration(id);
    if (!provider || !provider->installed()) {
        fail(aqtruncat::api::ErrorNotFound,
             provider ? QStringLiteral("the adapter of %1 is not installed").arg(id)
                      : QStringLiteral("unknown integration \"%1\"").arg(id));
        return;
    }
    // Only the adapter's own management commands, never anything a client
    // sends: the arguments are fixed here.
    if (calledFromDBus()) {
        setDelayedReply(true);
        m_pendingReplies.append({id, message(), connection()});
    }
    provider->run(arguments);
}

QString BackendService::SetIntegrationEnabled(const QString &id, bool enabled)
{
    ProviderIntegration *provider = integration(id);
    if (provider && provider->statusKnown() && provider->status().value(QStringLiteral("enabled")).toBool() == enabled)
        return enabled ? QStringLiteral("already enabled") : QStringLiteral("already disabled");
    integrationCommand(id, {enabled ? QStringLiteral("enable") : QStringLiteral("disable")});
    return {};
}

QString BackendService::SetIntegrationLiveUpdates(const QString &id, bool on)
{
    integrationCommand(id, {QStringLiteral("live-updates"), on ? QStringLiteral("on") : QStringLiteral("off")});
    return {};
}
