// AQTRunCat Plasmoid (F4.2): pure functions over the backend API payloads
// (docs/backend-dbus-api-cli.md). No Plasma, no D-Bus, no I/O:
// the QML files call these, and tests/plasmoid/tst_logic.qml tests them.
.pragma library

var ApiVersion = 1;
var ServiceName = "io.github.psykedady.AQTRunCat";
var ObjectPath = "/io/github/psykedady/AQTRunCat";
var InterfaceName = "io.github.psykedady.AQTRunCat1";

var Categories = ["builtin", "integration", "custom"];
var States = ["ok", "pending", "unavailable", "error"];

// Values from org.kde.plasma.workspace.dbus arrive wrapped ({value: …}) or
// as String objects; give back the plain value.
function unwrap(value) {
    if (value !== null && typeof value === "object") {
        if ("value" in value)
            return value.value;
        if (value instanceof String)
            return value.toString();
    }
    return value;
}

// JSON text (possibly wrapped) to an object; null when it is not JSON.
function parse(value) {
    var text = unwrap(value);
    if (typeof text !== "string" && !(text instanceof String))
        return null;
    try {
        return JSON.parse(String(text));
    } catch (e) {
        return null;
    }
}

// The API says: treat an unknown state as "error".
function stateOf(metric) {
    var state = metric ? metric.state : undefined;
    return States.indexOf(state) >= 0 ? state : "error";
}

// Only the backend's classification counts (never the title or the icon);
// an unknown category is shown with the custom metrics.
function categoryOf(metric) {
    var category = metric ? metric.category : undefined;
    return Categories.indexOf(category) >= 0 ? category : "custom";
}

// Number in 0…1 for a progress bar, or null: missing values never become 0.
function fraction(value) {
    if (typeof value !== "number" || !isFinite(value))
        return null;
    return Math.max(0, Math.min(1, value));
}

// Text of the value column. A metric without a valid value never shows a
// number: "…" while waiting, "—" when unavailable or in error.
function valueText(metric) {
    var state = stateOf(metric);
    if (state === "pending")
        return "…";
    if (state !== "ok")
        return "—";
    var text = metric.formattedValue;
    return typeof text === "string" && text.length > 0 ? text : "—";
}

// Secondary line: the backend's message for pending/unavailable/error,
// otherwise the detail.
function detailText(metric) {
    var state = stateOf(metric);
    if (state !== "ok") {
        if (typeof metric.message === "string" && metric.message.length > 0)
            return metric.message;
        return state === "pending" ? "Waiting for data" : state === "unavailable" ? "Unavailable" : "Error";
    }
    return typeof metric.detail === "string" ? metric.detail : "";
}

function itemRows(metric) {
    var items = metric && Array.isArray(metric.items) ? metric.items : [];
    // v2F1.4 (schema 4): display.showBar false → no bars; absent → as before.
    var bars = !metric || metric.showBar !== false;
    var rows = [];
    for (var i = 0; i < items.length; ++i) {
        var item = items[i] || {};
        var formatted = typeof item.formattedValue === "string" ? item.formattedValue : "";
        rows.push({
            title: typeof item.title === "string" ? item.title : "",
            value: formatted.length > 0 ? formatted : "—",
            detail: typeof item.detail === "string" ? item.detail : "",
            fraction: bars ? fraction(item.normalized) : null,
            warning: item.warning === true
        });
    }
    return rows;
}

// v2F1.6 (schema 6): a Custom Metric's own warnings (a missing icon): the
// metric still works; they are shown under it.
function sourceWarnings(metric) {
    var list = metric && Array.isArray(metric.warnings) ? metric.warnings : [];
    return list.filter(function (warning) { return typeof warning === "string" && warning.length > 0; });
}

