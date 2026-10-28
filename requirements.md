# AQTRunCat — Requirements and v2 Roadmap

This document is the roadmap for AQTRunCat 2.x. It defines what is planned;
nothing below the 2.0.0 baseline is implemented until its section says so.

- **2.0.0** is the consolidation baseline: the complete 1.x application
  (M0–M9, then F1–F7) released as one version, without new runtime
  functionality.
- **v2F1** (Custom Metrics v3), **v2F2** (Standalone Settings), and v2F3
  checkpoints 2.3.0–2.3.2 are complete. Current release: 2.3.2; next
  implementation checkpoint: 2.3.3 Dependency Advisor.

The behaviour shipped in 2.0.0 is documented in [README.md](README.md) and in
the topic documents indexed by [docs/README.md](docs/README.md); the
normative summaries of the 1.x features are in
[docs/feature-reference.md](docs/feature-reference.md).

    2.0.0  Consolidation baseline (1.x complete)            COMPLETE
    v2F1   Custom Metrics v3 + Rickroll runner  2.1.x       COMPLETE (2.1.7)
    v2F2   Standalone Settings                   2.2.0       COMPLETE
    v2F3   Dependency Advisor & Hardware Monitoring 2.3.x    CURRENT (2.3.2; next 2.3.3)

2.1.8 recursive Custom Metrics discovery, allowing complete repositories to
be cloned inside the metrics directory, was completed after v2F1.


## Versioning

The AQTRunCat version is `project(VERSION)` in `CMakeLists.txt`; the package
recipes (PKGBUILD, RPM spec, Debian changelog), the man page and the
Plasmoid's `metadata.json` carry the same value (checked by the
`packaging-versions` test). Every executable prints it with `-v` /
`--version`, and the tray menus show it.

- 1.0.0 was the M0–M9 roadmap; F1–F7 raised the minor version and their
  sub-features the patch (last 1.x: 1.7.4).
- 2.0.0 consolidates 1.7.4.
- v2F1 used 2.1.x; v2F2 is released directly as 2.2.0; v2F3 uses 2.3.x.
  Version 2.3.0 is documentation/version initialization only. New v2 features
  do not require a new runner.
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


## v2F1 Runner Record

| Feature | Version | Runner | Proposed id |
|---------|---------|--------|-------------|
| v2F1 | 2.1.1 | Rickroll | `rickroll` |
| v2F2 | — | None | — |
| v2F3 | — | None | — |

Rickroll is the only runner introduced by v2F1. Its art and provenance
requirements remain recorded in [docs/built-in-runners.md](docs/built-in-runners.md).

The completed v2F1 runner followed these requirements:

- It is a **built-in** runner (bundled like the F7 runners, see
  [docs/built-in-runners.md](docs/built-in-runners.md)), not a Custom
  Runner.
- It uses the existing animation architecture (runner model, loader,
  animator, CPU-driven speed); no runner-specific animation code.
- It works in both the Qt system tray and the native KDE Plasma 6 Plasmoid.
- It is selectable through the existing runner selection mechanism
  (Settings, `aqtruncat` CLI, Plasmoid configuration) and exposed through
  the existing backend and D-Bus runner discovery (`GetRunners`).
- The selection persists through the existing settings.
- Asset installation (bundled runner list, `data/runners/<id>/`,
  `third_party/<id>/` where applicable), packaging, automated tests,
  documentation and attribution (`THIRD_PARTY_NOTICES.md`, provenance and
  licence, or the user's explicit decision when the licence cannot be
  verified) are all covered.
- Existing runners, their ids and the default runner remain unaffected.
- The artwork is supplied or approved by the user **before**
  implementation. Replacement artwork is never downloaded or generated
  without explicit approval.
- The runner id follows the existing naming convention (lowercase, no
  separators, e.g. `walkinggoose`, `partyparrot`).


## v2F1 — Custom Metrics v3 + Rickroll

