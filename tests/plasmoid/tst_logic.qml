// F4.2: the Plasmoid's pure functions (contents/code/logic.js), with
// qmltestrunner. No Plasma, no D-Bus: API payloads as literals.
import QtQuick
import QtTest

import "../../plasmoid/package/contents/code/logic.js" as Logic

TestCase {
    name: "PlasmoidLogic"

    function metric(fields) {
        const base = { id: "cpu", category: "builtin", kind: "system", integration: null, title: "CPU",
                       state: "ok", message: "", value: 22.4, unit: "percent", formattedValue: "22.4 %",
                       detail: "", compactValue: "22%", compactLabel: "CPU", normalized: 0.224,
                       updatedAt: null, icon: null, items: [], data: {} };
        return Object.assign(base, fields || {});
    }

    ListModel { id: model }

    function test_unwrapAndParse() {
        compare(Logic.unwrap({ value: 42 }), 42);
        compare(Logic.unwrap("x"), "x");
        compare(Logic.unwrap(new String("y")), "y");
        compare(Logic.parse({ value: '{"a":1}' }).a, 1);
        compare(Logic.parse("not json"), null);
        compare(Logic.parse(null), null);
    }

    function test_states() {
        compare(Logic.stateOf(metric()), "ok");
        compare(Logic.stateOf(metric({ state: "pending" })), "pending");
        compare(Logic.stateOf(metric({ state: "unavailable" })), "unavailable");
        // Unknown states are errors (API: clients must treat them so).
        compare(Logic.stateOf(metric({ state: "weird" })), "error");
        compare(Logic.stateOf(metric({ state: undefined })), "error");
    }

    function test_categoriesComeFromTheBackendOnly() {
        // A title or an icon never decides the category.
        compare(Logic.categoryOf(metric({ title: "Claude Code", category: "custom" })), "custom");
        compare(Logic.categoryOf(metric({ title: "CPU", category: "integration", integration: "codex" })), "integration");
        compare(Logic.categoryOf(metric({ category: "future" })), "custom");
        const rows = Logic.metricRows([
            metric({ id: "custom:/a.json", category: "custom", title: "A" }),
            metric({ id: "cpu" }),
            metric({ id: "custom:/claude.json", category: "integration", integration: "claude-code", title: "Claude Code" }),
            metric({ id: "memory", title: "Memory" })
        ]);
        compare(rows.map(r => r.metricId), ["cpu", "memory", "custom:/claude.json", "custom:/a.json"]);
        compare(rows.map(r => r.category), ["builtin", "builtin", "integration", "custom"]);
        const hidden = Logic.metricRows([metric({ id: "cpu" }), metric({ id: "custom:/a.json", category: "custom" })],
                                        { builtin: true, integration: true, custom: false });
        compare(hidden.length, 1);
        compare(Logic.metricRows([], {}).length, 0);
    }

    function test_fpsSourceOffHidesFps() {
        function fps(condition, state, message, extra) {
            return metric(Object.assign({ id: "fps", title: "FPS", state: state, message: message, value: null,
                                          unit: "fps", formattedValue: "", compactValue: null, normalized: null,
                                          data: { source: condition === "disabled" ? "" : "mangohud",
                                                  condition: condition } }, extra || {}));
        }
        const others = [metric({ id: "cpu" }), metric({ id: "uptime", title: "Uptime", formattedValue: "1h 0m" })];
        // OFF: no row at all, the other built-in metrics stay as they are.
        const off = Logic.metricRows(others.concat([fps("disabled", "unavailable", "the FPS source is turned off")]));
        compare(off.map(r => r.metricId), ["cpu", "uptime"]);
        compare(off[1].value, "1h 0m");
        verify(Logic.hiddenMetric(fps("disabled", "unavailable", "the FPS source is turned off")));
        // With a source, every condition is a visible row with its status.
        const visible = [["missing", "unavailable", "MangoHud is not installed"],
                         ["not-configured", "unavailable", "MangoHud has no output_folder"],
                         ["inactive", "unavailable", "no game is logging with MangoHud now"],
                         ["stale", "unavailable", "the MangoHud log has not been written for 12 s"],
                         ["unsupported", "error", "the MangoHud log has no frame-metrics header"]];
        for (let i = 0; i < visible.length; ++i) {
            const rows = Logic.metricRows(others.concat([fps(visible[i][0], visible[i][1], visible[i][2])]));
            compare(rows.map(r => r.metricId), ["cpu", "uptime", "fps"], visible[i][0]);
            compare(rows[2].value, "—");
            compare(rows[2].detail, visible[i][2]);
            verify(!Logic.hiddenMetric(fps(visible[i][0], visible[i][1], visible[i][2])));
        }
        const measured = Logic.metricRows([fps("measured", "ok", "", { value: 144, formattedValue: "144 FPS" })]);
        compare(measured[0].value, "144 FPS");
        // Only FPS follows this rule: another metric's data never hides it.
        verify(!Logic.hiddenMetric(metric({ id: "cpu", data: { condition: "disabled" } })));
        verify(!Logic.hiddenMetric(null));
    }

    function test_fpsSourceSwitchUpdatesTheModel() {
        // OFF ↔ MangoHud through MetricsChanged deltas reaches the ListModel.
        const on = metric({ id: "fps", title: "FPS", state: "unavailable", message: "MangoHud is not installed",
                            formattedValue: "", data: { source: "mangohud", condition: "missing" } });
        const off = metric({ id: "fps", title: "FPS", state: "unavailable", message: "the FPS source is turned off",
                             formattedValue: "", data: { source: "", condition: "disabled" } });
        let metrics = [metric({ id: "cpu" }), off];
        model.clear();
        Logic.syncModel(model, Logic.metricRows(metrics));
        compare(model.count, 1);
        metrics = Logic.applyDelta(metrics, { metrics: [on], removed: [], order: ["cpu", "fps"] });
        Logic.syncModel(model, Logic.metricRows(metrics));
        compare(model.count, 2);
        compare(model.get(1).metricId, "fps");
        compare(model.get(1).detail, "MangoHud is not installed");
        metrics = Logic.applyDelta(metrics, { metrics: [off], removed: [], order: ["cpu", "fps"] });
        Logic.syncModel(model, Logic.metricRows(metrics));
        compare(model.count, 1);
        compare(model.get(0).metricId, "cpu");
    }

    function test_missingValuesNeverBecomeZero() {
        const pending = Logic.metricRow(metric({ state: "pending", value: null, normalized: null, formattedValue: "",
                                                 message: "waiting for the first sample" }));
        compare(pending.value, "…");
        compare(pending.hasFraction, false);
        compare(pending.detail, "waiting for the first sample");
        const unavailable = Logic.metricRow(metric({ state: "unavailable", value: null, normalized: null,
                                                     formattedValue: "", message: "no swap" }));
        compare(unavailable.value, "—");
        compare(unavailable.hasFraction, false);
        compare(unavailable.detail, "no swap");
        const error = Logic.metricRow(metric({ state: "error", formattedValue: "12 %", normalized: 0.12, message: "" }));
        compare(error.value, "—");
        compare(error.hasFraction, false);
        compare(error.detail, "Error");
        const noFraction = Logic.metricRow(metric({ id: "network.download", normalized: null, formattedValue: "8.9 KiB/s" }));
        compare(noFraction.value, "8.9 KiB/s");
        compare(noFraction.hasFraction, false);
        compare(Logic.fraction(null), null);
        compare(Logic.fraction(NaN), null);
        compare(Logic.fraction(1.7), 1);
        compare(Logic.fraction(0), 0);
    }

    function test_multiValueProviderMetrics() {
        const claude = metric({
            id: "custom:/claude.json", category: "integration", integration: "claude-code", kind: "command",
            title: "Claude Code", formattedValue: "26% used", normalized: null,
            items: [
                { id: "five_hour", title: "Current session", formattedValue: "26% used", detail: "Resets Oct 23, 20:00",
                  normalized: 0.26, compactValue: "26%", compactLabel: "cc5h", warning: false },
                { id: "seven_day", title: "Current week", formattedValue: "", detail: "Cannot be calculated reliably",
                  normalized: null, compactValue: null, compactLabel: "cc7d", warning: true }
            ]
        });
        const row = Logic.metricRow(claude);
        compare(row.category, "integration");
        const items = JSON.parse(row.itemsJson);
        compare(items.length, 2);
        compare(items[0].value, "26% used");
        compare(items[0].fraction, 0.26);
        compare(items[0].detail, "Resets Oct 23, 20:00");
        // Not computable: a warning and a dash, never an invented percentage.
        compare(items[1].warning, true);
        compare(items[1].value, "—");
        compare(items[1].fraction, null);
    }

    function test_singleItemIsOneRow() {
        const row = Logic.metricRow(metric({
            id: "custom:/w.json", category: "custom", title: "Outside", formattedValue: "14 °C", normalized: null,
            items: [{ id: null, title: "Temperature", formattedValue: "14 °C", detail: "", normalized: null, warning: false }]
        }));
        compare(row.itemsJson, "[]");
        compare(row.value, "14 °C");
        compare(row.detail, "Temperature");
        const stale = Logic.metricRow(metric({
            id: "custom:/s.json", category: "custom", title: "Build", formattedValue: "ok",
            items: [{ title: "Build", formattedValue: "ok", detail: "Out of date", normalized: 0.5, warning: true }]
        }));
        compare(stale.warning, true);
        compare(stale.detail, "Out of date");
        compare(stale.hasFraction, true);
    }

    function test_icons() {
        compare(Logic.iconSource(metric({ icon: { type: "theme", value: "cpu", resolved: "" } })), "cpu");
        compare(Logic.iconSource(metric({ icon: { type: "file", value: "x.svg", resolved: "/a b/x#1.svg" } })),
                "file:///a%20b/x%231.svg");
        compare(Logic.iconSource(metric({ icon: { type: "symbol", value: "staroflife" } })), "");
        compare(Logic.iconSource(metric({ icon: { type: "file", value: "relative.svg", resolved: "" } })), "");
        compare(Logic.iconSource(metric()), "");
    }

    function test_deltaGenerations() {
        compare(Logic.deltaAction(10, 10), "ignore");
        compare(Logic.deltaAction(10, 11), "apply");
        compare(Logic.deltaAction(10, 13), "resync");
        // Lower: a restarted backend counts from 1 again.
        compare(Logic.deltaAction(352, 2), "resync");
    }

    function test_applyDelta() {
        const metrics = [metric({ id: "cpu" }), metric({ id: "memory" }), metric({ id: "custom:/a.json", category: "custom" })];
        const updated = Logic.applyDelta(metrics, {
            generation: 5, metrics: [metric({ id: "cpu", formattedValue: "50 %" }), metric({ id: "custom:/b.json", category: "custom" })],
            removed: ["custom:/a.json"], order: ["cpu", "memory", "custom:/b.json"]
        });
        compare(updated.map(m => m.id), ["cpu", "memory", "custom:/b.json"]);
        compare(updated[0].formattedValue, "50 %");
        compare(metrics[0].formattedValue, "22.4 %"); // the input is not modified
        compare(Logic.applyDelta(metrics, { metrics: [], removed: [], order: ["cpu", "unknown"] }), null);
        compare(Logic.applyDelta(metrics, { metrics: [] }), null);
    }

    function test_syncModelKeepsDelegates() {
        model.clear();
        const first = Logic.metricRows([metric({ id: "cpu" }), metric({ id: "memory", title: "Memory" })]);
        Logic.syncModel(model, first);
        compare(model.count, 2);
        const second = Logic.metricRows([metric({ id: "cpu", formattedValue: "99 %" }), metric({ id: "memory", title: "Memory" })]);
        let changed = [];
        const watcher = (topLeft) => changed.push(topLeft.row);
        model.dataChanged.connect(watcher);
        Logic.syncModel(model, second);
        model.dataChanged.disconnect(watcher);
        compare(model.count, 2);
        compare(model.get(0).value, "99 %");
        compare(changed, [0]); // only the row that changed
        Logic.syncModel(model, Logic.metricRows([metric({ id: "memory" })]));
        compare(model.count, 1);
        compare(model.get(0).metricId, "memory");
    }

    function test_runnerToShow() {
        const catalog = {
            schema: 1, requested: "gone", selected: "cat", defaultRunnerId: "cat",
            runners: [
                { id: "cat", displayName: "Cat", source: "bundled", valid: true, monochrome: true, frameWidth: 56, frameHeight: 36,
                  frames: ["/usr/share/aqtruncat/runners/cat/cat-frame-0.png", "/usr/share/aqtruncat/runners/cat/cat-frame-1.png"],
                  sequence: [1, 0, 7, -1] },
                { id: "broken", valid: false, frames: [], sequence: [] }
            ]
        };
        const runner = Logic.runnerToShow(catalog);
        compare(runner.id, "cat");
        compare(runner.monochrome, true);
        compare(runner.aspect, 56 / 36);
        compare(runner.frames[0], "file:///usr/share/aqtruncat/runners/cat/cat-frame-0.png");
        compare(runner.sequence, [1, 0]); // indices outside the frames are dropped
        compare(Logic.runnerToShow(Object.assign({}, catalog, { selected: "broken" })), null);
        compare(Logic.runnerToShow(null), null);
        const unordered = Logic.runnerToShow({ selected: "a", runners: [{ id: "a", valid: true, monochrome: false,
                                                 frameWidth: 10, frameHeight: 36, frames: ["/a/0.png", "/a/1.png", "/a/2.png"], sequence: [] }] });
        compare(unordered.sequence, [0, 1, 2]);
        compare(unordered.monochrome, false);
    }

    function test_frameInterval() {
        compare(Logic.frameInterval(10), 100);
        compare(Logic.frameInterval(11), 91);
        compare(Logic.frameInterval(1000), 16);
        compare(Logic.frameInterval(0), 0);
        compare(Logic.frameInterval(undefined), 0);
    }

    function test_capabilities() {
        verify(Logic.hasCapability({ capabilities: ["metrics", "runners"] }, "runners"));
        verify(!Logic.hasCapability({ capabilities: ["metrics"] }, "runners"));
        verify(!Logic.hasCapability({}, "runners"));
        verify(!Logic.hasCapability(null, "runners"));
    }

    // F5 (schema 2)
    function test_gpuChoices() {
        const metrics = [metric(),
                         metric({ id: "gpu:0000:00:02.0", title: "Intel Iris Xe" }),
                         metric({ id: "gpu:0000:03:00.0", title: "AMD Radeon" }),
                         metric({ id: "gpu:custom", category: "custom" })];
        let choices = Logic.gpuChoices(metrics, "auto");
        compare(choices.length, 3);
        compare(choices[0].value, "auto");
        compare(choices[1].value, "0000:00:02.0");
        compare(choices[1].text, "Intel Iris Xe (0000:00:02.0)");
        compare(Logic.indexOfValue(choices, "0000:03:00.0"), 2);
        // A chosen GPU that is gone stays visible and selected.
        choices = Logic.gpuChoices([metric()], "0000:09:00.0");
        compare(choices.length, 2);
        compare(choices[1].text, "0000:09:00.0 (not present)");
        compare(Logic.gpuChoices(null, undefined).length, 1);
    }

    function test_updatesIntervalChoices() {
        const choices = Logic.updatesIntervalChoices();
        compare(choices.map(c => c.value), [0, 15, 30, 60, 180, 360, 720, 1440]);
        compare(choices[0].text, "Off");
        compare(choices[3].text, "Every hour");
        compare(choices[4].text, "Every 3 hours");
        compare(choices[7].text, "Once a day");
        compare(Logic.indexOfValue(choices, 60), 3);
        compare(Logic.indexOfValue(choices, 7), -1);
    }

    function test_f5MetricRows() {
        // Built-in F5 metrics need no special code: a GPU lists its fields,
        // an informational text has no bar, a count no fraction.
        const gpu = metric({ id: "gpu:0000:00:02.0", title: "Intel Iris Xe", formattedValue: "37.5 %", normalized: 0.375,
                             items: [{ id: "utilization", title: "Activity", formattedValue: "37.5 %", detail: "", normalized: 0.375 },
                                     { id: "memory", title: "Memory", formattedValue: "N/A", detail: "shared system memory", normalized: null },
                                     { id: "temperature", title: "Temperature", formattedValue: "N/A", detail: "no sensor", normalized: null }] });
        const row = Logic.metricRow(gpu);
        compare(JSON.parse(row.itemsJson).length, 3);
        compare(JSON.parse(row.itemsJson)[1].fraction, null);
        const kernel = Logic.metricRow(metric({ id: "kernel", title: "Kernel", value: null, unit: null,
                                                formattedValue: "Linux 7.2.10", normalized: null, compactValue: null }));
        compare(kernel.value, "Linux 7.2.10");
        verify(!kernel.hasFraction);
        const updates = Logic.metricRow(metric({ id: "updates.flatpak", state: "unavailable", value: null,
                                                 message: "Flatpak is not installed", normalized: null }));
        compare(updates.value, "—");
        compare(updates.detail, "Flatpak is not installed");
    }
}