// v2F1.6: what the configuration page lists, as the tray's Settings: the
// metric's own warnings, then its values outside their range (whose item
// carries the reason in its detail).
function metricWarnings(metric) {
    var warnings = sourceWarnings(metric);
    if (!metric || metric.enabled === false || stateOf(metric) === "error" || stateOf(metric) === "unavailable")
        return warnings;
    var items = Array.isArray(metric.items) ? metric.items : [];
    for (var i = 0; i < items.length; ++i) {
        var item = items[i] || {};
        if (typeof item.normalized === "number" && (item.normalized < 0 || item.normalized > 1))
            warnings.push((items.length === 1 ? "value" : item.title) + " " + item.formattedValue + " (" + item.detail + ")");
    }
    return warnings;
}

// Theme icon name or file URL of a metric icon, "" when there is none
// ("symbol" icons are SF Symbols names: no Plasma equivalent).
function iconSource(metric) {
    var icon = metric ? metric.icon : null;
    if (!icon || typeof icon !== "object")
        return "";
    if (icon.type === "file") {
        var file = typeof icon.resolved === "string" && icon.resolved.length ? icon.resolved : icon.value;
        return typeof file === "string" && file.charAt(0) === "/" ? fileUrl(file) : "";
    }
    if (icon.type === "theme")
        return typeof icon.value === "string" ? icon.value : "";
    return "";
}

// v2F1.7 (schema 7): a Custom Metric (or integration) command, or a group,
// whose icon asks the backend for a fresh sample (RefreshCustomMetric).
// Snapshot files have nothing to run; disabled metrics never run.
function refreshable(metric) {
    if (!metric || metric.enabled === false)
        return false;
    var category = categoryOf(metric);
    return (category === "custom" || category === "integration") && (metric.kind === "command" || metric.kind === "group");
}

// One flat row per metric for a ListModel. Nested items travel as JSON
// text, so that an unchanged metric compares equal and its delegate stays.
function metricRow(metric) {
    var f = metric.showBar === false ? null : fraction(metric.normalized);
    var row = {
        metricId: String(metric.id),
        category: categoryOf(metric),
        title: typeof metric.title === "string" ? metric.title : String(metric.id),
        state: stateOf(metric),
        value: valueText(metric),
        detail: detailText(metric),
        hasFraction: stateOf(metric) === "ok" && f !== null,
        fraction: f === null ? 0 : f,
        warning: false,
        icon: iconSource(metric),
        itemsJson: "[]",
        group: false,
        warnings: sourceWarnings(metric).join("\n"),
        refreshable: refreshable(metric)
    };
    var items = itemRows(metric);
    if (items.length === 1 && row.state === "ok") {
        // A single value: one row, not the same value twice. Its own title
        // (when it says more than the metric's) goes in the detail line.
        var item = items[0];
        row.value = item.value;
        row.hasFraction = item.fraction !== null;
        row.fraction = item.fraction === null ? 0 : item.fraction;
        row.warning = item.warning;
        var parts = [];
        if (item.title.length > 0 && item.title !== row.title)
            parts.push(item.title);
        if (item.detail.length > 0)
            parts.push(item.detail);
        else if (row.detail.length > 0)
            parts.push(row.detail);
        row.detail = parts.join(" · ");
    } else {
        row.itemsJson = JSON.stringify(items);
    }
    return row;
}

// FPS with its source turned OFF (data.condition "disabled") is the user's
// choice not to see it, not an unavailable value: the metric is left out.
// Every other FPS condition is shown with its status. v2F1.2 (schema 3): a
// Custom Metric whose file says "enabled": false is left out the same way
// (the configuration page still lists it).
function hiddenMetric(metric) {
    if (!metric)
        return false;
    if (metric.enabled === false)
        return true;
    return metric.id === "fps" && !!metric.data && metric.data.condition === "disabled";
}

