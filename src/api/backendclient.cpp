#include "backendclient.h"

#include "apischema.h"

#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>

namespace {

QJsonObject parseObject(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

std::optional<double> sampleTimeOf(const QJsonObject &object)
{
    const QJsonValue value = object.value(QStringLiteral("sampleTime"));
    return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
}

bool isSystemMetric(const QJsonObject &metric)
{
    return metric.value(QStringLiteral("category")) == QLatin1String("builtin");
}

} // namespace

BackendClient::BackendClient(Options options, QObject *parent)
    : CustomMetricsModel(parent)
    , m_options(std::move(options))
    , m_watcher(aqtruncat::api::ServiceName, m_options.connection, QDBusServiceWatcher::WatchForOwnerChange)
{
    m_grace.setSingleShot(true);
    m_grace.setInterval(m_options.graceMs);
    connect(&m_grace, &QTimer::timeout, this, &BackendClient::reportUnavailable);
    m_retry.setInterval(m_options.retryMs);
    connect(&m_retry, &QTimer::timeout, this, &BackendClient::requestState);
}

void BackendClient::start()
{
    QDBusConnection &bus = m_options.connection;
    // Signals match the well-known name, so they follow a restarted backend.
    const QString service = aqtruncat::api::ServiceName;
    const QString path = aqtruncat::api::ObjectPath;
    const QString interface = aqtruncat::api::InterfaceName;
    bus.connect(service, path, interface, QStringLiteral("MetricsChanged"), this,
                SLOT(onMetricsChanged(qulonglong,QString)));
    bus.connect(service, path, interface, QStringLiteral("RunnerChanged"), this, SLOT(onRunnerChanged(QString)));
    bus.connect(service, path, interface, QStringLiteral("SettingsChanged"), this, SLOT(onSettingsChanged(QString)));
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &newOwner) {
                if (newOwner.isEmpty()) {
                    lost();
                } else {
                    m_synced = false; // a (new) backend: take its whole state
                    requestState();
                }
            });
    requestState();
}

void BackendClient::requestState()
{
    if (m_requesting)
        return;
    m_requesting = true;
    // Auto-start: the bus activates the backend if it has a service file.
    QDBusMessage call = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                       aqtruncat::api::InterfaceName, QStringLiteral("GetState"));
    call.setAutoStartService(true);
    auto *watcher = new QDBusPendingCallWatcher(m_options.connection.asyncCall(call, 15000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_requesting = false;
        const QDBusPendingReply<QString> reply = *w;
        if (reply.isError()) {
            const QString name = reply.error().name();
            if (name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
                || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")
                || name == QLatin1String("org.freedesktop.DBus.Error.Spawn.ServiceNotFound"))
                startBackendIfPossible();
            if (!m_retry.isActive())
                m_retry.start();
            if (!m_synced && !m_grace.isActive())
                m_grace.start(); // first connection: no values yet, report soon
            return;
        }
        m_retry.stop();
        m_grace.stop();
        applyState(parseObject(reply.value()));
    });
}

void BackendClient::startBackendIfPossible()
{
    if (m_options.backendExecutable.isEmpty())
        return;
    if (m_lastStart.isValid() && m_lastStart.elapsed() < 10000)
        return;
    m_lastStart.start();
    // Detached: the backend outlives this frontend, as with activation. A
    // second backend started by someone else at the same time exits at
    // once (name ownership), so this never creates a duplicate.
    QProcess::startDetached(m_options.backendExecutable, {});
}

void BackendClient::applyState(const QJsonObject &state)
{
    m_generation = quint64(state.value(QStringLiteral("generation")).toDouble());
    m_sampleTime = sampleTimeOf(state);
    m_metrics = state.value(QStringLiteral("metrics")).toArray();
    m_discoveryDirectory = state.value(QStringLiteral("info")).toObject()
                               .value(QStringLiteral("customMetricsDirectory")).toString();
    const bool wasSynced = m_synced;
    m_synced = true;
    publishMetrics(true, true);
    applyRunner(state.value(QStringLiteral("runner")).toObject());
    emit settingsChanged();
    if (!wasSynced)
        emit connectionChanged(true);
}

void BackendClient::applyRunner(const QJsonObject &runner)
{
    if (runner.isEmpty() || runner == m_runner)
        return;
    m_runner = runner;
    emit runnerChanged(runner.value(QStringLiteral("fps")).toInt(10), runner.value(QStringLiteral("runnerId")).toString());
}

void BackendClient::publishMetrics(bool system, bool custom)
{
    if (system) {
        aqtruncat::metrics::SystemMetricsSnapshot snapshot = aqtruncat::api::systemSnapshotFromMetrics(m_metrics);
        snapshot.timeSeconds = m_sampleTime.value_or(aqtruncat::metrics::SystemMetricsSampler::now());
        emit systemSnapshot(snapshot);
    }
    if (custom) {
        m_sources = aqtruncat::api::sourcesFromMetrics(m_metrics);
        emit sourcesChanged(m_sources);
    }
}

