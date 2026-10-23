# AQTRunCat — Requirements and v2 Roadmap

This document is the roadmap for AQTRunCat 2.x. It defines what is planned;
nothing below the 2.0.0 baseline is implemented until its section says so.

- **2.0.0** is the consolidation baseline: the complete 1.x application
  (M0–M9, then F1–F7) released as one version, without new runtime
  functionality.
- **v2F1 – v2F4** are planned features. Each one begins with a new official
  built-in animated runner ([v2 Runner Convention](#v2-runner-convention)).

The behaviour shipped in 2.0.0 is documented in [README.md](README.md) and in
the topic documents indexed by [docs/README.md](docs/README.md); the
normative summaries of the 1.x features are in
[docs/feature-reference.md](docs/feature-reference.md).

    2.0.0  Consolidation baseline (1.x complete)            COMPLETE
    v2F1   Custom Metrics v3 + Rickroll runner  2.1.x       IN PROGRESS (2.1.2 done)
    v2F2   Independent Autostart + new runner   2.2.x       PLANNED
    v2F3   Standalone Settings + new runner     2.3.x       PLANNED
    v2F4   Dependency Advisor + new runner      2.4.x       PLANNED


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
  v2F3 → 2.3.x, v2F4 → 2.4.x); each completed sub-feature raises the
  **patch** within it.
- `2.x.0` is always the feature baseline (requirements, documentation and
  version metadata only); `2.x.1` is always the feature's new official
  built-in runner; the functional sub-features follow from `2.x.2`.
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


## v2 Runner Convention

Every major v2 feature (v2F1, v2F2, v2F3, v2F4) begins with the addition of
a new official built-in animated runner, delivered as the first
implementation milestone after the feature baseline (`2.x.1`).

| Feature | Version | Runner | Proposed id |
|---------|---------|--------|-------------|
| v2F1 | 2.1.1 | Rickroll | `rickroll` |
| v2F2 | 2.2.1 | TBD | TBD |
| v2F3 | 2.3.1 | TBD | TBD |
| v2F4 | 2.4.1 | TBD | TBD |

Names and artwork of the TBD runners are decided by the user; they are not
to be invented.

Requirements for every v2 runner:

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

Status: IN PROGRESS. Version family **2.1.x**; current version **2.1.2**
(2.1.0, 2.1.1 and 2.1.2 COMPLETE, 2.1.3–2.1.7 PLANNED).

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

- An explicit warning when a Custom Metric's command fails.
- The existing distinctions stay visible: execution failure, timeout,
  intentionally unavailable data (exit 69 / unavailable output) and valid
  values.
- Reuses the existing command process and status infrastructure.

### 2.1.4 — Optional Progress Bar

- An optional display setting, e.g. `display.showBar` (boolean).
- Text-only or numeric metrics can be shown without a bar.
- Absent: the current behaviour (a bar when a normalized value exists).
- Applied consistently on every surface that draws bars (Qt Dashboard,
  Plasmoid popup).

### 2.1.5 — Optional Metric Groups and JSON Inheritance

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

### 2.1.6 — Reload Custom Metrics

- Reload the Custom Metric configurations without restarting.
- Rescan the Custom Metrics directory recursively: new metrics and groups
  are discovered, deleted ones removed, modified ones reloaded.
- Backend scheduling and frontend presentation are reconciled: no stale
  entries, no duplicate execution.
- Reloading the configuration is distinct from refreshing values.

### 2.1.7 — Manual Refresh by Clicking Metric Icons

- Clicking a metric's icon requests a fresh sample immediately.
- A child's icon refreshes only that child; a group's icon refreshes all
  its enabled executable descendants. Disabled metrics never run.
- No overlapping executions; the existing process limits apply.
- The request goes through the shared backend; every connected frontend
  receives the result.
- The periodic schedule continues as before.


## v2F2 — Independent Autostart + New Runner

Status: PLANNED. Version family **2.2.x**.

    2.2.0  Feature baseline (requirements, version metadata)
    2.2.1  New official built-in runner (TBD)
    2.2.2  Independent backend and Qt tray autostart settings

### 2.2.1 — New Runner (TBD)

Name, id and artwork to be decided by the user; follows the
[v2 Runner Convention](#v2-runner-convention).

### 2.2.2 — Independent Autostart

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


## v2F3 — Standalone Settings + New Runner

Status: PLANNED. Version family **2.3.x**.

    2.3.0  Feature baseline (requirements, version metadata)
    2.3.1  New official built-in runner (TBD)
    2.3.2  Standalone Settings entry point

### 2.3.1 — New Runner (TBD)

Name, id and artwork to be decided by the user; follows the
[v2 Runner Convention](#v2-runner-convention).

### 2.3.2 — Standalone Settings

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


## v2F4 — Dependency Advisor + New Runner

Status: PLANNED. Version family **2.4.x**.

    2.4.0  Feature baseline (requirements, version metadata)
    2.4.1  New official built-in runner (TBD)
    2.4.2  Dependency detection model
    2.4.3  Distribution-aware recommendations in the installer
    2.4.4  `checkupdates` as the preferred Arch Linux update provider
    2.4.5  `aqtruncatctl --missing-deps`

The advisor reports the external tools AQTRunCat's **actual** functionality
uses, which of them are present, and how to install the missing ones. It
never installs anything.

### 2.4.1 — New Runner (TBD)

Name, id and artwork to be decided by the user; follows the
[v2 Runner Convention](#v2-runner-convention).

### 2.4.2 — Dependency Detection Model

Detect required, optional, missing, available and unsupported
dependencies.

Initial candidates (each to be verified against the code at implementation
time):

| Dependency | Enables | Notes |
|------------|---------|-------|
| Claude Code CLI | Claude Code integration (F2) | optional |
| Codex CLI | Codex integration (F3) | optional |
| MangoHud | FPS metric (F5) | optional |
| `pacman-contrib` (`checkupdates`) | Arch package updates metric (F5, 2.4.4) | optional, Arch only |
| Flatpak | Flatpak updates metric (F5) | optional |
| `lm_sensors` | hardware sensors | only if actually used; not used by 2.0.0 |
| other distribution-specific utilities | — | only when verified against existing code |

The package recipes already list optional dependencies (PKGBUILD
`optdepends`, Debian/RPM `Suggests`/`Recommends`: e.g. `qt6-wayland`,
`plasma-workspace`, `hwdata`, `flatpak`, `pacman-contrib`, `mangohud`); the
advisor's list and these recipes must stay consistent.

Requirements:

- A dependency is **required** only if the core application cannot work
  without it; being useful for an optional feature makes it optional.
- One shared detection model is used by the installer and the CLI: no
  separate, inconsistent lists or duplicated detection rules.
- Checks are read-only, work offline, need no root privileges and never
  install anything.
- Missing (the tool could be installed) is distinguished from unsupported
  (the functionality does not apply to this system, e.g. `checkupdates` on
  a non-Arch distribution).
- Installation suggestions are distribution-specific when known; packages
  unrelated to supported features are never recommended.

### 2.4.3 — Installer Recommendations

- During installation, `install.sh` shows the relevant optional
  dependencies and distribution-aware recommendations, using the shared
  detection model.
- It never runs package installation commands automatically.
- Preserved: system-wide and `--local` installation, the supported
  distributions, dry-run behaviour, the existing installer options and the
  cached-build installation (`--no-build`).

### 2.4.4 — Arch Linux Updates via `checkupdates`

`checkupdates` (from `pacman-contrib`) is the preferred Arch Linux package
update provider.

- The existing remote-refresh setting is respected: no network access is
  introduced silently when remote refresh is disabled.
- Never run `pacman -Sy` on the live package database; never require
  `sudo` or `pkexec` for update checks.
- Zero available updates is distinguished from a missing dependency, a
  network error, a timeout and an execution failure.
- The existing package update metric semantics stay compatible.
- Execution is bounded, and checks never overlap.

Note for the implementation: 2.0.0 already runs `checkupdates` when remote
refresh is enabled and it is installed, and `pacman -Qu` on the local
databases otherwise (`src/metrics/packageupdates.cpp`). Because
`checkupdates` normally syncs a temporary database copy (network access),
2.4.4 must decide, without breaking the rule above, what "preferred" means
with remote refresh disabled (keep `pacman -Qu`, or `checkupdates
--nosync` if it reads only already-synced data).

### 2.4.5 — `aqtruncatctl --missing-deps`

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