// A Custom Metric (or integration) stays "pending" while its command runs
// again, with its last values still in the payload. Like the Qt Dashboard,
// such a refresh keeps presenting the last settled state (and message)
// instead of "Command is running." without values: the row keeps its size,
// the popup neither flickers nor scrolls. Only a source with no value yet
// shows "pending". settled maps id → {state, message}: pass the previous
// result's settled back in; the input metrics are not modified.
function settleRefreshes(metrics, settled) {
    var previous = settled || {};
    var next = {};
    var result = [];
    for (var i = 0; i < metrics.length; ++i) {
        var metric = metrics[i];
        var hasValue = !!metric && (metric.category === "custom" || metric.category === "integration")
            && Array.isArray(metric.items) && metric.items.length > 0;
        if (!hasValue) {
            result.push(metric);
            continue;
        }
        if (metric.state !== "pending") {
            next[metric.id] = { state: metric.state, message: typeof metric.message === "string" ? metric.message : "" };
            result.push(metric);
            continue;
        }
        var shown = previous[metric.id] || { state: "ok", message: "" };
        next[metric.id] = shown;
        result.push(Object.assign({}, metric, { state: shown.state, message: shown.message }));
    }
    return { metrics: result, settled: next };
}

// v2F1.5 (schema 5): a group's child carries data.groupId.
function groupIdOf(metric) {
    return metric && metric.data && typeof metric.data.groupId === "string" ? metric.data.groupId : "";
}

// The compact cells of a group: its metric descendants (nested groups
// flattened), enabled ones only, each with its own value, status and bar
// (when its showBar allows it). The backend did all the inheritance.
function groupCells(group, metrics) {
    var groups = [group.data ? group.data.sourceId : ""];
    var cells = [];
    for (var i = 0; i < metrics.length; ++i) {
        var metric = metrics[i];
        if (!metric || groups.indexOf(groupIdOf(metric)) < 0)
            continue;
        if (metric.kind === "group") {
            groups.push(metric.data ? metric.data.sourceId : "");
            continue;
        }
        if (hiddenMetric(metric))
            continue;
        var state = stateOf(metric);
        var items = itemRows(metric);
        var values = items.map(function (item) { return item.value; }).filter(function (v) { return v !== "—"; });
        var value = state === "error" ? "—" : values.length > 0 ? values.join(" · ") : state === "pending" ? "…" : "—";
        var bar = null;
        for (var k = 0; k < items.length && state !== "error"; ++k) {
            if (items[k].fraction !== null) {
                bar = items[k].fraction;
                break;
            }
        }
        // Under the cell: its own warnings, then its items' (a value
        // outside its range).
        var warnings = sourceWarnings(metric);
        for (var w = 0; w < items.length && state === "ok"; ++w) {
            if (items[w].warning && items[w].detail.length > 0)
                warnings.push(items[w].detail);
        }
        // A child's own icon (not the group's) is shown in its cell.
        var icon = iconSource(metric);
        cells.push({
            metricId: String(metric.id),
            refreshable: refreshable(metric),
            icon: icon !== iconSource(group) ? icon : "",
            title: typeof metric.title === "string" ? metric.title : "",
            value: value,
            fraction: bar,
            state: state,
            detail: state === "ok" ? "" : detailText(metric),
            warnings: warnings
        });
    }
    return cells;
}

// Rows grouped by category (built-in, integrations, custom), API order kept
// inside each group; hidden categories and hidden metrics are left out.
function metricRows(metrics, shown) {
    var groups = { builtin: [], integration: [], custom: [] };
    for (var i = 0; i < metrics.length; ++i) {
        if (hiddenMetric(metrics[i]) || groupIdOf(metrics[i]).length > 0)
            continue; // a group's children are shown inside its row
        var row = metricRow(metrics[i]);
        if (metrics[i].kind === "group") {
            var cells = groupCells(metrics[i], metrics);
            if (cells.length === 0)
                continue; // no enabled child: nothing to show
            row.group = true;
            row.state = "ok";
            row.value = "";
            row.detail = "";
            row.hasFraction = false;
            row.itemsJson = JSON.stringify(cells);
        }
        groups[row.category].push(row);
    }
    var rows = [];
    for (var c = 0; c < Categories.length; ++c) {
        var category = Categories[c];
        if (shown && shown[category] === false)
            continue;
        rows = rows.concat(groups[category]);
    }
    return rows;
}

