/*
 * AQTRunCat Plasmoid (F4.2): client of the AQTRunCat backend (aqtruncatd)
 * through its public D-Bus API only (docs/backend-dbus-api-cli.md).
 * Every call is asynchronous: plasmashell never waits for the backend.
 *
 * - First call (GetState) with D-Bus activation: the bus starts aqtruncatd
 *   when it is not running. One backend per session however many widgets
 *   or frontends ask.
 * - Then MetricsChanged deltas by generation, RunnerChanged,
 *   SettingsChanged and IntegrationsChanged. A gap or a lower generation
 *   (restarted backend) takes the whole state again.
 * - Backend gone: the values are kept for a 3 s grace period (a quick
 *   restart does not flicker), then status becomes "unavailable" and the
 *   client retries with a growing delay (3 s up to 30 s). A new owner of
 *   the name is picked up at once.
 * - Another major API version: "incompatible", no further calls until the
 *   name changes owner.
 */
import QtQuick
import org.kde.plasma.workspace.dbus as DBus

import "../code/logic.js" as Logic

Item {
    id: client
    visible: false

    // "connecting" | "connected" | "unavailable" | "incompatible"
    property string status: "connecting"
    property string statusMessage: ""
    property var info: ({})
    property var runner: ({})
    property var settings: ({})
    property var integrations: []
    property var metrics: []
    // GetRunners payload; null without the "runners" capability or before it.
    property var runnerCatalog: null
    readonly property bool connected: status === "connected"
    readonly property bool hasRunnerCatalog: Logic.hasCapability(info, "runners")
    // false: metric deltas are not parsed (popup closed); set back to true,
    // the metrics are taken again with GetMetrics.
    property bool wantMetrics: true
    // F6: metrics holds current values (not the ones from before they were
    // last skipped); the hover summary shows values only then.
    readonly property bool metricsCurrent: connected && wantMetrics && !_metricsStale
    // Retry delays after a failed connection, ms (tests shorten them).
    property int graceMs: 3000
    property int retryMinMs: 3000
    property int retryMaxMs: 30000

    // Result of a write: ok, and the backend's message or error text.
    signal replied(string member, bool ok, string message)

    property real generation: 0
    property bool _requesting: false
    property bool _metricsStale: false
    property bool _runnersRequested: false
    property var _pending: []

    function call(member, args, signature, onDone) {
        const reply = DBus.SessionBus.asyncCall({
            service: Logic.ServiceName,
            path: Logic.ObjectPath,
            iface: Logic.InterfaceName,
            member: member,
            arguments: args || [],
            signature: signature || ""
        });
        // Keep the pending reply referenced until it has finished.
        _pending.push(reply);
        reply.finished.connect(() => {
            // The widget may be gone (removed from the panel) by now.
            if (!client || !Array.isArray(client._pending))
                return;
            const index = client._pending.indexOf(reply);
            if (index >= 0)
                client._pending.splice(index, 1);
            const values = reply.values || [];
            onDone(!reply.isError, reply.isError ? null : Logic.unwrap(values[0]), reply);
        });
    }

    // Whole state: start, recovery, resynchronisation.
    function refresh() {
        if (_requesting)
            return;
        _requesting = true;
        call("GetState", [], "", (ok, value, reply) => {
            _requesting = false;
            if (!ok) {
                failed(Logic.errorText(reply));
                return;
            }
            const state = Logic.parse(value);
            if (!state) {
                failed("the backend sent an unreadable state");
                return;
            }
            if (state.api !== Logic.ApiVersion) {
                retryTimer.stop();
                graceTimer.stop();
                status = "incompatible";
                statusMessage = "This widget needs AQTRunCat API " + Logic.ApiVersion + "; the running backend has API " + state.api + ".";
                return;
            }
            retryTimer.stop();
            graceTimer.stop();
            retryTimer.interval = retryMinMs;
            info = state.info || {};
            generation = Number(state.generation) || 0;
            metrics = Array.isArray(state.metrics) ? state.metrics : [];
            _metricsStale = false;
            settings = state.settings || {};
            integrations = Array.isArray(state.integrations) ? state.integrations : [];
            const runnerChanged = !runner || runner.runnerId !== (state.runner || {}).runnerId;
            runner = state.runner || {};
            status = "connected";
            statusMessage = "";
            if (runnerChanged || !runnerCatalog)
                reloadRunners();
        });
    }

    function refreshMetrics() {
        call("GetMetrics", [], "", (ok, value) => {
            const payload = ok ? Logic.parse(value) : null;
            if (!payload) {
                refresh();
                return;
            }
            generation = Number(payload.generation) || 0;
            metrics = Array.isArray(payload.metrics) ? payload.metrics : [];
            _metricsStale = false;
        });
    }

    function reloadRunners() {
        if (!hasRunnerCatalog) {
            runnerCatalog = null;
            return;
        }
        call("GetRunners", [], "", (ok, value) => {
            const catalog = ok ? Logic.parse(value) : null;
            if (catalog)
                runnerCatalog = catalog;
        });
    }

    function failed(message) {
        if (status === "connected" || status === "connecting") {
            if (!graceTimer.running)
                graceTimer.start();
        }
        statusMessage = message;
        if (!retryTimer.running)
            retryTimer.start();
    }

    // The backend left the bus: keep the values for the grace period.
    function lost() {
        if (status === "incompatible") {
            status = "connecting";
            return;
        }
        if (!graceTimer.running && status !== "unavailable")
            graceTimer.start();
        if (!retryTimer.running)
            retryTimer.start();
    }

    // --- writes (the backend validates; effects come back as signals) ---

    function setSetting(key, value) {
        call("SetSetting", [new DBus.string(key), new DBus.string(JSON.stringify(value))], "(ss)",
             (ok, result, reply) => replied("SetSetting", ok, ok ? "" : Logic.errorText(reply)));
    }

    function setIntegrationEnabled(id, enabled) {
        call("SetIntegrationEnabled", [new DBus.string(id), new DBus.bool(enabled)], "(sb)",
             (ok, result, reply) => replied("SetIntegrationEnabled", ok, ok ? String(result || "") : Logic.errorText(reply)));
    }

    function setIntegrationLiveUpdates(id, on) {
        call("SetIntegrationLiveUpdates", [new DBus.string(id), new DBus.bool(on)], "(sb)",
             (ok, result, reply) => replied("SetIntegrationLiveUpdates", ok, ok ? String(result || "") : Logic.errorText(reply)));
    }

    function registerCustomMetric(path) {
        call("RegisterCustomMetric", [new DBus.string(path)], "(s)",
             (ok, result, reply) => replied("RegisterCustomMetric", ok, ok ? "" : Logic.errorText(reply)));
    }

    // v2F1.2 (schema 3): writes "enabled" into the Custom Metric's file.
    function setCustomMetricEnabled(path, enabled) {
        call("SetCustomMetricEnabled", [new DBus.string(path), new DBus.bool(enabled)], "(sb)",
             (ok, result, reply) => replied("SetCustomMetricEnabled", ok, ok ? "" : Logic.errorText(reply)));
    }

    function unregisterCustomMetric(path) {
        call("UnregisterCustomMetric", [new DBus.string(path)], "(s)",
             (ok, result, reply) => replied("UnregisterCustomMetric", ok, ok ? "" : Logic.errorText(reply)));
    }

    onWantMetricsChanged: {
        if (wantMetrics && _metricsStale && connected)
            refreshMetrics();
    }

    Component.onCompleted: refresh()

    Timer {
        id: graceTimer
        interval: client.graceMs
        onTriggered: {
            client.status = "unavailable";
            if (!client.statusMessage)
                client.statusMessage = "The AQTRunCat backend (aqtruncatd) is not running.";
        }
    }

    Timer {
        id: retryTimer
        interval: client.retryMinMs
        repeat: true
        onTriggered: {
            interval = Math.min(client.retryMaxMs, interval * 2);
            client.refresh();
        }
    }

    DBus.DBusServiceWatcher {
        busType: DBus.BusType.Session
        watchedService: Logic.ServiceName
        onRegisteredChanged: {
            if (registered) {
                client.resetRetry();
                client.refresh();
            } else {
                client.lost();
            }
        }
    }

    function resetRetry() {
        retryTimer.stop();
        retryTimer.interval = retryMinMs;
    }

    // Signals match the well-known name: they follow a restarted backend.
    DBus.SignalWatcher {
        service: Logic.ServiceName
        path: Logic.ObjectPath
        iface: Logic.InterfaceName

        function dbusMetricsChanged(generation, changes) {
            if (!client.connected || client._requesting)
                return;
            const next = Number(Logic.unwrap(generation));
            const action = Logic.deltaAction(client.generation, next);
            if (action === "ignore")
                return;
            if (!client.wantMetrics || client._metricsStale) {
                // Not shown (or GetMetrics on its way): skip the parsing;
                // the metrics are taken whole when they are shown.
                client._metricsStale = true;
                client.generation = next;
                return;
            }
            if (action === "resync") {
                client.refresh();
                return;
            }
            const delta = Logic.parse(changes);
            const updated = delta ? Logic.applyDelta(client.metrics, delta) : null;
            if (!updated) {
                client.refresh();
                return;
            }
            client.generation = next;
            client.metrics = updated;
        }

        function dbusRunnerChanged(runner) {
            const value = Logic.parse(runner);
            if (!value || !client.connected)
                return;
            const runnerChanged = client.runner.runnerId !== value.runnerId;
            client.runner = value;
            if (runnerChanged)
                client.reloadRunners();
        }

        function dbusSettingsChanged(settings) {
            const value = Logic.parse(settings);
            if (value && client.connected)
                client.settings = value;
        }

        function dbusIntegrationsChanged(integrations) {
            const value = Logic.parse(integrations);
            if (Array.isArray(value) && client.connected)
                client.integrations = value;
        }
    }
}