Status: COMPLETE (2026-10-27). Version family **2.1.x**; current version
**2.1.8** (2.1.0–2.1.8 COMPLETE).

Extends Custom Metrics v2 (command and snapshot sources,
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md)) without
breaking existing configurations: every new field is optional and its
absence keeps the 2.0.0 behaviour. Opens with the Rickroll runner.

    2.1.0  Feature baseline (requirements, version metadata)
    2.1.1  Rickroll built-in runner
    2.1.2  Optional `enabled` flag
    2.1.3  Execution warnings
    2.1.4  Optional progress bar (`display.showBar`)
    2.1.5  Optional metric groups and JSON inheritance
    2.1.6  Reload / rescan Custom Metrics
    2.1.7  Manual refresh by clicking metric icons
    2.1.8  Recursive discovery through organisational directories

### 2.1.0 — v2F1 Baseline

Status: COMPLETE (2026-10-23). Documentation and roadmap baseline for v2F1.
No new functionality compared with 2.0.0 apart from version metadata and
documentation.

The baseline was documented in the roadmap (this section) and the version
went from 2.0.0 directly to 2.1.1 together with the Rickroll runner: no
separate 2.1.0 build or package was produced.

### 2.1.1 — Rickroll Runner

Status: COMPLETE (2026-10-23). Record:
[docs/built-in-runners.md](docs/built-in-runners.md) §10.