// F6: tooltip of a popup row when its detail lines are hidden (widget
// option showDetails off): the detail and each item's detail, the
// backend's own text. Empty when the details are on screen already. The
// caller passes "" for a detail that is shown anyway (state messages,
// warnings).
function rowToolTip(detail, items, showDetails) {
    if (showDetails)
        return "";
    var lines = [];
    if (typeof detail === "string" && detail.length > 0)
        lines.push(detail);
    var list = Array.isArray(items) ? items : [];
    for (var i = 0; i < list.length; ++i) {
        var item = list[i] || {};
        // A warning's detail is always on screen.
        if (item.warning !== true && typeof item.detail === "string" && item.detail.length > 0)
            lines.push((item.title ? item.title + ": " : "") + item.detail);
    }
    return lines.join("\n");
}

// F6: the words for a missing value in the hover summary (the same as
// src/app/hoversummary.cpp): never a 0, never the backend's raw message.
function hoverStateText(state) {
    if (state === "unavailable")
        return "unavailable";
    if (state === "error")
        return "error";
    return "waiting for data";
}

// F6: the hover summary lines, from the backend's own texts:
//   CPU: 18.4 %
//   RAM: 46.0 % · 7.2 GiB / 15.5 GiB used
//   Swap: 2.5 % · 0.3 GiB / 12.0 GiB   (no line without swap)
// The Qt tray builds the same lines (src/app/hoversummary.cpp).
function hoverLines(metrics) {
    var byId = {};
    var list = Array.isArray(metrics) ? metrics : [];
    for (var i = 0; i < list.length; ++i) {
        if (list[i] && list[i].category === "builtin")
            byId[list[i].id] = list[i];
    }
    function withDetail(metric) {
        var detail = typeof metric.detail === "string" ? metric.detail : "";
        return metric.formattedValue + (detail.length > 0 ? " · " + detail : "");
    }
    function current(metric) {
        return !!metric && stateOf(metric) === "ok" && typeof metric.formattedValue === "string"
            && metric.formattedValue.length > 0;
    }
    var lines = [];
    var cpu = byId["cpu"];
    lines.push("CPU: " + (current(cpu) ? cpu.formattedValue : hoverStateText(cpu ? stateOf(cpu) : "pending")));
    var memory = byId["memory"];
    lines.push("RAM: " + (current(memory) ? withDetail(memory) : hoverStateText(memory ? stateOf(memory) : "pending")));
    var swap = byId["swap"];
    var noSwap = !!swap && stateOf(swap) === "unavailable" && !!swap.data && swap.data.present === false;
    if (!noSwap)
        lines.push("Swap: " + (current(swap) ? withDetail(swap) : hoverStateText(swap ? stateOf(swap) : "pending")));
    return lines;
}

// F6: the widget's tooltip below "AQTRunCat": the backend's state, or the
// runner and the summary. Values only while they are current (the client
// parses metrics only while something shows them, see metricsCurrent).
function hoverText(status, runnerName, metrics, current) {
    if (status === "connecting")
        return "Connecting to the backend…";
    if (status === "incompatible")
        return "Incompatible backend";
    if (status !== "connected")
        return "Backend not running";
    var lines = [typeof runnerName === "string" && runnerName.length > 0 ? runnerName : "Connected"];
    if (current)
        lines = lines.concat(hoverLines(metrics));
    return lines.join("\n");
}

