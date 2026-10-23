# AQTRunCat — Requirements and v2 Roadmap

This document is the roadmap for AQTRunCat 2.x. It defines what is planned;
nothing below the 2.0.0 baseline is implemented until its section says so.

- **2.0.0** is the consolidation baseline: the complete 1.x application
  (M0–M9, then F1–F7) released as one version, without new runtime
  functionality.
- **v2F1 – v2F3** are planned features.

The behaviour shipped in 2.0.0 is documented in [README.md](README.md) and in
the topic documents indexed by [docs/README.md](docs/README.md); the
normative summaries of the 1.x features are in
[docs/feature-reference.md](docs/feature-reference.md).

    2.0.0  Consolidation baseline (1.x complete)            COMPLETE
    v2F1   Custom Metrics v3                    2.1.x       PLANNED
    v2F2   Independent Autostart                2.2.0       PLANNED
    v2F3   Standalone Settings                  2.3.0       PLANNED


## Versioning

The AQTRunCat version is `project(VERSION)` in `CMakeLists.txt`; the package
recipes (PKGBUILD, RPM spec, Debian changelog), the man page and the
Plasmoid's `metadata.json` carry the same value (checked by the
`packaging-versions` test). Every executable prints it with `-v` /
`--version`, and the tray menus show it.

- 1.0.0 was the M0–M9 roadmap; F1–F7 raised the minor version and their
  sub-features the patch (last 1.x: 1.7.4).
- 2.0.0 consolidates 1.7.4.
- Each v2 feature raises the **minor** version (v2F1 → 2.1.x, v2F2 → 2.2.x,
  v2F3 → 2.3.x); each completed sub-feature raises the **patch** within it.
- The application version is independent of the D-Bus API version
  (`io.github.psykedady.AQTRunCat1`, api 1), of the API payload `schema`
  and of configuration formats: those change only when their contract
  changes (additive changes raise `schema`).

Development model: feature work happens on feature branches named after the
feature; the user performs every Git write operation (branches, commits,
merges, tags).


## 2.0.0 — Consolidation Baseline

Status: COMPLETE.

2.0.0 contains everything implemented in 1.7.4 and nothing more:

- headless backend `aqtruncatd`, D-Bus API (api 1, schema 2) and
  `aqtruncatctl`;
- `aqtruncat` command line, Qt system tray frontend with Dashboard,
  Settings and Metrics Bar, hover information;
- native KDE Plasma 6 widget;
- built-in system metrics (CPU, memory, swap, network, storage, disk I/O,
  uptime, GPU, FPS, package and Flatpak updates, kernel, distribution);
- Custom Metrics (RunCat Neo-compatible snapshots and command sources) and
  the Claude Code and Codex integrations;
- five built-in runners: Cat, Coffee, Walking Goose, Party Parrot, Engine;
- installer (system-wide and `--local`) and native package recipes.

Settings, user runners and Custom Metrics configured with 1.x keep working
unchanged.


## v2F1 — Custom Metrics v3

Status: PLANNED. Version family **2.1.x**.

Extends Custom Metrics v2 (command and snapshot sources,
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md)) without
breaking existing configurations: every new field is optional and its
absence keeps the 2.0.0 behaviour.

### 2.1.0 — v2F1 Baseline

Documentation and roadmap baseline for v2F1. No new functionality compared
with 2.0.0 apart from version metadata and documentation.

### 2.1.1 — Optional `enabled` Flag

- An optional boolean `enabled` in a Custom Metric's JSON. Missing means
  `true` (backward compatible).
- `enabled: false` disables execution (no command is run, no schedule) and
  display (Dashboard, Metrics Bar, Plasmoid).
- A checkbox in the GUI enables or disables each Custom Metric. The GUI may
  add the `enabled` field to the existing JSON; it preserves every
  unrelated field and writes atomically.
- Changes apply without restarting the application.

### 2.1.2 — Execution Warnings

- An explicit warning when a Custom Metric's command fails.
- The existing distinctions stay visible: execution failure, timeout,
  intentionally unavailable data (exit 69 / unavailable output) and valid
  values.
- Reuses the existing command process and status infrastructure.

### 2.1.3 — Optional Progress Bar

- An optional display setting, e.g. `display.showBar` (boolean).
- Text-only or numeric metrics can be shown without a bar.
- Absent: the current behaviour (a bar when a normalized value exists).
- Applied consistently on every surface that draws bars (Qt Dashboard,
  Plasmoid popup).

### 2.1.4 — Optional Metric Groups and JSON Inheritance

Groups of Custom Metrics using the existing `metric.json` format. A parent
`metric.json` declares `source.type: "group"`; its children live in
subdirectories, each with its own `metric.json`.

```text
custom-metrics/
└── dice-metrics/
    ├── metric.json
    ├── dice.svg
    ├── random-metric.sh
    ├── d100/
    │   └── metric.json
    ├── d20/
    │   └── metric.json
    └── d6/
        └── metric.json
```

Parent:

```json
{
  "id": "dice-metrics",
  "title": "Dice Metrics",
  "symbol": {
    "type": "file",
    "value": "./dice.svg"
  },
  "source": {
    "type": "group",
    "executable": "./random-metric.sh",
    "arguments": [],
    "intervalSeconds": 5,
    "timeoutSeconds": 3
  }
}
```