- The official built-in runner **Rickroll**, id `rickroll`,
  following every requirement of the
  [v2 Runner Convention](#v2-runner-convention).
- Frames are supplied or approved by the user before implementation; their
  provenance and licence (or the user's explicit decision, as for Walking
  Goose and Party Parrot) are recorded in `THIRD_PARTY_NOTICES.md`.
- No change to Custom Metrics in this step.

As implemented: 31 frames, 33×36, monochrome stencil of Rick Astley in
"Never Gonna Give You Up" (the Rickroll meme), in
`third_party/rickroll/runners/rickroll/` with a reproducible
`make-frames.py`; registered in `AQTRUNCAT_BUNDLED_RUNNERS` (six built-in
runners), metadata in `data/runners/rickroll/`; Cat stays the default.

### 2.1.2 — Optional `enabled` Flag

Status: COMPLETE (2026-10-23). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §8.

- An optional boolean `enabled` in a Custom Metric's JSON. Missing means
  `true` (backward compatible).
- `enabled: false` disables execution (no command is run, no schedule) and
  display (Dashboard, Metrics Bar, Plasmoid).
- A checkbox in the GUI enables or disables each Custom Metric. The GUI may
  add the `enabled` field to the existing JSON; it preserves every
  unrelated field and writes atomically.
- Changes apply without restarting the application.

As implemented: `enabled` is read from snapshot files and command
configurations (a non-boolean is a validation error). A disabled source
stays listed by the backend without values (Unavailable, "Disabled.") and
never runs; disabling stops an in-flight command. The checkbox (tray
Settings and Plasmoid configuration) goes through the backend: D-Bus
`SetCustomMetricEnabled` writes only the flag (other bytes unchanged,
atomic replace) and applies it at once; payloads carry `enabled` (API
schema 3, additive). Dashboard, Metrics Bar and Plasmoid popup leave
disabled sources out; a Metrics Bar selection is kept. Integrations'
sources are switched in Integrations, not here. A producer that rewrites a
snapshot file without the flag re-enables it.

### 2.1.3 — Execution Warnings

Status: COMPLETE (2026-10-23). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §9.

- An explicit warning when a Custom Metric's command fails.
- The existing distinctions stay visible: execution failure, timeout,
  intentionally unavailable data (exit 69 / unavailable output) and valid
  values.
- Reuses the existing command process and status infrastructure.

As implemented: the existing states (Pending, Available, Unavailable,
Error + message) carry everything; no new state or API field. Exit 69 stays
Unavailable; any other non-zero exit, a missing or non-executable program
(previously Unavailable: deliberate change), a launch failure, a timeout or
invalid output is Error with a concise reason (exit status, first stderr
line bounded to 200 characters, timeout, validator error). A success clears
the warning; errors never stop the schedule. Dashboard: error icon, "—"
instead of the last values (layout kept), "Error: <reason>"; Metrics Bar:
"!" and the reason in the tooltip, no stale value; Plasmoid: dialog-error
icon and the reason (unchanged, now tested); Settings: error icon on the
status. The backend logs one line per new failure and one per recovery
(`aqtruncat.custommetrics`).

### 2.1.4 — Optional Progress Bar

Status: COMPLETE (2026-10-23). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §10.

- An optional display setting, e.g. `display.showBar` (boolean).
- Text-only or numeric metrics can be shown without a bar.
- Absent: the current behaviour (a bar when a normalized value exists).
- Applied consistently on every surface that draws bars (Qt Dashboard,
  Plasmoid popup).

As implemented: `display.showBar` (boolean; any other type is a validation
error; a non-object `display` too) in command configurations — with
snapshot output `display` may now hold `showBar` alone — and in snapshot
files (other `display` keys ignored there, as unknown keys are). Only
`false` changes anything: no bar in the Dashboard (none created, the card
is shorter) and in the Plasmoid popup; values, normalized data, icons,
titles, statuses and warnings are unchanged; `true` never invents a bar.
The Metrics Bar draws no bars (unchanged). Effective value in the payload
as `showBar` (API schema 4, additive). No Settings editor (Settings has none
for display properties): set it in the file.

### 2.1.5 — Optional Metric Groups and JSON Inheritance

Status: COMPLETE (2026-10-23). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §11.

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
Resolved in 2.1.5: numbers are accepted and passed as their decimal text.

As implemented: groups are discovered from package `metric.json` files (and
explicitly registered `metric.json` files), children are the package's
subdirectories with a `metric.json` (natural order, at most 256, nesting
bounded at 4 levels, symlink cycles skipped, unrelated subdirectories
ignored). Effective child configuration = recursive merge (objects merged,
arrays and scalars replaced; `id` and `enabled` never inherited), validated
by the existing command parser; `source.executable` and a file `symbol` are
resolved against the directory of the file that declared them. Child ids:
explicit `id` or the directory name, hierarchical (`dice-metrics/d20`),
duplicates in a group are errors; the source identity (Metrics Bar
selection) stays the child file's canonical path. Effective enabled =
ancestors' and own; Settings and the Plasmoid configuration write the
group's or the child's own flag. Each child is an ordinary command job.
API schema 5 (additive): kind `group`, `data.groupId`,
`data.configuredEnabled`. Dashboard: one card per group, children as
wrapping cells (`FlowLayout`), small bars only where `showBar` allows;
Plasmoid: the same as a `Flow`; Metrics Bar: children are ordinary items
("Group — Child", captioned with the child's title), groups are not items;
groups without enabled children are hidden; Settings: a tree.

### 2.1.6 — Reload Custom Metrics

Status: COMPLETE (2026-10-24). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §12.
Entry points: Settings and Plasma widget "Reload Custom Metrics",
`aqtruncatctl custom reload`, D-Bus `ReloadCustomMetrics` (schema 6).
A reload that cannot read a directory is not applied; per-file errors keep
the established policy (the source stays, in Error, listed in the report).

- Reload the Custom Metric configurations without restarting.
- Rescan the Custom Metrics directory recursively: new metrics and groups
  are discovered, deleted ones removed, modified ones reloaded.
- Backend scheduling and frontend presentation are reconciled: no stale
  entries, no duplicate execution.
- Reloading the configuration is distinct from refreshing values.

### 2.1.7 — Manual Refresh by Clicking Metric Icons

Status: COMPLETE (2026-10-24). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §13.
Entry points: the metric's (or group's, or child's own) icon in the
Dashboard and in the Plasma widget's popup; a Metrics Bar item's middle
click or its menu's "Refresh Now" (its left click keeps opening the
Dashboard); `aqtruncatctl custom refresh`; D-Bus `RefreshCustomMetric`
(schema 7). Concurrency policy: a request for a running metric, or for one
whose last run ended less than one second ago, is coalesced with it (never
queued, never a second process). The refreshed metric's next periodic run
is one interval after the manual run.

- Clicking a metric's icon requests a fresh sample immediately.
- A child's icon refreshes only that child; a group's icon refreshes all
  its enabled executable descendants. Disabled metrics never run.
- No overlapping executions; the existing process limits apply.
- The request goes through the shared backend; every connected frontend
  receives the result.
- The periodic schedule continues as before.

### 2.1.8 — Recursive Custom Metric Discovery

Status: COMPLETE (2026-10-27). Record:
[docs/custom-metrics-commands.md](docs/custom-metrics-commands.md) §14.

- Discovery recursively traverses directories that do not contain a
  `metric.json`, allowing collections such as
  `metrics/ExampleMetrics/<package>/metric.json`.
- A directory containing `metric.json` is a package boundary and is not
  traversed by general discovery. Its subdirectories are children only when
  that configuration explicitly declares `source.type: "group"`.
- Traversed container directories are watched, so adding a package is noticed
  without restarting AQTRunCat. Symlink aliases and loops remain deduplicated
  by canonical directory identity.


## v2F2 — Standalone Settings

Status: COMPLETE (2.2.0).

The existing Settings window launches independently with `aqtruncat settings`,
from the AQTRunCat Settings application-menu entry, from the Qt tray menu, or
from the native Plasma widget popup. It reuses the existing SettingsWindow,
AppSettings and backend API. A dedicated session-bus name coordinates a
single process per user session and recovers automatically when it exits.
Closing standalone Settings exits that frontend only; the backend keeps its
existing on-demand activation and session lifetime. The login preference is
still the existing Qt Tray Plugin autostart setting, with its stored key and
value unchanged. Plasma continues to manage the widget's lifecycle and
configuration separately.


## v2F3 — Dependency Advisor & Hardware Monitoring

Status: IN PROGRESS. Checkpoints 2.3.0–2.3.2 are complete. Version family
**2.3.x**; next is 2.3.3.

    2.3.0  Phase Initialization
    2.3.1  Arch Updates via checkupdates
    2.3.2  CPU Temperature Sensors                         COMPLETE
    2.3.3  Dependency Advisor                              NEXT

### 2.3.0 — Phase Initialization

Status: COMPLETE.

- Initialize version 2.3.0, update roadmap and documentation, establish the
  v2F3 development baseline, and preserve existing behavior.
- No functional changes were introduced in 2.3.0.

### 2.3.1 — Arch Updates via `checkupdates`

Status: COMPLETE (2.3.1).

- `checkupdates` from `pacman-contrib` is the sole Arch-family source for
  official pacman repository updates. Default checks use `--nosync`; the
  explicit remote-refresh setting syncs only checkupdates' private database.
- Never use `pacman -Sy`, `pacman -Qu` fallback, root, or package install.
  Exit 2 with empty stdout means zero; exit 0 requires valid update records;
  unexpected status/output, startup failure and timeout are errors.
- Missing `checkupdates` is Unavailable with a diagnostic and the suggested
  install command `sudo pacman -S pacman-contrib`; it is never run
  automatically. AUR updates are outside scope.
- Existing manager detection supports explicit IDs `arch`, `archarm`,
  `manjaro`, `endeavouros`, `cachyos`, and `ID_LIKE=arch` when the existing
  capability checks find pacman and its local database. Non-Arch providers,
  metric/API contract, schedule and backend ownership are unchanged.

The polling interval, first-check delay, timeout, output bounds and
non-overlap continue to be owned by `UpdatesService` in the backend. AUR is
not counted by `checkupdates`.

### 2.3.2 — CPU Temperature Sensors

Status: COMPLETE (2.3.2). Adds the native `cpu.temperature` metric in °C.
The backend discovers explicitly identified CPU channels under
`/sys/class/hwmon`, preferring package/die labels and known CPU drivers; only
well-known CPU thermal-zone types are accepted as fallback. It retains sensor
identity between samples and reports unavailable/error instead of fabricating
a value. Collection is part of the existing one-second backend sample, with
sensor discovery cached for five minutes and immediate rediscovery on failure.
No `sensors` subprocess is used; `lm_sensors` is neither required nor invoked.
See [CPU temperature documentation](docs/cpu-temperature.md) for selection
rules, frontend integration, and hardware limitations.

The metric is available through the existing API schema (additive schema 8),
Dashboard, Metrics Bar, Plasma Plasmoid, and `aqtruncat metrics` output. A
schema-9 presentation follow-up adds global Settings for CPU temperature, GPU
memory/temperature, and FPS visibility; Dashboard, Metrics Bar and Plasmoid
use the same preferences. CPU temperature is grouped with CPU activity in the
Dashboard and Plasmoid. Unavailable (N/D) CPU/GPU/FPS fields are hidden, while
pending and error states remain visible. These are presentation-only settings:
sampling, backend lifecycle, and dependencies are unchanged.

### 2.3.3 — Dependency Advisor

Next implementation checkpoint; do not implement before requested.

Detect required, optional, missing, available and unsupported dependencies.

Initial dependency candidates (verify each against actual code at
implementation time):

| Dependency | Enables | Notes |
|------------|---------|-------|
| Claude Code CLI | Claude Code integration (F2) | optional |
| Codex CLI | Codex integration (F3) | optional |
| MangoHud | FPS metric (F5) | optional |
| `pacman-contrib` (`checkupdates`) | Arch package updates metric (F5, 2.3.1) | optional, Arch only |
| Flatpak | Flatpak updates metric (F5) | optional |
| `lm_sensors` | — | Not used by AQTRunCat 2.3.2; direct kernel hwmon access is used. |
| other distribution-specific utilities | — | only when verified against actual code |

Keep the advisor consistent with package-recipe optional dependencies
(PKGBUILD `optdepends`, Debian/RPM `Suggests` or `Recommends`).

Requirements:

- A dependency is **required** only if the core application cannot work
  without it; dependencies for optional features remain optional.
- Use one shared detection model for installer and CLI rather than duplicate,
  potentially inconsistent rules.
- Checks are read-only, offline-capable, require no root, and never install.
- Distinguish missing tools from unsupported features/hardware.
- Give distribution-specific installation suggestions when known; don't
  recommend packages unrelated to AQTRunCat functionality.

Dependency Advisor requirements:

```bash
aqtruncatctl --missing-deps
```

- Prints a human-readable report of the missing optional dependencies, the
  AQTRunCat functionality each one enables, and installation commands for
  the detected distribution.
- Never installs packages.
- Works without an active backend or Plasma session, offline and without
  elevated privileges.
- Uses the same dependency detection model as the installer.
- Exit code `0` when detection succeeds, even if optional dependencies are
  missing; nonzero only for genuine command failures.
- CLI documentation (help, man page, docs) and automated tests are part of
  the implementation.
- Inspect actual runtime/optional dependencies; explain the AQTRunCat feature
  each enables and provide distribution-aware suggestions.
- Include `checkupdates`/`pacman-contrib` and `lm_sensors` where applicable.
- Support declarative dependencies in Custom Metrics JSON and recursive
  discovery, without executing or arbitrarily parsing metric scripts.
- Distinguish missing dependencies from unsupported hardware and other
  execution failures. Never auto-install or request administrator privileges.
- Integrate in standalone Settings where appropriate; reuse existing
  architecture instead of adding an unnecessary background service.
- Do not add mandatory `pacman-contrib` or `lm_sensors` package dependencies
  as part of v2F3; both remain optional for their respective features.
- Do not implement the Advisor until the 2.3.3 checkpoint.


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