// Brings a ListModel to rows with the least changes: rows are removed,
// moved or inserted one by one and the others only get the roles that
// differ, so delegates stay and the list keeps its scroll position (a
// clear() would reset the view to the top whenever a metric appears or
// goes, e.g. a Custom Metric enabled or disabled).
function syncModel(model, rows) {
    var wanted = {};
    var i;
    for (i = 0; i < rows.length; ++i)
        wanted[rows[i].metricId] = true;
    for (i = model.count - 1; i >= 0; --i) {
        if (!wanted[model.get(i).metricId])
            model.remove(i, 1);
    }
    for (i = 0; i < rows.length; ++i) {
        var id = rows[i].metricId;
        if (i >= model.count || model.get(i).metricId !== id) {
            var from = -1;
            for (var j = i + 1; j < model.count; ++j) {
                if (model.get(j).metricId === id) {
                    from = j;
                    break;
                }
            }
            if (from < 0) {
                model.insert(i, rows[i]);
                continue;
            }
            model.move(from, i, 1);
        }
        var current = model.get(i);
        for (var key in rows[i]) {
            if (current[key] !== rows[i][key])
                model.setProperty(i, key, rows[i][key]);
        }
    }
}

// MetricsChanged against the client's generation (API §6):
//   "ignore"  the same generation again (already in the state);
//   "apply"   the next one: apply the delta;
//   "resync"  a gap, or a lower one (a restarted backend): take GetState.
function deltaAction(current, generation) {
    if (generation === current)
        return "ignore";
    if (generation === current + 1)
        return "apply";
    return "resync";
}

// Applies a delta to metrics (array in API order). Returns the new array,
// or null when the delta cannot be applied (unknown ids in "order"): the
// caller then takes GetState.
function applyDelta(metrics, changes) {
    var byId = {};
    var i;
    for (i = 0; i < metrics.length; ++i)
        byId[metrics[i].id] = metrics[i];
    var changed = Array.isArray(changes.metrics) ? changes.metrics : [];
    for (i = 0; i < changed.length; ++i)
        byId[changed[i].id] = changed[i];
    var removed = Array.isArray(changes.removed) ? changes.removed : [];
    for (i = 0; i < removed.length; ++i)
        delete byId[removed[i]];
    var order = Array.isArray(changes.order) ? changes.order : null;
    if (!order)
        return null;
    var result = [];
    for (i = 0; i < order.length; ++i) {
        if (!(order[i] in byId))
            return null;
        result.push(byId[order[i]]);
    }
    return result;
}

// F5 (schema 2) shared settings: the choices of the Metrics Bar GPU, from
// the backend's "gpu:<device>" metrics. "auto" first; a chosen device that
// is not present now stays listed.
function gpuChoices(metrics, current) {
    var choices = [{ text: "Automatic (discrete GPU first)", value: "auto" }];
    var list = Array.isArray(metrics) ? metrics : [];
    for (var i = 0; i < list.length; ++i) {
        var metric = list[i] || {};
        var id = typeof metric.id === "string" ? metric.id : "";
        if (metric.category !== "builtin" || id.indexOf("gpu:") !== 0)
            continue;
        var device = id.substring(4);
        choices.push({ text: (metric.title || device) + " (" + device + ")", value: device });
    }
    if (typeof current === "string" && current.length > 0) {
        var known = false;
        for (var j = 0; j < choices.length; ++j)
            known = known || choices[j].value === current;
        if (!known)
            choices.push({ text: current + " (not present)", value: current });
    }
    return choices;
}

// Update-check intervals the backend accepts (minutes; 0 = off).
function updatesIntervalChoices() {
    var minutes = [0, 15, 30, 60, 180, 360, 720, 1440];
    var choices = [];
    for (var i = 0; i < minutes.length; ++i) {
        var m = minutes[i];
        var text = m === 0 ? "Off" : m < 60 ? "Every " + m + " minutes" : m === 60 ? "Every hour"
                 : m === 1440 ? "Once a day" : "Every " + (m / 60) + " hours";
        choices.push({ text: text, value: m });
    }
    return choices;
}

function indexOfValue(choices, value) {
    for (var i = 0; i < choices.length; ++i) {
        if (choices[i].value === value)
            return i;
    }
    return -1;
}

function hasCapability(info, name) {
    return !!info && Array.isArray(info.capabilities) && info.capabilities.indexOf(name) >= 0;
}