Child:

```json
{
  "title": "D20",
  "source": {
    "type": "command",
    "arguments": [20]
  },
  "display": {
    "suffix": "d20",
    "normalizedRange": {
      "minimum": 1,
      "maximum": 20
    },
    "showBar": false
  }
}
```

Requirements:

- Groups are optional; standalone metrics keep working unchanged.
- Children inherit the parent's properties unless they override them;
  nested JSON objects are merged recursively.
- A group is a container, not an executable metric.
- Each executable child has its own schedule, values, errors and enabled
  state.
- Child ids are stable and unique, preferably scoped by their group.
- A disabled group disables all its descendants, even a child that declares
  `enabled: true`.
- Group presentation is compact and responsive: grouped metrics can be
  shown inline and wrap to the available width.
- Nested groups may be supported, or at least not precluded by the
  architecture, without unnecessary complexity in the first implementation.

Relative path resolution:

- Relative paths resolve against the directory of the `metric.json` that
  declared them; inherited paths keep that context, paths overridden by a
  child resolve against the child's `metric.json`.
- Never relative to the process working directory.
- Legitimate `../` references are allowed, within the existing execution
  security rules (direct executable, no shell, no elevation).

Open point for the implementation: 2.0.0 accepts only strings in
`source.arguments` (the child example above uses the number `20`); either
accept numbers (converted to their decimal text) or document strings.

### 2.1.5 — Reload Custom Metrics

- Reload the Custom Metric configurations without restarting.
- Rescan the Custom Metrics directory recursively: new metrics and groups
  are discovered, deleted ones removed, modified ones reloaded.
- Backend scheduling and frontend presentation are reconciled: no stale
  entries, no duplicate execution.
- Reloading the configuration is distinct from refreshing values.

### 2.1.6 — Manual Refresh by Clicking Metric Icons

- Clicking a metric's icon requests a fresh sample immediately.
- A child's icon refreshes only that child; a group's icon refreshes all
  its enabled executable descendants. Disabled metrics never run.
- No overlapping executions; the existing process limits apply.
- The request goes through the shared backend; every connected frontend
  receives the result.
- The periodic schedule continues as before.


## v2F2 — Independent Autostart

Status: PLANNED. Target version **2.2.0**.

The current single "Start AQTRunCat at login" setting is ambiguous since
the backend and the tray became separate programs (it starts the tray, which
starts the backend). Provide independent autostart controls for:

1. the backend (`aqtruncatd`);
2. the Qt system tray frontend (`aqtruncat`).

Requirements:

- Each component is clearly identified in Settings.
- Either can start at login without the other.
- D-Bus activation of the backend keeps working; explicit login autostart
  is distinguished from on-demand activation.
- No duplicate instances.
- Migration from the previous single setting.
- System-wide and `--local` installations and the existing desktop
  integration are respected.


## v2F3 — Standalone Settings

Status: PLANNED. Target version **2.3.0**.

The Settings window can be opened without the tray frontend or the Plasma
widget.

Requirements:

- A standalone launch mechanism (a dedicated executable or a command-line
  option).
- The tray runner need not be running; no tray icon is created only to show
  Settings; no Plasma widget is needed.
- Reuses the existing Settings implementation and the shared configuration;
  no duplicate backend process or configuration logic.
- Settings stays reachable from the tray and the Plasma widget as today.
- Changes reach the running components as they do now.
- The entry point is integrated with installation and packaging where
  appropriate (e.g. a desktop entry).


## General Principles

Development must preserve the principles established during M0–M9 and
F1–F7:

1. The generic Qt/Linux application remains the portable cross-desktop
   baseline.

2. Plasma-specific functionality is an additive integration layer, not a
   replacement for the generic application.

3. Frontends should reuse core models, settings, metrics and application logic
   rather than duplicating business logic.

4. New metrics should reuse the existing metric and state architecture where
   appropriate.

5. Avoid duplicated samplers. Acquisition schedules must match the source and
   should not be coupled unnecessarily to CPU sampling or UI refresh.

6. Optional functionality must fail gracefully and must not bring down the
   application.

7. Unsupported hardware, desktops, tools or data sources must be represented
   as unavailable/unsupported rather than as zero values or fatal errors.

8. Avoid unnecessary polling and expensive background work.

9. Prefer direct/native Linux interfaces over external commands where
   practical. F1 command sources and small F2/F3 adapters are deliberate
   extension points, not a reason to replace suitable native interfaces.

10. Installation must continue to support both system-wide and `--local`
    workflows.

11. User data and configuration must remain separate from installed
    application files and must be preserved during install, update and
    uninstall.

12. Compatibility with the generic Qt/SNI application must not be sacrificed
    solely to improve a desktop-specific frontend.

13. Existing M0–M9 and F1–F7 behaviour and compatibility contracts remain
    in force unless a feature explicitly and deliberately extends them, as
    F1 did for the M6 Custom Metrics acquisition model.

14. The v2 features in this document are planned work. Their presence here
    does not imply that they are implemented or supported before their
    section says COMPLETE.