void BackendClient::onMetricsChanged(qulonglong generation, const QString &changes)
{
    if (!m_synced || generation <= m_generation)
        return; // a state request is (or was) responsible for this
    if (generation != m_generation + 1) {
        requestState(); // missed an update: take the whole state again
        return;
    }
    const QJsonObject delta = parseObject(changes);
    QHash<QString, QJsonObject> byId;
    for (const QJsonValue &value : std::as_const(m_metrics))
        byId.insert(value.toObject().value(QStringLiteral("id")).toString(), value.toObject());
    bool system = false;
    bool custom = false;
    for (const QJsonValue &value : delta.value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject metric = value.toObject();
        byId.insert(metric.value(QStringLiteral("id")).toString(), metric);
        (isSystemMetric(metric) ? system : custom) = true;
    }
    for (const QJsonValue &value : delta.value(QStringLiteral("removed")).toArray()) {
        const QJsonObject removed = byId.take(value.toString());
        (isSystemMetric(removed) ? system : custom) = true;
    }
    QJsonArray ordered;
    const QJsonArray order = delta.value(QStringLiteral("order")).toArray();
    for (const QJsonValue &id : order) {
        if (byId.contains(id.toString()))
            ordered.append(byId.value(id.toString()));
    }
    if (ordered.size() != byId.size()) {
        requestState(); // inconsistent delta: never guess
        return;
    }
    if (!custom) {
        // A changed order of custom sources counts as a custom change.
        QStringList before;
        for (const QJsonValue &value : std::as_const(m_metrics))
            before << value.toObject().value(QStringLiteral("id")).toString();
        QStringList after;
        for (const QJsonValue &value : order)
            after << value.toString();
        custom = before != after;
    }
    m_metrics = ordered;
    m_generation = generation;
    m_sampleTime = sampleTimeOf(delta);
    publishMetrics(system, custom);
}

void BackendClient::onRunnerChanged(const QString &runner)
{
    if (m_synced)
        applyRunner(parseObject(runner));
}

void BackendClient::onSettingsChanged(const QString &)
{
    emit settingsChanged();
}

void BackendClient::lost()
{
    m_synced = false;
    if (!m_grace.isActive())
        m_grace.start();
    if (!m_retry.isActive())
        m_retry.start();
}

void BackendClient::reportUnavailable()
{
    if (m_synced)
        return;
    // Keep the presentation honest: the values are not current any more.
    const std::string why = "the AQTRunCat backend (aqtruncatd) is not running";
    aqtruncat::metrics::SystemMetricsSnapshot snapshot;
    snapshot.timeSeconds = aqtruncat::metrics::SystemMetricsSampler::now();
    const aqtruncat::metrics::MetricStatus missing = aqtruncat::metrics::MetricStatus::unavailable(why);
    snapshot.cpu.status = missing;
    snapshot.memory.status = missing;
    snapshot.network.status = missing;
    snapshot.diskIo.status = missing;
    snapshot.uptime.status = missing;
    snapshot.gpu.status = missing;
    snapshot.fps.status = missing;
    snapshot.packageUpdates.status = missing;
    snapshot.flatpakUpdates.status = missing;
    snapshot.kernel.status = missing;
    snapshot.distribution.status = missing;
    // The storage rows the frontend already shows must not keep old values.
    for (const QJsonValue &value : std::as_const(m_metrics)) {
        const QJsonObject metric = value.toObject();
        if (!metric.value(QStringLiteral("id")).toString().startsWith(QLatin1String("storage.")))
            continue;
        aqtruncat::metrics::StorageMetrics storage;
        storage.path = metric.value(QStringLiteral("data")).toObject().value(QStringLiteral("path")).toString().toStdString();
        storage.status = missing;
        snapshot.storage.push_back(storage);
    }
    emit systemSnapshot(snapshot);
    for (aqtruncat::custommetrics::Source &source : m_sources) {
        source.state = aqtruncat::custommetrics::State::Unavailable;
        source.message = QString::fromStdString(why);
    }
    emit sourcesChanged(m_sources);
    emit connectionChanged(false);
    startBackendIfPossible();
}

bool BackendClient::registerFile(const QString &path, QString *error)
{
    QDBusMessage call = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                       aqtruncat::api::InterfaceName, QStringLiteral("RegisterCustomMetric"));
    call << path;
    const QDBusMessage reply = m_options.connection.call(call, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

bool BackendClient::setEnabled(const QString &path, bool enabled, QString *error)
{
    QDBusMessage call = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                       aqtruncat::api::InterfaceName, QStringLiteral("SetCustomMetricEnabled"));
    call << path << enabled;
    const QDBusMessage reply = m_options.connection.call(call, QDBus::Block, 10000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

void BackendClient::unregisterFile(const QString &path)
{
    QDBusMessage call = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                       aqtruncat::api::InterfaceName, QStringLiteral("UnregisterCustomMetric"));
    call << path;
    m_options.connection.asyncCall(call);
}