// file:// URL of an absolute path; every component is percent-encoded
// ("#", "?" and spaces are valid in file names).
function fileUrl(path) {
    var parts = String(path).split("/");
    for (var i = 0; i < parts.length; ++i)
        parts[i] = encodeURIComponent(parts[i]);
    return "file://" + parts.join("/");
}

// The runner to show from a GetRunners payload: the "selected" one when it
// is valid, else null (the widget then shows its icon). Frames as URLs.
function runnerToShow(catalog) {
    if (!catalog || !Array.isArray(catalog.runners))
        return null;
    for (var i = 0; i < catalog.runners.length; ++i) {
        var runner = catalog.runners[i];
        if (runner.id !== catalog.selected || runner.valid !== true)
            continue;
        if (!Array.isArray(runner.frames) || runner.frames.length === 0)
            return null;
        var count = runner.frames.length;
        var sequence = [];
        var raw = Array.isArray(runner.sequence) ? runner.sequence : [];
        for (var j = 0; j < raw.length; ++j) {
            if (typeof raw[j] === "number" && raw[j] >= 0 && raw[j] < count)
                sequence.push(raw[j]);
        }
        if (sequence.length === 0) {
            for (var k = 0; k < count; ++k)
                sequence.push(k);
        }
        var frames = [];
        for (var f = 0; f < count; ++f)
            frames.push(fileUrl(runner.frames[f]));
        return {
            id: runner.id,
            displayName: runner.displayName || runner.id,
            monochrome: runner.monochrome !== false,
            aspect: runner.frameWidth > 0 && runner.frameHeight > 0 ? runner.frameWidth / runner.frameHeight : 1,
            frames: frames,
            sequence: sequence
        };
    }
    return null;
}

// Timer interval of the local animation for the fps the backend computed.
function frameInterval(fps) {
    if (typeof fps !== "number" || !(fps > 0))
        return 0;
    return Math.max(16, Math.round(1000 / fps));
}

// Text of a D-Bus error from the backend, without its D-Bus name.
function errorText(reply) {
    if (!reply)
        return "";
    var error = reply.error;
    if (error && typeof error.message === "string" && error.message.length)
        return error.message;
    return error && typeof error.name === "string" ? error.name : "unknown error";
}

// v2F1.6 (schema 6): the line shown after ReloadCustomMetrics, the same as
// the tray's Settings (custommetrics::reloadSummary).
function reloadSummary(report) {
    var count = function (key) { return report && Array.isArray(report[key]) ? report[key].length : 0; };
    var text = "Custom Metrics reloaded: " + count("added") + " added, " + count("changed") + " changed, "
        + count("removed") + " removed, " + (report && typeof report.unchanged === "number" ? report.unchanged : 0)
        + " unchanged.";
    var named = function (entries) {
        var names = entries.slice(0, 3).map(function (entry) { return entry.name + " (" + entry.message + ")"; });
        if (entries.length > 3)
            names.push((entries.length - 3) + " more");
        return names.join("; ");
    };
    var errors = report && Array.isArray(report.errors) ? report.errors : [];
    if (errors.length)
        text += " " + errors.length + " with errors: " + named(errors) + ".";
    var warnings = report && Array.isArray(report.warnings) ? report.warnings : [];
    if (warnings.length)
        text += " " + warnings.length + " with warnings: " + named(warnings) + ".";
    return text;
}

// A refused reload: the backend's reason; nothing was changed.
function reloadFailure(message) {
    return "Custom Metrics not reloaded: " + message + " The active configuration is unchanged.";
}

// v2F1.7 (schema 7): the line shown after RefreshCustomMetric. The new
// values arrive as MetricsChanged; this only says what was asked.
function refreshSummary(result) {
    var started = result && Array.isArray(result.started) ? result.started.length : 0;
    var coalesced = result && Array.isArray(result.coalesced) ? result.coalesced.length : 0;
    if (started > 0)
        return "Refreshing…";
    if (coalesced > 0)
        return "Already running or just refreshed";
    return "Nothing to refresh: no enabled metric";
}
