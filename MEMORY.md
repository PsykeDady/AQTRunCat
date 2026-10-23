# MEMORY.md — AQTRunCat maintainer notes (2.1.4)

> Audience: an AI agent or developer maintaining AQTRunCat without access to
> earlier conversations. Precedence when sources disagree: **actual
> code/tests > this file (frozen decisions §5) > docs/ > commit messages**.
>
> **State (2026-10-23): AQTRunCat 2.1.4** on branch `feature/v2/f3`
> (v2F1: 2.1.1 = 385ceb1, 2.1.2 = 0a72597, 2.1.3 = 613bdbe; 2.1.4
> `display.showBar` uncommitted — the user commits). 2.0.0 (commit 61f3a8e) was the consolidation release
> of the complete 1.x application (M0–M9 → 1.0.0, then F1–F7 → 1.7.4),
> without functional change from 1.7.4. All 1.x features are
> complete (summary §2, history §4 and §13, normative summaries in
> `docs/feature-reference.md`, topic documents indexed by `docs/README.md`).
> `requirements.md` is the **v2 roadmap**: v2F1 Custom Metrics v3 + Rickroll
> (2.1.x), v2F2 Independent Autostart (2.2.x), v2F3 Standalone Settings
> (2.3.x), v2F4 Dependency Advisor (2.4.x) (summary at the end of §2).
> Done: 2.1.0–2.1.4. Next: 2.1.5 (metric groups and JSON inheritance) —
> only on the user's explicit instruction, one sub-feature at a time.
>
> The user owns every Git write (branches, commits, merges, tags); read-only
> Git only. Reports to the user in Italian; code, comments and
> requirements in English; `docs/*.md` topic records in Italian.
>
> Operational lessons (still valid):
> - Private-bus experiments: always `dbus-run-session
>   --config-file=build/tests/backendipc-bus.conf` (no service dirs). The
>   default config activates the machine's portals, gvfs, ksecretd, a11y.
>   Offscreen runs: unset WAYLAND_DISPLAY/DISPLAY. The offscreen QPA has no
>   SNI tray: to validate the tray, use the real Wayland platform (no window
>   is mapped) with a fake StatusNotifierWatcher on the private bus, and pass
>   WAYLAND_DISPLAY as an absolute path when XDG_RUNTIME_DIR is isolated
>   (otherwise Qt aborts without a platform plugin).
> - The user has an installed AQTRunCat in `/usr/local` (its `aqtruncatd`
>   runs on the real session bus): never touch it; tests must not depend
>   on it.
> - Never restart the user's plasmashell (it takes about a minute here);
>   suggest `install.sh --restart-plasma` or the systemctl command. A
>   running plasmashell keeps the widget QML it loaded first.
> - A Bash `-c` command containing `rm -rf "$VAR"` is blocked by the
>   harness: use fresh scratch directories. Bash `printf %.3f` follows the
>   Italian locale (comma): use `LC_ALL=C` in generated fixtures.
> - The shell sandbox cannot reach the user's AT-SPI bus.
>
> Last verified (2.0.0, 2026-10-22): see §9 (clean build, ctest, checks).

---

## 0. Working rules (user-imposed, apply to every task)

- **Git: no commit/push/branch/merge/tag/history rewrite** — the user does
  every Git write. Remote `origin git@psdadygithub:PsykeDady/AQTRunCat.git`.
  Read-only `git status`/`git log`/`git diff --check` are fine. No git
  config changes.
- Work **only on the requested feature/sub-feature**; stop at its end and
  report. Do not implement later roadmap items because they "look easy".
- `requirements.md` is the user's **v2 roadmap**: update a feature's status
  and its record when it is implemented; otherwise change it only when
  asked. Normative 1.x decisions live in §5 here and in
  `docs/feature-reference.md`; `docs/*.md` are historical records: do not
  rewrite their observed results, add notes instead.
- **Stop conditions**: if a choice with significant user-visible semantics has
  several equally reasonable options, stop and report *Option A / Option B /
  trade-offs / recommendation* instead of picking arbitrarily. If it is
  clearly standard Linux practice, implement and document it. If a new
  significant dependency is needed, stop and ask first.
- Branches (observed 2026-10-22): the 1.x work was done on `feature/f1` …
  `feature/f7` (pushed to origin) and consolidated on `feature/main` (2.0.0);
  2.0.0 was then committed on `main` (61f3a8e) and v2 work happens on
  `feature/v2/main` (observed 2026-10-23, 2.1.1);
  local `main` is still at an old commit (27e6d2d) and `feature/base`, once
  planned as the common baseline, never existed. Ask the user which branch
  a v2 feature starts from; never create or switch branches yourself.
- No shelling out for routine work (no `top`, `free`, `df`, `iostat`, `unzip`,
  `bsdtar` …). External tools are allowed **only for manual validation**.
- Tools/scripts: precise PID handling, cleanup on Ctrl+C (verify with a
  pseudo-terminal), never `pkill` broadly, never kill instances not started
  by the script, no hard-coded personal paths, `shellcheck`-clean Bash.
- Never touch the user's real data without restoring it (e.g. a real
  `~/.local/share/AQTRunCat` test must be removed afterwards if it did not
  exist). Never leave temp files or processes. Note: since M6 the app creates
  `~/.local/share/AQTRunCat/metrics/` at startup, so running `tools/*.sh` on
  the real profile creates it — `rmdir` it afterwards if it did not exist.
- Screenshots must be cropped to AQTRunCat UI and exclude unrelated desktop or
  personal data. M4 evidence explicitly lives in `docs/dashboard/*.png`;
  the older M1 spike captures remain ignored historical artifacts.
- End-of-feature checklist the user always asks for: clean build in a fresh
  directory, 0 avoidable warnings, all tests, isolated runtime validation
  (real-session checks are listed for the user, never claimed), earlier
  tools still work, no leftover processes/temp files, no personal paths, a
  topic doc in `docs/` (indexed in `docs/README.md`) + requirements/MEMORY
  update, version bump per `requirements.md`, list of created/modified files,
  test counts, runtime results, open decisions. Report in Italian (the user
  writes Italian; `docs/*.md` are in Italian; code/comments/requirements in
  English).
- Licensing: AQTRunCat is Apache-2.0. Do not use RunCat branding/icon as
  AQTRunCat identity. Do not import `runcat-tray` assets (unclear/GPL
  provenance). Record any copied upstream asset in `THIRD_PARTY_NOTICES.md`
  (repo, commit, original path, license); an asset whose licence cannot be
  verified only by the user's explicit decision, stated as such (the
  Walking Goose and Party Parrot frames).

---

## 1. Project identity

- Name **AQTRunCat**, tagline **"A Cute Run Cat for Linux"**, executable
  **`aqtruncat`**, version **2.1.1** (CMake `PROJECT_VERSION`). History:
  1.0.0 = M0–M9; F<n> raised the minor and its sub-features the patch (1.4.2
  = F4.2, 1.5.6 = F5.1–F5.6, 1.6.3 = F6.1–F6.3, 1.7.4 = F7.1–F7.4); 2.0.0 =
  consolidation of 1.7.4. v2 rule (requirements.md "Versioning"): v2F<n>
  raises the minor (v2F1 → 2.1.x), its sub-features the patch. The D-Bus
  api (1) and payload schema (4 since 2.1.4) change only with their contract. Bump CMakeLists, PKGBUILD, RPM spec (+changelog),
  debian/changelog, the man page and `plasmoid/package/metadata.json`
  (KPlugin.Version) together (`packaging-versions` test).
  All executables accept `-v`/`--version`; the tray menus show "AQTRunCat
  <version>" (disabled item above Quit). `install.sh --no-build` compares the
  build's version (`<build>/src/aqtruncat --version`, else
  CMAKE_PROJECT_VERSION) with the sources and asks "Install the previous
  version X (y) or build the new version Y (N)? [y/N]" (N default, also
  without a terminal).
- Goal: bring the RunCat Neo (macOS) experience to Linux — an animated tray
  runner whose speed follows CPU load, plus system metrics, dashboard, custom
  metrics, metrics bar — as a *proper Linux/Qt application*, not a mechanical
  macOS port and not a fork of `runcat-tray`.
- Stack: C++20, Qt 6 (Widgets/Gui/Core/Test), CMake ≥ 3.21, libarchive ≥ 3.0.
- Primary target: Arch Linux, **KDE Plasma 6 + Wayland**. Avoid KDE-specific
  dependencies in the generic application (no KDE Frameworks or LayerShellQt).
  F4.2 added an optional native Plasmoid (QML only) alongside, never in place
  of, that portable baseline. Metrics core must not depend on Qt GUI, KDE or
  Wayland.
- Philosophy: **"Compatibility, not bug compatibility."** Keep useful RunCat
  Neo compatibility (runner assets, Custom Metrics JSON, animation intent) but
  never reproduce upstream bugs/approximations/macOS limitations. Document
  intentional differences (*Upstream behaviour / AQTRunCat behaviour / Reason*).
- Architecture principle: business logic in UI-independent services; CLI and
  GUI are thin frontends over the same services.

---

## 2. Current project state (verified)

```text
M0 COMPLETE   upstream analysis
M1 COMPLETE   tray spike, CPU-driven runner, cleanup/freeze
M2 COMPLETE   runner system + M2.1 lifecycle (ZIP import/remove) + official CLI
M3 COMPLETE   core metrics + `aqtruncat metrics`
M4 COMPLETE   Dashboard + shared desktop MetricSampler
M5 COMPLETE   Settings and Desktop Integration
M6 COMPLETE   Custom Metrics
M7 COMPLETE   Metrics Bar (separate SNI items)
M8 COMPLETE   Desktop Compatibility & Polish
M9 COMPLETE   Packaging, Installation & Release Readiness — end of the initial roadmap
F1 COMPLETE   Custom Metrics v2 / Executable Custom Metrics
F2 COMPLETE   Claude Code integration: usage quotas 5h/week (revised 2026-10-20), adapter over F1
F3 COMPLETE   Codex integration: usage limits per window (local sessions + opt-in live app-server read), adapter over F1
F4.1 COMPLETE Headless backend aqtruncatd (QtCore+QtDBus), D-Bus API io.github.psykedady.AQTRunCat1 (api 1, schema 1), CLI aqtruncatctl; tray = client
F4.2 COMPLETE Native KDE Plasma 6 Plasmoid io.github.psykedady.aqtruncat (QML package, client of the API), GetRunners/"runners", install.sh --plasmoid/--no-plasmoid
F5 COMPLETE   Additional Built-in Metrics: uptime, GPU (per device), FPS (MangoHud log), package/Flatpak updates (read-only, local metadata by default), kernel, distribution; API schema 2
F6 COMPLETE   Hover Information: CPU/RAM/swap summary in the tray tooltip and the Plasmoid's native tooltip; Metrics Bar/popup details (1.6.3)
F7 COMPLETE   Built-in runners Walking Goose (walkinggoose), Party Parrot (partyparrot), Engine (engine) (1.7.4)
2.0.0         Consolidation release: version 2.0.0, docs by topic (docs/README.md), v2 roadmap in requirements.md; no functional change
2.1.0 DONE    v2F1 baseline (roadmap only; version went 2.0.0 → 2.1.1 directly, no separate 2.1.0 build)
2.1.1 DONE    v2F1.1 Rickroll built-in runner (rickroll, 31 frames 33×36): six built-in runners (docs/built-in-runners.md §10)
2.1.4 DONE    v2F1.4 optional display.showBar (API schema 4) (docs/custom-metrics-commands.md §10)
2.1.3 DONE    v2F1.3 Custom Metric execution warnings (docs/custom-metrics-commands.md §9)
2.1.2 DONE    v2F1.2 optional Custom Metrics "enabled" (+ Settings/Plasmoid checkbox, SetCustomMetricEnabled, API schema 3) (docs/custom-metrics-commands.md §8)
NEXT          v2F1 2.1.5–2.1.7 Custom Metrics v3, v2F2 Independent Autostart (2.2.x), v2F3 Standalone Settings (2.3.x), v2F4 Dependency Advisor (2.4.x): PLANNED, only on instruction
```

v2 roadmap summary (requirements.md is authoritative; added 2026-10-23):

- **Runner-per-feature convention**: every v2F<n> starts with a new
  official *built-in* animated runner at `2.x.1`, right after the `2.x.0`
  baseline (requirements + version metadata only); functional sub-features
  follow from `2.x.2`. The runner reuses the animation architecture, works
  in tray and Plasmoid, is selectable/persisted through the existing
  selection, settings and `GetRunners`, and covers assets, packaging,
  tests, docs and `THIRD_PARTY_NOTICES.md`; existing runners untouched.
  Artwork is supplied/approved by the user *before* implementation — never
  download or generate it. v2F1 runner = **Rickroll** (proposed id
  `rickroll`); v2F2–v2F4 runners are TBD: do not invent names or art.
- **v2F1** 2.1.0 baseline, 2.1.1 Rickroll, 2.1.2 `enabled`, 2.1.3 execution
  warnings, 2.1.4 `display.showBar`, 2.1.5 groups/inheritance, 2.1.6
  reload/rescan, 2.1.7 click-to-refresh (each shifted +1 patch from the
  2.0.0 draft).
- **v2F2** 2.2.1 runner, 2.2.2 independent backend/tray autostart (login
  autostart ≠ D-Bus activation). **v2F3** 2.3.1 runner, 2.3.2 standalone
  Settings.
- **v2F4 Dependency Advisor**: 2.4.1 runner, 2.4.2 shared detection model
  (required/optional/missing/available/unsupported; read-only, offline,
  no root, never installs; one model for installer and CLI, consistent
  with the package recipes' optdepends), 2.4.3 distro-aware installer
  recommendations (no automatic installs; keep `--local`, `--dry-run`,
  `--no-build`), 2.4.4 `checkupdates` preferred on Arch (respect remote
  refresh, never `pacman -Sy`/sudo/pkexec, zero ≠ missing/network/timeout/
  failure; open point: 2.0.0 already uses it when remote is on — decide
  `pacman -Qu` vs `checkupdates --nosync` when remote is off), 2.4.5
  `aqtruncatctl --missing-deps` (exit 0 even with missing optionals,
  works without backend/Plasma). `lm_sensors` is not used by 2.0.0: list
  it only if a feature starts using it.

- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
  Clean build: **0 warnings** (`-Wall -Wextra -Wpedantic` on all targets).
- Tests (2026-10-22, after F7): **53 ctest suites, all passing** (new
  `bundledrunners`; extended backendipc, plasmoidclient, plasmoidlogic 26,
  installer).
- Tests (2026-10-22, after F6): **52 ctest suites, all passing** (new
  `hoversummary` 8; extended metricsbarpresenter 14, metricsbarmodel 21,
  plasmoidlogic 25, plasmoidclient 12).
- Tests (2026-10-22, after F5): **51 ctest suites, all passing** (new
  `uptime` 16, `systeminfo` 14, `gpu` 14, `fps` 12, `packageupdates` 10,
  `updatesservice` 16; extended apischema 12, appsettings 16,
  metricsbarmodel 19, dashboard 15, settingswindow 10, backendipc 13,
  plasmoidlogic 18). Before F5: **45 ctest suites, all passing** on the host (2026-10-21, after
  F4.2: + `plasmoidlogic` qmltestrunner 15, `plasmoidclient` 10,
  `plasmoidpackage` 9, `backendipc` 12, installer 275 checks); containers
  skip 4 Qt cases (locale ×2, root permissions ×2). The plasmoid suites
  register only when qmltestrunner / Qt Qml / dbus-run-session exist;
  `loadsInPlasma` skips without plasmoidviewer.
- Dependencies: Qt 6.5+ (`Widgets`, `Test`; dev machine has 6.11.2), libarchive
  ≥ 3.0 (CMake `FindLibArchive`; configure fails with an explanatory message if
  missing). At runtime on Plasma, Qt's KDE platform theme loads ~28 KF6 libs
  (not a build dependency). F4.2 widget: no build dependency; at runtime
  Plasma 6 QML modules (plasma-workspace, libplasma, Kirigami, KCMUtils, Qt
  Quick Dialogs); its tests optionally use qmltestrunner, Qt6::Qml and
  plasmoidviewer (plasma-sdk).
- CLI: `aqtruncat [run|runner list|runner import|runner remove|metrics|autostart]` (§6).
- Install: `cmake --install` (GNUInstallDirs) is the single source of truth;
  `install.sh` (system /usr/local with sudo only for copies, or `--local` XDG
  without sudo; autostart ON by default; manifest-based uninstall keeping user
  data); native packages in `packaging/` (Arch PKGBUILD, Fedora RPM, Debian);
  Flatpak/AppImage deferred. License Apache-2.0 (`LICENSE`); original icon.
- Runner system: generic runners, bundled `cat` (default), `coffee`,
  `walkinggoose`, `partyparrot`, `engine` (F7), `rickroll` (2.1.1), user
  runners in XDG data dir, Gallery-compatible, safe ZIP import, removal.
- Metrics: CPU, RAM, swap, network, storage capacity, disk I/O via a Qt-free
  layer; consumed by CLI and by the app-owned desktop sampler/Dashboard.
- Settings: typed XDG persistence, runner selection/preview, Normal/Slow-down,
  Automatic/Manual network selection, Dashboard refresh and XDG autostart.
- Custom Metrics: the M6 RunCat Neo-compatible external JSON snapshot source
  remains intact (persistent registration, XDG discovery, watch/retry/atomic
  replace, last good value). F1 adds explicitly typed command sources with a
  bounded direct `QProcess`, scalar output, per-source intervals, a shared
  scheduler, package discovery and structured theme/file icons. Both produce
  the same Snapshot/Source model for Dashboard and Metrics Bar (§13 and
  `docs/custom-metrics-commands.md`). F2 adds `source.output: "snapshot"`
  (multi-value command output) and exit 69 → Unavailable.
- Claude Code integration (F2, revised 2026-10-20): separate QtCore
  executable `aqtruncat-claude-metric` (lib `aqtruncat_claude`, not linked by
  the core) run as a normal F1 command; primary values = plan usage quotas
  (Current session 5h, Current week all models) with reset times; managed
  from Settings › Integrations or its CLI (`enable`/`disable`/`status`/
  `live-updates`), which share one configuration owned by the adapter
  (`docs/integration-claude-code.md`). Snapshot items may carry `id`/`detail`/
  `metricsBarValue`/`metricsBarLabel`; Metrics Bar ids
  `custom-item:<item>:<source>` select one item.
- Installer closes this installation's running AQTRunCat (SIGTERM, ≤10 s,
  exact exe path, never SIGKILL) before replacing/removing files.
- Metrics Bar (M7): one extra QSystemTrayIcon per selected metric next to
  the untouched runner item; **max 3 metric items** (runner always present,
  not counted → worst case 4 SNI); semantic `MetricsBarModel` → SNI renderer/
  presenter; built-ins + custom `metricsBarValue`; opt-in, disabled by default.
- Dev tools: `tools/test-cpu-fps-curve.sh`, `tools/test-cpu-runtime.sh`,
  `tools/cycle-runners.sh`, `tools/test-metrics-runtime.sh` (§10).
- M1.0 spike kept behind `-DAQTRUNCAT_BUILD_SPIKES=ON` (default OFF).

Dev machine (where all runtime validation happened): Arch, kernel 7.2.4-zen2,
Plasma/KWin 6.7.5, KF 6.30, Qt 6.11.2 (upgraded by 2026-10-21, F4.2: kernel
7.2.10-zen1, Plasma 6.8.0, KF 6.31, Qt 6.12.0, plasma-sdk 6.8.0), libarchive 3.8.9, i7-11390H (8 threads),
two NVMe (root ext4 on nvme0n1p7, /home ext4 on nvme1n1p2 with ecryptfs home),
zram swap, Wi-Fi `wlan0`, `docker0`; eDP-1 1920×1080 at scale 1.25 (tray here)
+ HDMI 1.0; panel thickness 33/34; Breeze Light. `clamd` (ClamAV) is in a
crash loop on this machine, keeping ~1 core busy → idle CPU reads ~19–30 %.

---

## 3. Architecture (as implemented)

**Since F4.1 the engine runs in its own process.** `aqtruncatd`
(`src/daemon/main.cpp`, QtCore + QtDBus only) owns `BackendService`
(`src/api/backendservice.*`): AppSettings (+ INI file watch → `reload()`),
the one MetricSampler, CustomMetricsService (F1 + F2/F3 adapters),
CpuMonitor → RunnerSpeedController (fps), AutostartManager and both
ProviderIntegrations, exported on the session bus (contract:
`docs/backend-dbus-api-cli.md`, schema in `src/api/apischema.*`).
`aqtruncat run` (trayapp) is a client through `BackendClient`
(`src/api/backendclient.*`, implements `CustomMetricsModel`): it rebuilds
the system snapshot / sources from JSON for the unchanged Dashboard,
Metrics Bar and Settings, and animates at the received fps (no sampler, no
Custom Metrics, no CPU monitor, no autostart in the frontend). The diagram
below is the pre-F4.1 single-process view: the sampler/CpuMonitor/speed half
now lives in aqtruncatd, the RunnerAnimator/TrayPresenter half in the tray.
`aqtruncatctl` (`src/ctl/main.cpp`) is the reference CLI client.

**Since F4.2** the KDE Plasma 6 widget (`plasmoid/package`, installed to
`share/plasma/plasmoids/io.github.psykedady.aqtruncat`) is a third, QML-only
client: `BackendClient.qml` (Plasma's `org.kde.plasma.workspace.dbus`) keeps
state/deltas/runner/settings/integrations, `RunnerView.qml` animates the
frames listed by `GetRunners` locally at the received fps,
`FullRepresentation.qml` is the Plasma popup. `aqtruncatd` gets the
catalogue from `Config::runnerCatalog` (set in `daemon/main.cpp` with the
bundled runners at `<bindir>/../share/aqtruncat/runners`).

**Since F5** the same `SystemMetricsSampler` also fills `uptime`, `gpu`
(`metrics::GpuSampler`: DRM discovery, amdgpu/i915/xe sysfs, NVML via
dlopen), `fps` (`metrics::MangoHudFpsReader`), and every 10 min `kernel` /
`distribution` (`metrics/systeminfo`); `packageUpdates`/`flatpakUpdates` are
filled by the backend from `UpdatesService` (`src/app/updatesservice.*`,
bounded subprocesses `BoundedProcess`, shared `app/processgroup.h` with F1)
just before publishing. `apischema` turns them into `builtin` objects
(`gpu:<id>` per device; schema 2) and back (`systemSnapshotFromMetrics`), so
the Dashboard and the Metrics Bar keep reading one typed snapshot.

```text
                     src/app/main.cpp  (parse → dispatch only)
                              │
        ┌─────────────────────┼──────────────────────────┐
   run (QApplication)   runner … (QCoreApplication)   metrics (no QCoreApplication)
   src/app/trayapp.cpp   src/cli/runnercommands.cpp    src/cli/metricscommand.cpp
        │                       │                             │
        │                 RunnerService (src/runner)    SystemMetricsSampler (src/metrics)
        │                                                     │
        │                                               SystemMetricsSnapshot
        │                                                     │
        │                         app-owned MetricSampler (QTimer ~1 Hz)
        │                                    ┌─────────┴─────────┐
        │                                    ▼                   ▼
        │                           CpuMonitor (EMA .4)    DashboardWindow
        │                                    │             (raw snapshot)
        ▼                                    ▼
  RunnerAnimator ◄─ FpsStabilizer ◄─ selected speed curve ───┘
        │
  TrayPresenter ─► QSystemTrayIcon
```

Static libraries (see `src/CMakeLists.txt`):

| Target | Content | Deps |
|---|---|---|
| `aqtruncat_core` | `core/`: cpustat (parse/delta/sampler/countLogicalCpus), smoothing (Ema), speedcurve (cpuToFps, FpsStabilizer), frameplayback | none (C++) |
| `aqtruncat_runner` | `runner/`: model, framenames, loader, catalog, animator, archive, installer, remover, service | core, Qt6::Gui, libarchive (private) |
| `aqtruncat_metrics` | `metrics/`: status, counterrates, memory, network, diskio, storage, systemmetrics, metricsformat, textparse | core only (no Qt) |
| `aqtruncat_cli` | `cli/`: commandline (grammar/help/exit codes), runnercommands, metricscommand | runner, metrics |
| `aqtruncat_engine` (F4.1) | `app/` QtCore part + `custommetrics/`: AppSettings, MetricSampler, CustomMetricsModel/Service/parser, CpuMonitor, RunnerSpeedController, autostart, ProviderIntegration, metricsbarids, storage paths | metrics, Qt6::Core |
| `aqtruncat_api` (F4.1) | `api/`: apischema, BackendService, BackendClient | engine, Qt6::DBus |
| `aqtruncat_desktop` | `app/` UI only: trayapp, Dashboard/Settings/preview, Metrics Bar model/renderer/presenter, tray presenter | api, engine, runner, Qt6::Widgets |
| `aqtruncat` (exe) | `app/`: main, trayapp, logging; bundled runners as qrc | desktop, CLI + Qt6::Widgets |
| `aqtruncat_runner_catalog` (F4.2) | `api/runnercatalog`, `app/runnerselection`: GetRunners payload and the start-up runner fallback | api, runner (QtGui) |
| `aqtruncatd` / `aqtruncatctl` (exe, F4.1) | `daemon/main.cpp`, `ctl/main.cpp` | aqtruncatd: api + runner_catalog (QtCore, QtDBus, QtGui for PNG decoding only, no QGuiApplication); aqtruncatctl: api (QtCore + QtDBus only) |

Key components:

- **MetricSampler** (`src/app/metricsampler.*`): application-owned QObject
  wrapper over `SystemMetricsSampler`; one coarse 1 s timer, immediate baseline
  snapshot, idempotent start/stop, emits typed snapshots to desktop consumers.
- **AppSettings** (`src/app/appsettings.*`): typed application-owned settings
  over QSettings INI in `$XDG_CONFIG_HOME/AQTRunCat/AQTRunCat.ini`; immediate
  validated persistence and signals, independent from Widgets.
- **CustomMetricsService** (`src/app/custommetricsservice.*`, model/parser in
  `src/custommetrics/`): application-owned registry for explicit paths and
  `$XDG_DATA_HOME/AQTRunCat/metrics/*.json`; canonical deduplication, one
  QFileSystemWatcher over files/directories, atomic-replace recovery, 100 ms
  debounce, 3×200 ms transient retry and shared 5 s degraded-only recovery.
  Publishes separate typed custom snapshots and never samples system metrics.
- **CpuMonitor** (`src/app/cpumonitor.*`): consumes raw CPU from shared
  snapshots, keeps `Ema(0.4)` and emits `sampled(raw, smoothed)`; it owns no
  source and no timer. Pending/unavailable/error snapshots are ignored.
- **trayapp** (`runTrayApplication`): resolves CLI > persistent > default,
  chooses runner via `RunnerService`,
  owns one MetricSampler and Dashboard, wires snapshots to both Dashboard and
  runner CPU, then `sampled → RunnerSpeedController (Normal/Slow-down) →
  FpsStabilizer::update → RunnerAnimator::setFps`; `--fps N` bypasses runner
  CPU only; SIGINT/SIGTERM
  via self-pipe → clean quit.
- **RunnerAnimator** (`src/runner/runneranimator.*`): `Qt::PreciseTimer`,
  interval `round(1000/fps)`; `setFps` only changes interval (position kept);
  `setSequence` (runner switch) restarts at the new runner's first frame,
  keeps timer/FPS, emits `frameChanged(index)`.
- **DashboardWindow** (`src/app/dashboardwindow.*`): reusable `Qt::Tool`
  Widgets window; closing hides it; snapshot-driven CPU/memory/swap/network/
  Root+Home/disk presentation with explicit unavailable states.
- **SettingsWindow** (`src/app/settingswindow.*`): one lazy reusable `Qt::Tool`
  over `AppSettings`; RunnerService-backed selection, fixed-20-FPS preview,
  speed/network/refresh/autostart controls and Restore Defaults. Closing hides.
  F2 revision: **Integrations** group (Claude Code) driven by a
  `ProviderIntegration` (`src/app/providerintegration.*`): runs the adapter's
  `status --json` / `enable` / `disable` / `live-updates` asynchronously, one
  at a time, and watches the files `status` reports; created lazily with the
  Settings window in `trayapp.cpp` (adapter found next to the running
  `aqtruncat`, else in PATH). The integration's package is hidden from the
  Custom Metrics list.
- **TrayPresenter** (`src/app/traypresenter.*`): builds one `QIcon` per frame
  once per runner (and on `QEvent::ApplicationPaletteChange`); monochrome
  runners tinted with `QPalette::WindowText` (`CompositionMode_SourceIn`),
  colour runners untouched; tooltip "AQTRunCat — <name>" + the F6 hover
  summary (`setSummary`, from `src/app/hoversummary.*`; sent only on a text
  change, together with the next frame, 250 ms fallback); context menu
  Dashboard show/hide, `Settings…` and "Quit AQTRunCat". Primary tray
  activation toggles the Dashboard. No path/CPU knowledge. Images keep native
  size/aspect.
- **RunnerService** (`src/runner/runnerservice.*`): facade for every frontend:
  `entries()`, `list()`, `choose(requested, default)`,
  `importArchive(path, {id, replaceExisting, limits})`, `remove(id)`.
  Built with `RunnerLocations{bundledRoot=":/runners", userRoot=userRunnersDirectory()}`.
- **Import lifecycle**: `extractRunnerArchive` (QTemporaryDir) → `loadRunner`
  validation → `installRunner` (staging + atomic rename) — three separate
  components; `RunnerError {None, NotFound, Invalid, Conflict, Filesystem}`.
- **SystemMetricsSampler** (`src/metrics/systemmetrics.*`): the single owner
  of metric reads; `sample(monotonicSeconds)` → `SystemMetricsSnapshot`.
  Sources injectable (`MetricsSources{procRoot, sysRoot, storagePaths,
  statFilesystem}`) for tests. No timers inside; callers drive it.
- Logging categories `aqtruncat.cpu`, `aqtruncat.speed`, `aqtruncat.runner`
  (debug off by default; `--verbose` or `QT_LOGGING_RULES`). **Qt sends logs
  to journald when stderr is not a TTY** (`journalctl --user _COMM=aqtruncat`,
  force stderr with `QT_FORCE_STDERR_LOGGING=1`). Therefore CLI errors are
  always written with `fprintf(stderr)`/`QTextStream(stderr)`, never only qWarning.

- **MetricsBarModel** (`src/app/metricsbarmodel.*`): semantic items (id,
  label, shortLabel, shortValue, detail, status) from the shared system
  snapshot + M6 sources + settings; zero-delay coalescing, emits only on
  change; no sampler/timer. **Renderer** (`metricsbarrenderer.*`): square
  22-unit canvas, caption over bold value, QFontMetricsF fitting (value
  13→7, caption 9→6), `###` overflow, `…`/`—`/`!` states, palette WindowText,
  image side 22·max(2,dpr). **MetricsBarPresenter** (`metricsbarpresenter.*`):
  one `MetricsBarTrayItem` (QSystemTrayIcon wrapper, fakeable) per item,
  setIcon only when rendered content changes, setToolTip only when text
  changes, reorder = keep ordered prefix and re-create the rest; own
  Dashboard/Settings/Quit actions; primary click toggles Dashboard.
- Compact formatter `src/metrics/compactformat.*` (Qt-free): `23%`,
  `850B`/`1.2K`/`850K`/`1.0M`…, ≤ 4 chars, locale-independent.
- Shared `customSymbolText` (`src/app/customsymbol.*`) maps custom `symbol`
  for Dashboard and Metrics Bar.

---

## 4. Milestone history (operative summary)

### M0 — Upstream analysis (docs/upstream-runcat-neo-analysis.md)
- Upstreams analysed from source: RunCat Neo `b3b1543` (Swift/SwiftUI, macOS 26,
  Apache-2.0), SystemInfoKit 7.2.2 `56bfde2` (its metrics), Runner Gallery
  `b979658` (43 runners, Apache-2.0), runcat-tray `9fe316b` (C/GTK3
  AppIndicator, BSD-2; ~5 % of Neo's features, buggy CPU math, strcmp frame
  ordering, unclear-licence assets).
- Upstream speed: `speed = clamp(cpu/5, 1, 20)`, `fps = 2·speed` (linear 2–40
  FPS, CAKeyframeAnimation), speed changes only per metrics sample (3/5/10 s),
  phase-continuous. Upstream slow-down: `0.5·(21 − v)`.
- Upstream quirks NOT to copy: CPU busy excludes `nice` but nice is in the
  total; 99.9 % cap; network sums all interfaces incl. loopback and divides by
  the nominal interval; memory "Pressure" is (wired+compressed)/max, not kernel
  pressure.
- Custom Metrics schema/behaviour fully documented in M0 §6 (input for M6).
- Assets: RunCat Neo built-in frames and Gallery runners are Apache-2.0
  (reusable with attribution); RunCat name/icon are trademarks (not usable).

### M1 — Cat Lives
- **M1.0 tray spike** (docs/tray-spike.md): result **B — Qt tray is
  sufficient with compromises**. Findings:
  - On Plasma, `QSystemTrayIcon` → plasma-integration `KDEPlatformSystemTrayIcon`
    → `KStatusNotifierItem`: each `setIcon` emits `NewIcon` + `NewToolTip`
    (tooltip carries an icon copy) and sends the pixmap at native size;
    plasmashell does one `GetAll` per frame (10 ms coalescing timer).
    Generic Qt path (`QT_QPA_PLATFORMTHEME=generic`) letterboxes to square, adds
    22×22, drops >64 px, and its palette is default black (invisible on dark panels).
  - Plasma tray slot is square ≈22 logical px; wide runners shrink (cat 56×36 →
    ~24×14 physical px at 1.25 scale; engine 83 px barely legible).
  - Cost is linear: ≈0.42 % of one core per FPS desktop-wide (plasmashell
    ≈0.2 %/fps, kwin ≈0.17 %/fps; the app itself ≈0.035 %/fps); 40 fps ≈ 17 %,
    20 fps ≈ 8.3 %; plasmashell RSS +~30 MB at 40 fps then plateau.
- **Manual visual test (user)**: <10 FPS unsatisfying, 10 good minimum, up to 40
  very good but costly → approved curve below; linear upstream curve rejected.
- **M1.1 CPU runner** (docs/cpu-and-runner-speed.md): `/proc/stat`, 1 s sampling,
  EMA α 0.4, FpsStabilizer (immediate ≥3 FPS, smaller changes need 3
  consecutive samples) chosen by replaying recorded data (step 2 → 7 flips in
  24 s at half load; step 3 → 1 flip). Runtime: idle 11 fps, half 26–27, full
  39–40, recovery fast; real tray FPS confirmed via D-Bus `NewIcon` rate.
- **M1.2 cleanup/freeze**: iowait = idle (final); slow-down curve fixed
  (mirrored, not implemented); permanent tools moved to `tools/`; spike tools
  stay in `spike/m1-tray/tools/`.
- Curve: `fps = round(10 + 30·(cpu/100)²)`, cpu clamped [0,100] → 10–40 FPS.

### M2 — Runner system (docs/runners.md, docs/runner-import-and-cli.md)
- Generic `Runner` model; nothing in code knows "cat" (default id only in CMake).
- Bundled `cat` + `coffee` (RunCat Neo frames, `third_party/`, + own metadata).
- Loader with fatal/warning split, natural ordering, Gallery layouts, metadata,
  monochrome detection; catalog with XDG user dir and user > bundled precedence.
- **M2.1**: libarchive ZIP import (secure extraction → validation → atomic
  install), `--force` replace with rollback, removal (bundled protected), the
  official sub-command CLI with legacy aliases and exit codes 0–5.
- Verified: all 43 Gallery ZIPs import/list/run; frame order verified end-to-end
  through D-Bus on 7 runners incl. a 24-frame one.

### M3 — Core metrics (docs/system-metrics.md)
- Qt-free metrics layer, typed snapshot, explicit status (unavailable ≠ 0),
  one CPU implementation shared with the runner, real-elapsed-time rates,
  hardware-only network aggregation, whole-physical-disk I/O aggregation, df
  storage semantics, locale-independent binary formatter, `aqtruncat metrics`
  and `--watch`.
- Validated against `free`, `df`, independent `/proc` counters; ~108 µs per
  snapshot, `metrics --watch` ≈0.05 % CPU.

### M4 — Dashboard (docs/dashboard.md)
- Compact Qt Widgets Dashboard with CPU, memory, swap, aggregate network,
  Root/Home capacity and aggregate physical-disk I/O; explicit N/A/waiting.
- One desktop MetricSampler feeds both Dashboard and runner. CpuMonitor now
  retains only the M1 EMA; curve and stabiliser are unchanged.
- Primary tray activation toggles one reusable `Qt::Tool` window; DBusMenu
  supplies Show/Hide fallback and Quit. Closing the window only hides it.
- Root is always sampled; Home only when `stat(2).st_dev` differs. Plasma 6
  Wayland, fractional scale, runtime tools and hidden/visible performance were
  validated; screenshots are under `docs/dashboard/`.

### M5 — Settings and Desktop Integration (docs/settings-and-desktop-integration.md)
- `AppSettings` provides typed immediate persistence through QSettings INI in
  XDG config. Precedence is explicit CLI > persistent setting > default.
- One reusable Settings window selects runners via RunnerService, previews
  decoded frames at fixed 20 FPS, chooses Normal/Slow-down, configures network
  Automatic/Manual, Dashboard presentation refresh and XDG autostart.
- Slow-down is the frozen mirrored curve downstream of the unchanged shared
  CPU/EMA pipeline. `--fps` still bypasses it.
- Manual network counts exactly selected names, including virtual links;
  absent names remain persisted and policy changes reset the rate baseline.
- Plasma 6 Wayland lifecycle, persistence, fallback, performance and prior
  diagnostic tools were validated. Final result: 24 suites / 359 cases.

### M9 — Packaging, Installation & Release Readiness (docs/installation-and-packaging.md)
- Added: `LICENSE` (Apache-2.0, canonical text with generic appendix), original
  icon `data/icons/hicolor/{16..256,scalable}/apps/aqtruncat.*` (SVG source +
  PNGs rendered with rsvg-convert), `data/aqtruncat.desktop` (Exec=aqtruncat,
  Categories=System;Monitor;), man page `data/aqtruncat.1`, CMake install
  rules (+ `BUILD_TESTING` option, DOCDIR share/doc/aqtruncat, no install
  RUNPATH: `CMAKE_INSTALL_RPATH_USE_LINK_PATH`), CLI `aqtruncat autostart
  enable|disable|status` (src/app/autostartcommand.*; AutostartManager now
  uses the canonical executable path and writes Icon=aqtruncat).
- `install.sh`: options --local, -A/--autostart=true, -na/--autostart=false,
  -U/--uninstall, --prefix=, --dry-run, --build-dir=, -h/--help; distro
  detection via os-release ID/ID_LIKE (AQTRUNCAT_OS_RELEASE override for
  tests); prints package commands, never installs packages; temp build with
  Ninja if present; stage with `cmake --install --prefix /` and map bin/,
  share/; manifest at <prefix>/share/aqtruncat/ or $XDG_STATE_HOME/aqtruncat/;
  autostart via the app's own command as the real user (SUDO_USER aware).
- Packaging: `packaging/make-source-tarball.sh`, `packaging/arch/PKGBUILD`,
  `packaging/rpm/aqtruncat.spec`, `packaging/debian/` (copied to debian/ in
  the extracted tarball). Maintainer = placeholder
  `maintainers@aqtruncat.invalid` (replace before publishing); no public URL
  exists yet (rpmlint/lintian warnings documented).
- Bugs found and fixed in M9: manifest validation inside a process
  substitution did not abort (foreign manifest accepted); Qt version detection
  concatenated qmake usage text (failed on Arch with Qt 6.12 and no qtpaths6);
  default Makefile generator failed where only ninja is installed (minimal
  Arch); RPM RUNPATH from qt_standard_project_setup; LICENSE duplicated in RPM;
  Debian synopsis article; missing man page.
- Final battery (sequential after an out-of-memory abort of the parallel run):
  makepkg check 32/32; rpmbuild/dpkg-buildpackage 32/32; rpmlint 0 errors;
  installer scenarios PASS on 4 distros. Host: 0 warnings, 32/32 ctest.
- Verified: makepkg + pacman -U/-R in
  archlinux container; rpmbuild + rpmlint + dnf in Fedora 44; dpkg-buildpackage
  + lintian + apt in Debian 13 and Ubuntu 26.04; real install.sh (root, user +
  sudo, `sudo ./install.sh`, --local) in all four containers; --local install
  launched in the real Plasma session (runner + bar registered).

### M8 — Desktop Compatibility & Polish (docs/desktop-compatibility.md)
- Distros (docker containers, source mounted read-only): Fedora 44 (Qt 6.11),
  Ubuntu 26.04 LTS (Qt 6.10), Debian 13 (Qt 6.8): build 0 warnings, 30/30
  suites, 3 expected skips (locale ×2, root perms). Ubuntu 24.04 (Qt 6.4.2)
  rejected by configure; user said ignore it → README warning (README.md was
  created in M8 for this: build, dev packages, warning).
- Desktops: Plasma 6.7.5 Wayland PASS; XWayland (`QT_QPA_PLATFORM=xcb`) PASS
  WITH LIMITATION; Fedora build on host Plasma with generic/GNOME Qt theme;
  waybar in nested sway (Fedora container); stalonetray XEmbed in Xvfb
  (container, private D-Bus without watcher); no-tray startup. NOT TESTED:
  Plasma X11 session (kwin_x11 not installed), GNOME, XFCE, LXQt.
- Bugs fixed: (1) Quit ignored with Dashboard AND Settings visible —
  QCoreApplication::quit() is cancelled by windows refusing close → new
  `requestApplicationQuit()` (exit(0)) for both tray menus and SIGINT/SIGTERM;
  (2) runner/bar black on dark panels with non-KDE Qt themes (GNOME theme
  reports colorScheme Dark but keeps light palette) → `trayForegroundColour()`
  in runnerpresentation (palette Window when scheme contradicts palette) +
  re-tint on QStyleHints::colorSchemeChanged; (3) repeated
  `inotify_add_watch ... Permission denied` for unreadable custom files →
  not watched while unreadable.
- Suspend: real s2idle suspend (user woke the machine) during soak PASS;
  CLOCK_MONOTONIC stops in suspend (8.1 s monotonic over 151 s wall). SIGSTOP
  60 s stall PASS. Tray: plasmashell restart PASS; kded6 (watcher) restart
  PASS WITH LIMITATION (items re-register, visual order may change).
- Final perf (quiet session, runner Normal ~11 fps): runner only 0.40–0.42 %
  core / 58.4 MiB; worst case runner+3 metrics 0.45–0.47 % / 60.4 MiB, bar
  +2.1–2.3 NewIcon/s, plasmashell +0.15 pt; Dashboard visible +0.1 pt and
  +5 MiB; Settings open costs ~2.5 pt (20 fps preview, by design M5).

### M7 — Metrics Bar (docs/metrics-bar.md)
- Spike (Plasma 6.7.5): square ~22 px SNI cells, no text label
  (`XAyatanaLabel` unused, `Title` not shown in panel), bigger icons are
  rescaled; a wide label is illegible, `23%` legible. All items of one
  process share `Id = Title = AQTRunCat`; Plasma sorts by Title, ties by
  registration order, so the bar groups right after the runner. Per-item
  Plasma visibility settings apply to all AQTRunCat items together.
  Registration is async per KSNI connection → order is best effort.
- Two-line icon (identity caption + value) chosen after visual comparison:
  value size is width-bound, so the caption costs nothing.
- Settings "Metrics Bar" section (enable, checkable ordered list, Move
  Up/Down, "N of 3" count; at 3 selected the unselected rows are disabled);
  INI `metricsBar/enabled` (false), `metricsBar/items` (≤ 3 ordered ids,
  default `cpu`). Legacy/hand-edited lists: drop empty/malformed/unknown/
  duplicate ids, keep first 3 in stored order. Limit enforced in AppSettings
  AND in `MetricsBarModel::setConfiguration` (`metricsbar::MaxItems = 3`).
  A selected-but-absent custom source keeps its slot (no 4th item moves up).
- User requirement (2026-10-09): the runner is never part of, replaced or
  removed by the bar; acceptance worst case = animated runner (normal CPU
  mode) + 3 metrics. Component harnesses that show metric SNIs without a
  runner are exploratory only.
- Acceptance gate (runner animated in Normal mode + 3 metrics, 4 SNI): bar
  +2.2–2.9 NewIcon/s, app 0.47 % core, RSS 60.2 MiB; runner unchanged
  (~11 fps idle). Stress 11 SNI ×600 s: 0 warnings, 0 orphans, exit 0.
  Details: docs/metrics-bar.md §10–§11. Final: 30 suites / 472 cases.
- M7 fixed `tools/test-cpu-runtime.sh` to count the runner item's NewIcon.

### M6 — Custom Metrics (docs/custom-metrics.md)
- AQTRunCat consumes the documented RunCat Neo JSON snapshot schema; producers
  own `formattedValue`, timestamps and update cadence. No source providers,
  QProcess, commands, HTTP, per-metric timers or internal scheduler exist.
- Explicit file registrations persist in AppSettings; Linux auto-discovery is
  finalised as `$XDG_DATA_HOME/AQTRunCat/metrics/*.json`. Existing paths use
  canonical identity, so explicit/discovered and symlink aliases deduplicate.
- The application-owned service watches files and parent directories, survives
  atomic replace, retries transient failures without blocking, retains the
  last known good snapshot and isolates every broken source.
- Dashboard and Settings are scrollable consumers. `metricsBarValue` is
  preserved in the model for M7, but no Metrics Bar was implemented.
- Plasma/Wayland validation used an isolated XDG profile. Final result:
  26 suites / 385 cases; idle cost at fixed 10 FPS stayed 0.4–0.5% of one core
  for 0/5/20 snapshots, with RSS 59.1/59.2/61.3 MiB and 1/6/21 inotify watches.

---

## 5. Frozen decisions / invariants (do not "fix")

| Decision | Value | Why (don't revert) |
|---|---|---|
| CPU active/idle | active = user+nice+system+irq+softirq+steal; idle = idle+iowait; guest/guest_nice ignored | nice is real work (upstream bug excluded it); iowait = core waiting, must not speed up runner; guest already in user/nice |
| CPU ceiling | none (0–100, no 99.9 cap) | upstream artefact |
| CPU anomalies | per-field regression → delta 0 + flag; total Δ 0 → no value; all regress → rebaseline | iowait can go backwards on real kernels |
| Speed curve | `fps = round(10 + 30·(cpu/100)²)`, clamp, NaN/inf → 10 | approved after visual test; quadratic saves tray cost at mid load |
| FPS range | 10 min, 40 max | <10 ugly; >40 not validated, costly |
| Slow-down | `slow(cpu) = round(10 + 30·(1 − cpu/100)²)` | M5 mirrored curve; same 10–40 range; replaces upstream 0.5·(21−v) |
| CPU sampling | one desktop MetricSampler at 1 s, independent from animation timer | runner and Dashboard share the raw sample; never read /proc/stat twice/per frame |
| Smoothing | EMA α 0.4 (τ≈2 s), first sample initialises | measured trade-off |
| Stabiliser | apply at once if \|Δ\| ≥ 3 FPS, else after 3 consecutive equal targets; starts at 10 | removes 26↔28 flicker without slowing reaction |
| Animation | speed change keeps current frame; new runner restarts at its frame 0; icons prebuilt | no flicker / no disk access per tick |
| Rates | Δbytes / actual monotonic elapsed time | upstream divided by nominal interval |
| Memory | used = MemTotal − MemAvailable; % = used/total; fallback estimate flagged | cache is not "used" |
| Swap | separate section, never added to RAM | |
| Network Automatic | only interfaces with `/sys/class/net/<if>/device`; lo + virtual excluded; per-interface kept | VPN/container traffic also crosses the NIC → double counting |
| Network Manual | exactly selected interface names, including virtual; absent names retained; policy change resets baseline | explicit user override without spikes |
| Disk I/O aggregate | only whole disks with `/sys/block/<d>/device`, not `hidden`; partitions/dm/md/loop/zram excluded | stacked devices re-count the same I/O; zram is RAM |
| Storage | df semantics: used=(blocks−bfree)·frsize, avail=bavail·frsize, %=used/(used+avail) | root-reserved space neither used nor available |
| Desktop storage paths | Root always; Home only when `stat(2).st_dev` differs | filesystem identity, never path-string comparison |
| Unavailable | `std::optional` empty + `MetricStatus`, never 0 | 0 and "unknown" differ |
| Built-in system metric sources (M3–M9/current) | `/proc`, `/sys`, `statvfs` only; no external commands | F1 command sources are explicitly configured Custom Metrics, not a replacement for direct built-in metric sources |
| Formatting | binary units, 1 decimal, `std::to_chars` (locale-independent) | QCoreApplication sets the system locale (Italian comma) |
| Runner precedence | user > bundled; same-root dup id → first in natural order wins + note | |
| Explicit `--runner` missing/invalid | error, never fallback | default runner may fall back with a warning |
| Bundled runners | `cat` (default), `coffee`, `walkinggoose`, `partyparrot`, `engine` (F7), `rickroll` (2.1.1) | one new runner per v2 feature at 2.x.1, only with user-supplied/approved art; don't add others without being asked |
| XDG | runners: `$XDG_DATA_HOME/AQTRunCat/runners/`; settings: `$XDG_CONFIG_HOME/AQTRunCat/AQTRunCat.ini`; autostart: `$XDG_CONFIG_HOME/autostart/aqtruncat.desktop` | Qt/XDG standard locations, data and config separate |
| Custom Metrics snapshot contract (M6, preserved by F1) | external RunCat Neo JSON snapshots; `formattedValue` stays opaque; snapshot sources run no commands/providers/scheduler | compatibility with existing producers; F1 deliberately adds a separate command-backed source while retaining this snapshot contract |
| Custom Metrics command contract (F1) | explicit `source.type=command`; required `id`, `title`, `executable`; direct executable + ≤256 args, no shell/elevation; scalar UTF-8 stdout; prefix/suffix and optional normalized range | invalid/malformed config is never executed or treated as snapshot |
| Command scheduling/lifecycle (F1) | one shared monotonic timer; immediate first run; next interval (1–86400 s) starts after completion; timeout default 10 s/range 1–300 s; no overlap/catch-up; each command is its own process-group leader (`setpgid` child modifier); timeout/overflow SIGKILL the group, then re-check every 1 s (no spin); stop = SIGTERM all groups, ≤250 ms total wait, SIGKILL, final group SIGKILL; helpers left in background after a *successful* run are not killed | independent schedules without CPU-sampler coupling or idle polling; scripts' children never leak |
| Command publish cost (F1) | service publishes at start and end of every run; hidden Dashboard stores the list and rebuilds cards only on show; Settings keeps the selected custom row across rebuilds | 5 commands cost 2.2 % → 0.87 % of a core after deferral |
| Command output/state (F1, 2.1.3) | stdout/stderr each ≤16 KiB; one trimmed nonempty scalar line; exit 69 = Unavailable; missing/non-executable (Unavailable until 2.1.2) and startup/exit/timeout/parse = Error with a concise reason (status + first stderr line ≤200 chars; "Invalid output: …"); Error shows no value anywhere; refresh = Pending retaining last value; Dashboard presents the last settled state during a refresh (no "Waiting…" flicker) | bounded failure, diagnostic stderr, never silent zero |
| Claude Code integration (F2, revised 2026-10-20) | adapter outside the core, run as F1 `output:"snapshot"` command every 10 s; **primary = plan quotas** five_hour / seven_day from `~/.claude.json` `cachedUsageUtilization` (what `/usage` fetched; same account only) and, opt-in, status-line `rate_limits` captured to `$XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json`; newest report per window wins; >5 min → "as of", past reset or >24 h → not shown; context window only with `--context`; opt-in, never auto-added to the bar | exactly the `/usage` concepts; no credentials/network/tokens; never derived from context tokens; never 0 for missing |
| Live updates (F2) | only the `statusLine` member of Claude Code's user settings.json, textual edit verified by parse-compare, refuse otherwise; backup (value + exact text) in `$XDG_DATA_HOME/AQTRunCat/integrations/claude-code/statusline-backup.json`; helper chains the previous command via memfd + `execl("/bin/sh","-c",…)`; off/disable restore byte-exactly; user-changed statusLine left alone; explicit confirmation in Settings | user decision 2026-10-20 (hybrid source); never silently modify Claude config |
| Integration configuration ownership (F2) | the adapter owns package + backup; Settings runs the adapter CLI (`status --json` etc.), so GUI = CLI | one configuration, no second metric engine |
| Integration provenance (F2) | optional top-level `"integration": "<id>"` in a **command** configuration (written by the adapter) → `CommandDefinition::integration` → `Source::integration`; empty = user Custom Metric; M6 snapshots never; never by title/icon/path. Dashboard: **Integrations** section before **Custom Metrics**, same card renderer, each source once, empty sections hidden (no "No custom metrics" label). Old unmarked package: Settings rewrites it once (`integrationMarked` in `status --json`) | user requirement 2026-10-20: provider ≠ user metric |
| Snapshot item extensions (F2) | optional `id` (`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`, unique), `detail`, `metricsBarValue`, `metricsBarLabel`; bar id `custom-item:<item>:<source>`; item without compact value = Unavailable; caption replaces the source icon | multi-value providers without Claude-specific UI code |
| Codex integration (F3) | adapter outside the core; usage limits of the `codex` bucket per window length (labels/ids/captions from minutes); local rollouts by default, opt-in live `codex app-server` `account/rateLimits/read` ≤ every 5 min (retry 60 s, cached, no account id); newest per window; fresh 10 min, past reset/24 h hidden; never Codex config/credentials; GUI confirmation for live | user decisions 2026-10-20 (hybrid; few live reads for validation) |
| Item warnings (2026-10-20) | optional snapshot item `"warning": true` (bool) → Dashboard warning icon (`dialog-warning`, style fallback) + detail tooltip; quota windows not current = "Out of date"/"Not reported" with warning; live updates off + a window not current → "Usage data" warning explaining when values change and where to enable live updates (Claude: adapter reads `bridgeStatus`; Codex: `--live`) | user: "Unknown" without live updates was not intuitive |
| Test suite isolation (2026-10-20) | `installer` ctest is RUN_SERIAL; it brings the build to "no work to do" before testing `--no-build`; integration QTRY timeouts 15 s | the installer test builds the shared build tree; parallel relinks replaced binaries in use (the long-standing intermittent failures); ninja 1.13 needs several passes after incremental rebuilds |
| Shared provider code (F3) | `src/providers/common` quota + package helpers used by both adapters; no generic provider framework | avoid copying F2 without a large refactor |
| Installer sudo retry (2026-10-20) | `authenticate_sudo` after build/stage (and before uninstall): `sudo -v` refused → warning "wrong or expired password?"; on a TTY ask "Retry installing the build? [y/N]" (uninstall: "Retry the uninstallation?") and loop without rebuilding; N/EOF/no TTY → die, nothing changed, hint `--no-build` (build kept) | user request: expiring password should not waste the build |
| Installer options summary (2026-10-20) | every run (also `--dry-run`) starts with "AQTRunCat installer" and `  key               = value (origin)` lines: action, installation type, rebuild, autostart, dry run, binary, data, manifest, privileges, stop timeout, distribution, running; replaced the old `Action:`/`Autostart:` dry-run plan | user request: see the options in effect at start |
| Installer build (2026-10-20) | default = from scratch every time in persistent `${XDG_CACHE_HOME:-~/.cache}/aqtruncat/install-build` (marker `.aqtruncat-install-build`; root → root's cache); `--no-build` reuses it only if same source tree + Release + `ninja -n` "no work to do" (`make -q` otherwise), else error, nothing installed; `--build-dir` now builds all targets | user: default must stay a full build; full Release build ≈ 97 s on the dev machine (8 jobs on 4 cores + clamd), `--no-build` ≈ 0.2 s |
| Installer running instance (F2) | before copy/removal: SIGTERM to the real user's processes whose `/proc/PID/exe` is exactly `BINDIR/aqtruncat` (or "(deleted)"); comm only as prefilter; wait ≤10 s (`AQTRUNCAT_STOP_TIMEOUT` for tests), start time vs PID reuse, zombie = exited; timeout → error, nothing changed, no SIGKILL; preflight (build/stage/manifest/`sudo -v`) first; no restart; other users only reported | user requirement 2026-10-20 |
| Custom Metrics XDG | non-recursive `$XDG_DATA_HOME/AQTRunCat/metrics/*.json` plus immediate `$XDG_DATA_HOME/AQTRunCat/metrics/<package>/metric.json`; explicit paths persist in INI | package level permits colocated relative executable/icon while preserving M6 standalone discovery |
| Custom symbols (F1) | legacy string or `{type: theme|file, value}`; file/executable relative paths and command CWD resolve from config directory; invalid icon uses fallback without invalidating metric | deterministic self-contained packages, never process CWD |
| Custom identity/order | canonical path when resolvable; explicit order first, discovered-only by stable name/path | no duplicate aliases or filesystem-order dependence |
| Custom failure/retry | last-known-good retained; 100 ms debounce, 3×200 ms retry, shared degraded-only 5 s recovery | atomic saves do not flash errors; no healthy-state polling |
| Custom input limits | 1 MiB JSON, 256 entries, 4096 chars per known string | local malformed/accidentally huge files cannot exhaust the tray app |
| ZIP | libarchive, ZIP only (seekable, central directory) | user choice; no Qt private QZipReader, no custom parser, no shelling out, no KArchive |
| Archive security | §7 rules + limits | untrusted input |
| Shared services | GUI and CLI call `RunnerService` / metrics layer | CLI must not own logic |
| CLI exit codes / aliases | §6 | public contract |
| Settings precedence | explicit CLI > persistent setting > compiled default; CLI never rewrites persistence | one central resolver |
| Missing saved runner | missing id → default + repair; present-but-invalid → temporary fallback without repair | do not destroy potentially recoverable preferences |
| Dashboard refresh | Settings throttles presentation to 1/2/5 s on the snapshot's sample time with 50 ms tolerance (F4.1: `sampleTime` from the backend); sampler and runner stay 1 Hz | preserves M1 EMA/sample semantics; without the tolerance 0.9996 s samples skipped a period |
| Generic tray integration (M1–M9/current) | QSystemTrayIcon (generic Qt), no Plasmoid/KF/LayerShellQt in this frontend | M1.0 = B; planned F4 is an additive native frontend, not a replacement |
| Metrics Bar transport | one separate SNI per selected metric; runner item never reused | square slots, no SNI text label (M7 spike) |
| Metrics Bar model/render split | `MetricsBarModel` semantic only; geometry/fonts only in renderer | keeps an M8 Plasma renderer possible |
| Metrics Bar custom value | `metricsBarValue` verbatim; missing/empty → not eligible; never from `formattedValue` | producer owns the short value |
| Metrics Bar overflow/status | fit 13→7 units, else `###`; `…` pending, `—` unavailable, `!` error; never `0` | no silent truncation; tooltip has full value |
| Metrics Bar updates | icon only when rendered content changes; tooltip only when text changes | SNI updates cost in plasmashell (M1) |
| Metrics Bar defaults | disabled (opt-in); items default `cpu`; ids `cpu, memory, swap, network.download, network.upload, storage.root, storage.home, disk.read, disk.write, custom:<canonical path>` | stable persisted ids |
| Metrics Bar limit | max 3 metric items (built-in + custom); runner separate, always present, not counted | user requirement (M7) |
| Explicit quit | `requestApplicationQuit()` = `QCoreApplication::exit(0)` for tray menus and signals | quit() is cancelled by hide-on-close windows (M8 bug) |
| Tray foreground | palette WindowText, or palette Window when the reported colour scheme contradicts the palette; re-tint on colorSchemeChanged | non-KDE Qt themes report scheme without palette (M8); no hard-coded colours |
| Qt minimum | 6.5 (Ubuntu 24.04 not supported, README warning) | user decision M8 |
| SNI identity/order | no per-item Id hacks, no ordering hacks; host-defined | M7/M8 investigation |
| Install layout | CMake install rules only; installer maps staged bin/ share/; manifest-based uninstall; never touches ~/.config/AQTRunCat or ~/.local/share/AQTRunCat | single source of truth, user data safety (M9) |
| Installer autostart | default ON; always through `aqtruncat autostart enable|disable` as the real user | the backend (since F4.1; the app before) re-applies the INI preference; the entry still starts the frontend `aqtruncat` (M9) |
| Backend process (F4.1) | dedicated `aqtruncatd`, single instance by bus name (DoNotQueue, no replacement), D-Bus activation + start fallback next to the client, lives until logout/SIGTERM, not stopped by the last client | user-approved option B; Plasmoid/CLI need it without a tray |
| Public API (F4.1) | `io.github.psykedady.AQTRunCat1` at `/io/github/psykedady/AQTRunCat`, JSON strings, api 1 in the interface name, additive changes raise `schema`; deltas with generation+1, clients resync on gaps | stable contract for third-party clients |
| API safety (F4.1) | no method runs client-supplied commands; integrations take known ids + fixed adapter args; no credentials/provider files/env exposed | security requirement |
| Packages | Arch/RPM/DEB recipes in packaging/, versions checked by ctest `packaging-versions`; Flatpak/AppImage deferred | M9 evaluation |
| Plasmoid form (F4.2) | QML-only Plasma package, no C++ plugin, D-Bus via `org.kde.plasma.workspace.dbus`, async calls only | a native plugin is not found by plasmashell from /usr/local or --local; no native code in plasmashell |
| Plasmoid role (F4.2) | frontend only: no sampling, provider files, commands, scheduler or own settings store; shared settings via SetSetting & co. (immediate), widget options in Plasma's KConfig only | single engine (F4.1); no competing configuration |
| Runner catalogue (F4.2) | `GetRunners`/capability `runners`, schema 1, frames as absolute files + `sequence`, `selected` = tray's start-up fallback; bundled runners also installed as files; QtGui kept out of aqtruncat_api/aqtruncatctl | no frames over IPC; generic for non-Qt clients |
| Plasmoid install (F4.2) | default when Plasma 6 QML modules exist; `--plasmoid` fails without them; `--no-plasmoid` skips/removes; never edit panels, never add an instance; plasmashell restarted **only on request**: after updating an installed widget while this user's plasmashell runs, ask on a terminal "Restart Plasma now …? [y/N]" (default N, no terminal = no), or `--restart-plasma`/`--no-restart-plasma`; restart = `systemctl --user restart plasma-plasmashell.service` (+ reset-failed/start if it is not back; kquitapp6+kstart without the unit), wait ≤ 90 s, else print the commands | user requirement F4.2.1; installing ≠ activating; restart opt-in added by the user on 2026-10-22 (a running plasmashell keeps the QML it loaded: removing/re-adding the widget does not reload it) |
| Plasmoid packaging (F4.2) | inside the main package, weak dependency on plasma-workspace | smallest maintainable; non-KDE users get no Plasma deps |
| F5 metric category | uptime, GPU, FPS, updates, kernel, distribution are **built-in** (`category builtin`, `kind system`), sections of `SystemMetricsSnapshot`; never Custom Metrics/Integrations | user requirement; one engine, one sampler |
| F5 API | schema 2 (additive), api 1; ids `uptime`, `gpu:<PCI slot>` (one per GPU, sorted), `fps`, `updates.packages`, `updates.flatpak`, `kernel`, `distribution`; units `seconds`/`fps`/`count`; kernel/distribution `value` null; GPU object `normalized` null (items carry bars); no dedicated IPC methods | stable ids; multiple GPUs unambiguous; F4.1 contract kept |
| F5 GPU | per-field state; Intel = *activity* from RC6 residency delta (method `rc6-residency`, label "Activity"), not per-engine utilization; amdgpu sysfs; NVIDIA via NVML `dlopen("libnvidia-ml.so.1")` only; integrated Intel: VRAM/temperature unavailable (no CPU-package substitute); runtime-suspended GPU never read; `integrated` null for AMD | no privileges, no subprocess, never wake a dGPU, never invent values |
| F5 GPU selection | setting `gpuDevice` `auto` (first known discrete, else first) or id; resolved in the sampler, published as `data.selected`; absent chosen id stays persisted, auto meanwhile; Metrics Bar `gpu.utilization/memory/temperature` follow it | one GPU per bar slot without client logic |
| F5 FPS | only MangoHud's CSV log (verified in MangoHud source: appended + flushed per sample); `output_folder` from `~/.config/MangoHud/*.conf` (re-read ≤ every 5 s); last complete row; fresh ≤ 3 s, stale ≤ 60 s (no value), else inactive; malformed row = error; setting `fpsSource` mangohud (default) / off; **off = FPS hidden everywhere** (Dashboard row + Gaming header, Metrics Bar item without a slot or placeholder, selection kept; Plasmoid row) via one rule `condition == disabled` (`metrics::fpsHidden`, `logic.js hiddenMetric`); API still reports `fps` as `disabled`; default `FpsMetrics::condition` is Inactive; a source change republishes FPS at once (fix 2026-10-22, still 1.5.6) | no injection, no overlay, never from GPU load/refresh rate; MangoHud control socket has no FPS |
| F5 updates commands | `pacman -Qu` (remote opt-in: `checkupdates`), `apt-get -s -q -o Debug::NoLocking=true dist-upgrade` (no remote: needs root), `dnf -q -C check-update` (remote: `--refresh`), `flatpak remote-ls --user/--system/--installation=N --updates --cached --app/--runtime --columns=ref`; manager by installed capability, os-release only for ties | read-only, no root, local metadata by default, verified no network |
| F5 updates scheduling | `UpdatesService` in the backend: two jobs, ≤1 command each, first check 30 s after start, next `updatesIntervalMinutes` (60; 0 off; 15–1440) after completion, remote ≥ 60 min; debounced (30 s) re-check on package-db/Flatpak `.changed` changes; 120 s timeout, 4 MiB/stream, process-group SIGKILL; LC_ALL=C, stdin /dev/null | infrequent, cached, non-overlapping, bounded |
| F5 update semantics | count only when ok; error keeps `lastSuccess` but no count; metadata > 7 days = stale warning; Flatpak count = distinct refs per installation summed (apps + runtimes, no .Locale/.Debug); any failing installation → error (no partial sum); no Flatpak → unavailable | honest counts |
| F6 hover summary | tray and Plasmoid show the same lines: `CPU: <formatPercent>`, `RAM: <percent> · <formatMemoryDetail>`, `Swap: <percent> · <formatSwapDetail>`; no Swap line when swap is not configured; missing = `waiting for data`/`unavailable`/`error` (never 0, never the backend's message); `Connecting to the backend…` / `Backend not running` replace values (after the client's 3 s grace); Plasmoid title "AQTRunCat", first line the runner | one formatter for API `detail` and tray (`metrics::formatMemoryDetail/formatSwapDetail`); Plasmoid formats nothing itself |
| F6 tray tooltip transport | `TrayPresenter::setSummary`: no send if the text is unchanged; a changed text is set right before the next frame's `setIcon` (250 ms single-shot fallback); runner switch sets it at once; one QString tooltip (Qt/KDE put it in the SNI title, no subtitle via the Qt API) | the KDE theme already sends NewToolTip per frame; F6 adds ≈ 1/s |
| F6 Plasmoid tooltip | the shell's own `ToolTipArea` via `toolTipMainText`/`toolTipSubText` (`Text.PlainText`), no custom window; `root.hovered` = compact `containsMouse || activeFocus` → `wantMetrics`; values only while `BackendClient.metricsCurrent` (connected, wanted, not stale) | no JSON parsing in plasmashell outside hover/popup (F4.2 rule); `GetMetrics` returns the last sample, never triggers one |
| F6 metric details | Metrics Bar `Item::context` = slowly changing lines (part of the tooltip change check); value-tied lines go in `detail` (sent with the icon, M7 rule); only data already in the snapshot; Plasmoid popup row tooltip only when "Detail lines" is off (`logic.js rowToolTip`); Dashboard untouched | no invented data; no extra SNI traffic per value change |
| F5 tests hermetic | `backendipc`, `plasmoidclient`, `plasmoid-preview.sh` write `updates/intervalMinutes=0`; `tst_systemmetrics` injects kernel/osRoot/fps off/nvml null | never depend on host package databases or GPU |

---

## 6. CLI contract (verified against `src/cli/commandline.cpp`)

```text
aqtruncat                                  = aqtruncat run
aqtruncat run [--runner ID] [--fps N] [--verbose]
aqtruncat runner list
aqtruncat runner import FILE.zip [--id ID] [--force]
aqtruncat runner remove ID
aqtruncat metrics [--watch]
aqtruncat autostart enable|disable|status      (M9; used by install.sh)
aqtruncat --help | -h, aqtruncat <command> [sub] --help, aqtruncat --version | -v
```

- Legacy aliases (no deprecation): `aqtruncat --fps N`, `--runner ID`,
  `--verbose` (any args starting with `-` → `run`); `aqtruncat --list-runners`
  (alone) = `runner list`.
- Options: `--name value` or `--name=value`; `--` ends options; repeated,
  unknown, empty-valued options or stray positionals → usage error. `--fps`
  must be an integer 1–120 (diagnostic: fixed rate, CPU ignored).
- Generic Qt options (`-platform`, `-style`) are NOT accepted (parsing happens
  before QApplication) — use environment variables.
- `run` creates `QApplication`; `runner …` creates `QCoreApplication`;
  `metrics` creates neither (keeps C locale).
- `metrics`: samples, waits ~1 s, samples again, prints report; `--watch`
  prints one line/s (`HH:MM:SS  CPU … | Mem … | Net down … up … | Disk read … write …`)
  until SIGINT/SIGTERM → exit 0; returns 1 only if no metric at all is available.
- Help text is hand-written in `commandline.cpp` (short, explains purpose).
- Exit codes (`aqtruncat::cli::ExitCode`, mapping `exitCodeFor(RunnerError)`):

| Code | Meaning | From |
|---|---|---|
| 0 | success | |
| 1 | failure: filesystem/install error, app failure, no metric available | `Filesystem` |
| 2 | invalid command line | parser |
| 3 | not found (runner id or archive file) | `NotFound` |
| 4 | invalid runner/archive/id | `Invalid` |
| 5 | conflict: already installed, bundled protected, ambiguous id | `Conflict` |

Errors → stderr (`aqtruncat: …` + hint line); results → stdout.

---

## 7. Runner format and compatibility

**Upstream facts** (Gallery `b979658`, 43 runners): PNG frames, height 36 px,
width 25–100 px, 5–24 frames, names `<name>-frame-<N>.png` numbered from 0,
37 monochrome / 6 colour (`metadata.json` `type`), ZIP = `<name>-frames.zip`
containing only `<name>-frames/*.png` (no metadata), mixed STORED/DEFLATE.
Gallery has no frame order field. Some assets contain duplicate frames
(cat-tail bakes a ping-pong; dojo-panda frame 11 = frame 0).

**Accepted runner directory** (`loadRunner`, `src/runner/runnerloader.*`):

```text
<id>/*.png [+ metadata.json]                      frames at top level
<id>/<x>/*.png [+ metadata.json at either level]   exactly one sub-dir with frames (Gallery folder + extracted ZIP)
```

- Frame files: `*.png` (case-insensitive), excluding hidden files and
  `preview.png`; `__MACOSX` and dot-dirs skipped; other files ignored.
- Ordering: `naturalLess` — digit runs numeric (no overflow), text
  case-insensitive, ties broken by plain compare → total, **locale-independent**
  (no QCollator). Matches upstream `localizedStandardCompare` intent.
- Fatal (runner invalid): missing dir; no frames; several sub-dirs with frames;
  non-PNG or corrupt PNG (format checked from header, `QImageReader("png")`);
  frames of different sizes; side > 1024 px (checked before decode); > 256 frames.
- Warnings (still valid): numbering gaps/duplicates, size outside 10–100×36,
  single frame, any metadata problem (unreadable, invalid JSON, wrong types,
  unknown `type`, bad `frameOrder` → ignored with defaults).
- `metadata.json` keys: `displayName`, `author`, `type` (`monochrome`|`color`),
  optional `frameOrder` (indices; AQTRunCat extension, RunCat Neo key name).
  Unknown keys (e.g. `tags`) ignored.
- Defaults: name from id (`border-collie` → "Border Collie"); sequence 0..n-1;
  monochrome auto-detected when `type` absent: monochrome if < 3 % of visible
  pixels have chroma (max−min channel) > 24. Calibrated on the Gallery
  (monochrome ≤ 0.67 %, colour ≥ 8.84 %) → 43/43 match.
- Model `Runner`: id, displayName, author, source (Bundled/User), path,
  frames (`QImage` ARGB32_Premultiplied, decoded once), sequence, frameSize
  (any aspect, never scaled/padded), monochrome, warnings.
- Ids created by AQTRunCat: `^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$`
  (`isValidRunnerId`). Directory name → id strips `-frames`
  (`beagle-frames` → `beagle`).

**Locations & precedence**: bundled = Qt resources `:/runners/<id>/` (frames
from `third_party/runcatneo/runners/<id>/`, since F7 per id
`runner_frames_<id>` for `walkinggoose`/`partyparrot`/`rickroll`; metadata from
`data/runners/<id>/`; CMake cache vars
`AQTRUNCAT_BUNDLED_RUNNERS="cat;coffee;walkinggoose;partyparrot;engine;rickroll"` (migrated
from an old cache unless customised),
`AQTRUNCAT_DEFAULT_RUNNER="cat"`, configure fails if default not bundled);
user = `$XDG_DATA_HOME/AQTRunCat/runners/` (default `~/.local/share/...`).
User overrides bundled with same id. Default runner missing/invalid → first
valid (bundled first, then id order) with a warning note; explicit request →
error. Hidden directories in the user root are ignored (used for staging).

**ZIP import** (`src/runner/runnerarchive.*`, `runnerinstaller.*`, `runnerservice.cpp`):

- ZIP only (`archive_read_support_format_zip_seekable`), names via
  `archive_entry_pathname_utf8`; AQTRunCat writes files itself (never
  `archive_write_disk`) into a fresh `QTemporaryDir`, `NewOnly` (duplicates fail).
- Rejected → whole archive invalid: absolute paths, `..`, `.`/empty
  components, backslashes, drive letters, control chars, names > 512 chars;
  symlinks, hardlinks, char/block devices, sockets, any non-regular/non-dir.
  (Verified: libarchive 3.8.9 reports crafted chr/blk/sock modes as such →
  rejected; it maps a FIFO mode to a regular file → harmless.)
- Junk validated then skipped: `__MACOSX/…`, any hidden component.
- Limits `ArchiveLimits` (actual decompressed bytes): archive ≤ 64 MiB,
  ≤ 1024 entries, ≤ 8 MiB per file, ≤ 64 MiB total.
- Layout: PNGs at top level → id from archive name; else exactly one top-level
  dir (and no top-level files) → that dir, id from its name; otherwise root and
  the loader decides (several frame dirs → invalid "ambiguous").
- Pipeline: extract (temp) → `isValidRunnerId` (else suggest `--id`) →
  `loadRunner` → `installRunner`: copy into `<userRoot>/.aqtruncat-install-XXXXXX`
  (same filesystem, hidden), re-validate the copy, `QDir::rename` to
  `<userRoot>/<id>`. Any failure → nothing installed, temp dirs auto-removed.
- Collisions: with bundled → allowed, reported as override; with existing user
  runner (also a `<id>-frames/` dir) → `Conflict` unless `--force`;
  several user dirs for the id → `Conflict`; non-runner path at destination →
  `Conflict`.
- `--force`: new copy staged+validated, old moved to
  `.aqtruncat-replaced-XXXXXX/old`, new renamed in, on failure old restored;
  if restore fails the backup is kept (never deleted); backup removed only on
  success. Known micro-window between the two renames.
- Removal (`removeUserRunner`): user dir renamed to
  `.aqtruncat-removing-XXXXXX` then deleted; bundled-only id → `Conflict`
  ("cannot be removed"); unknown → `NotFound`; ambiguous → `Conflict`,
  nothing removed; removing a user override reports the bundled one is used again.
- Running app keeps frames in memory; removal does not affect it.

**Validation results**: **43/43 real Runner Gallery ZIPs imported
successfully** with the real binary (0 failures, 0 warnings, all listed valid,
0 temp leftovers); import median 4 ms / max 12 ms (extract ≤1, validate ≤6,
install ≤5 ms; ~24 ms on the real home fs). All 43 extracted also load via
directories (M2). Frame order verified through D-Bus `IconPixmap` sampling on
cat, coffee, escapement (24 frames), engine, dojo-panda (colour), cat-tail
(100 px) — 0 wrong steps; Plasma always receives native size.

---

## 8. Metrics semantics (src/metrics — Dashboard must use these)

Common: snapshot `SystemMetricsSnapshot{timeSeconds, cpu, memory, network,
storage[], diskIo}`; each section has `MetricStatus{state, message}` with
`Ok | Pending (rate needs another sample) | Unavailable (source absent here) |
Error (present but unparsable)`. Values only meaningful when `Ok`; absent =
empty `std::optional`, **never 0**. One failing section never affects others.
Raw units only (bytes, bytes/s, percent). Formatting: `metricsformat.h`
(`formatBytes/formatRate/formatPercent`, `UnavailableText = "n/a"`).

Rates (`counterrates.h`, `CounterRateTracker`, shared by network and disk):
`rate = Δbytes / (t_now − t_prev)` with monotonic time. First sample →
Pending; elapsed ≤ 0 / NaN / inf → Pending and **baseline kept**; per-source
counter decrease → that source has no rate this interval (`counterReset`),
new baseline next time; appearing source → no rate until next sample;
disappearing → absent; no counted source present → Unavailable; counted
sources exist but none has a rate → Pending; Δ0 → real 0 (Ok).

| Metric | Source | Raw | Algorithm / filter | Not available | Limitations |
|---|---|---|---|---|---|
| CPU | `/proc/stat` | `usagePercent` (unsmoothed), `logicalCpus` (count of `cpuN` lines) | `core/cpustat` (same code as runner) | first sample/no Δ → Pending; missing file → Unavailable; parse → Error | no per-core values |
| Memory | `/proc/meminfo` (kB=KiB ×1024) | totalBytes, availableBytes, usedBytes, usagePercent, availableIsEstimate | used = total − MemAvailable; fallback avail = MemFree+Buffers+Cached+SReclaimable−Shmem clamped [MemFree, MemTotal] | MemTotal missing/0, MemFree missing, malformed used field → Error | |
| Swap | `/proc/meminfo` | swapPresent, swapTotalBytes, swapUsedBytes, swapUsagePercent | used = SwapTotal − SwapFree | SwapTotal 0/absent → `swapPresent=false` (not an error) | |
| Network | `/proc/net/dev` + `/sys/class/net/<if>/device` | download/upload B/s aggregate, `interfaces[]` {name, kind Loopback/Physical/Virtual, counted, rates, counterReset} | aggregate = Physical only | no physical iface → Unavailable "no physical network interface" | VPN traffic measured encrypted on the NIC; links that exist only as virtual devices without a physical NIC (e.g. `ppp0` modem) are not counted; host↔container bridge traffic not counted; 32-bit wraps treated as resets |
| Storage | `statvfs(path)` (default paths `{"/"}`) | per path: totalBytes, usedBytes, availableBytes, usagePercent | df semantics (§5) | statvfs failure → Unavailable; inconsistent stats (frsize/blocks 0, free>total, avail>free, overflow) → Error | btrfs/CoW approximate (as df); CLI uses `/`, Dashboard adds distinct Home |
| Disk I/O | `/proc/diskstats` (sectors × 512 always) + `/sys/block/<d>/{device,hidden}` | read/write B/s aggregate, `devices[]` | aggregate = PhysicalDisk only | no physical disk → Unavailable "no physical disk" | no NFS/SMB/FUSE; md RAID counts member-disk bytes; eMMC boot partitions count as disks |
| Uptime (F5) | `/proc/uptime` first field | seconds | each sample; `formatDuration` truncates to minutes | missing → Unavailable, malformed → Error | — |
| GPU (F5) | `/sys/class/drm/card*/device` (+ hwmon), NVML dlopen | per device: utilization %, VRAM used/total, °C, MHz, per-field status | Intel: 100·(1−Δrc6/Δt) | no GPU → Unavailable; suspended → fields Unavailable, not read | see §11 item 24 |
| FPS (F5) | newest MangoHud `*.csv` (not `_summary`) in `output_folder` | fps, frametime, condition | last complete row; ≤3 s fresh | stale/inactive/missing/not-configured/disabled → Unavailable; bad log → Error | §11 item 23 |
| Kernel / distribution (F5) | `uname(2)`; os-release → lsb-release → release files | strings | every 600 s | Unavailable, never failure | — |
| Updates (F5) | backend `UpdatesService` (not the sampler) | count, manager, lastSuccess, metadata age | §5 F5 rows | Unavailable (no tool/off), Error (failed), Pending (first check) | §11 item 25 |

Sampling cost: `sample()` ≈ 108 µs (≈50 syscalls: 6 opens, 18 reads,
~14 stats for classification, 1 statfs); classification is redone every
sample on purpose (hot-plug) — caching not needed.

---

## 9. Tests and validation

2.1.4 (2026-10-23, `display.showBar`): clean build in a fresh directory
0 warnings; ctest **53/53**; `packaging-versions` 2.1.4; `git diff --check`
clean (no shell script changed).

2.1.3 (2026-10-23, execution warnings): clean build in a fresh directory
0 warnings; ctest **53/53**; `packaging-versions` 2.1.3; `git diff --check`
clean (no shell script in the repo changed). Isolated runtime: real backend,
command switching fail/69/ok/fail → error/unavailable/ok/error with the
expected messages; log lines failed/recovered/failed only (journald by
default, stderr with QT_FORCE_STDERR_LOGGING=1); stderr beyond the first
line never shown or logged. `aqtruncatctl metrics` text hides the retained
value of a failing source (payload still carries it; state decides).

2.1.2 (2026-10-23, Custom Metrics `enabled`): clean build in a fresh
directory 0 warnings; ctest **53/53**; `packaging-versions` 2.1.2;
`git diff --check` clean (no shell script changed). Isolated runtime: real
`aqtruncatd` on a private bus, a slow command metric disabled/re-enabled
with `gdbus call … SetCustomMetricEnabled`: no process and no new run while
disabled, payload `enabled:false`/`unavailable`/"Disabled."/no items,
re-enabled runs at once, file gains only `"enabled": …`, no leftover
process. Real-session checks (tray Settings checkbox, Plasmoid config) are
left to the user.

2.1.1 (2026-10-23, Rickroll): clean build in a fresh directory 0 warnings;
ctest **53/53** (`loadsInPlasma` 3/3 not skipped); `packaging-versions`
2.1.1; every executable prints 2.1.1; `aqtruncat runner list` shows six
bundled runners; shellcheck and `git diff --check` clean. Isolated runtime:
tray with `runner/id=rickroll` (private bus, fake SNI host, Wayland
platform): 33×36 icon, ~10 fps, F6 tooltip "AQTRunCat — Rickroll";
`tools/plasmoid-preview.sh --runner rickroll` OK; CMake cache migration
from a 2.0.0 tree verified (default list gains `rickroll`, custom kept).
Real Plasma session checks are left to the user.

2.0.0 (2026-10-22): clean build in a fresh directory 0 warnings; ctest
**53/53** (installer included, `loadsInPlasma` 3/3 not skipped);
`packaging-versions` 2.0.0; every executable prints 2.0.0
(`aqtruncatctl` "(API 1)"); `aqtruncat runner list` shows the five bundled
runners; shellcheck and `git diff --check` clean; Markdown links checked
(all resolve). No source behaviour changed from 1.7.4.

F4.1 (2026-10-21): 42 ctest suites (new `apischema` and `backendipc`, the
latter under `dbus-run-session` with isolated XDG: registration/version/single
instance, deltas, classification, settings both ways, shared backend,
frontend exit, backend restart, start fallback, CLI text/JSON/exit codes,
D-Bus activation on a private dbus-daemon); installer 208 checks. Runtime on
Plasma 6 Wayland (isolated XDG, real session bus): see the F4.1 doc §14.
After the 1.4.1 follow-up (same day): installer 217 checks (+ `--no-build`
version mismatch: dry-run, no terminal → build, `y` via `script` pty →
reuse, Enter → build), `metricsbarpresenter` checks the version menu item of
both menus (`TrayPresenter::menu()` accessor), `backendipc` checks
`aqtruncatctl -v/--version`; 42/42. `backendipc` runs under
`dbus-run-session --config-file=<build>/tests/backendipc-bus.conf` (a
session bus WITHOUT service directories): with the default session config
an AQTRunCat installed in /usr/local was D-Bus activated inside the test and
`frontendRecoversFromABackendRestart` failed deterministically.

Automated (40 ctest suites: 38 Qt suites + 2 Bash suites, 0 warnings, verified 2026-10-20 after F3 — new `codexusage` 17 and `codexintegration` 6, installer 198 checks (later in the day: warnings + suite isolation, 40/40 in 5 consecutive rebuild-then-test cycles); before that, after the F2 revision — new `claudeusage` 19 and `claudestatusline` 12, `claudecodeintegration` 12, `claudecodemetric` 28 (now `--context`), custommetric 22, commandmetric 13, dashboard 14, metricsbarmodel 17, appsettings 14, installer 189 checks; earlier: 36 suites verified 2026-10-16 after F2; the per-suite table below predates F1 — F1 added `commandmetric` 10 and `commandmetricsservice` 11, and raised custommetric 16, dashboard 11, metricsbarmodel 16, metricsbarrenderer 23; F2 added `claudecodemetric` 28 and `claudecodeintegration` 6 (QtTest totals incl. data rows/init/cleanup) and +2 each to commandmetric/commandmetricsservice; tests never read the real
`/proc`; fixtures are text strings + temporary `/sys` trees; archives built
with libarchive's writer and re-read to prove hostile names are stored):

| Suite | Cases | Area |
|---|---:|---|
| cpustat | 30 | /proc/stat parse, deltas, nice/iowait/steal/guest, regressions |
| speedcurve | 32 | normal/slow curves, clamp, monotonicity, stabiliser |
| smoothing | 9 | EMA step/flicker/convergence |
| frameplayback | 7 | sequence advance/wrap/replace |
| framenames | 10 | natural ordering, filtering |
| runnerloader | 24 | validation fatal/warnings, layouts, metadata, mono detection |
| runnercatalog | 14 | discovery, precedence, XDG, choose |
| runneranimator | 9 | timing, fps change keeps frame, runner switch |
| runnerarchive | 48 | ZIP valid/malformed/hostile/limits |
| runnerlifecycle | 16 | import/select/remove, override, force, rollback, leftovers |
| cli | 39 | grammar, aliases, help, usage errors, runner commands, exit codes |
| memory | 16 | meminfo, fallback, swap, invalid |
| network | 20 | parse, automatic/manual policy, elapsed time, resets, appear/disappear |
| storage | 11 | df semantics, invalid stats |
| diskio | 11 | parse, device policy, double counting, resets |
| metricsformat | 14 | unit boundaries, rates, percent, locale |
| systemmetrics | 9 | fixtures → snapshot → CLI report/watch line; storage-only success |
| desktopmetrics | 5 | shared snapshot → runner CPU; unavailable/first sample; sampler lifecycle |
| storagepaths | 5 | same/different filesystem identity; lookup failure |
| dashboard | 9 | built-in + custom states/order/scroll, sampler propagation, lifecycle |
| appsettings | 13 | typed persistence/defaults, custom paths, autostart, precedence, Metrics Bar enable/items/limit/legacy normalisation |
| desktopsettings | 7 | speed/network/runtime selection and presentation throttle |
| runnerpreview | 7 | fixed-rate preview, tint/colour, pause and runner replacement |
| settingswindow | 9 | M5 controls/lifecycle, custom add/remove/discovery dedup, Metrics Bar selection/order/3-limit/custom come-and-go |
| custommetric | 15 | upstream fixture/schema, optional/unknown fields, Unicode, dates, limits |
| custommetricsservice | 10 | persistence/discovery/dedup, watch/atomic replace/retry/recovery |
| compactformat | 33 | compact percent/rate boundaries, rounding, saturation, ≤4 chars, locale |
| metricsbarmodel | 15 | ids, order, statuses, home, custom eligibility/verbatim/disappear, coalescing, 3-limit |
| metricsbarrenderer | 22 | content policy, fitting, overflow `###`, caption drop, DPR sizes, palette-only colour |
| metricsbarpresenter | 11 | create/update/remove, no-op suppression, tooltip policy, reorder, icon reuse, actions, palette |
| installer (Bash) | 119 checks | install.sh CLI, distro detection, Qt version parsing, XDG paths, local/system lifecycle, uninstall safety |
| packaging-versions (Bash) | 1 | PKGBUILD/spec/debian changelog/man page versions = CMake |

Manual/runtime evidence already established (don't redo unless changed):

- Plasma/Wayland tray: runner shown and animated; frame delivery = requested
  FPS (D-Bus `NewIcon` rate); light/dark palette re-tint live; tray menu Quit works.
- CPU curve tool covers 0→100→0; `test-cpu-runtime.sh`: idle 11 fps, half
  load ~23–29, full 38–40, recovery back to 11–13 (variance explained by real
  background spikes, verified by sampling `/proc/stat` without the app).
- Gallery: 43/43 directories and ZIPs; lifecycle in the real XDG dir
  (import → list → run → remove; bundled protection; override → remove →
  bundled back).
- RAM/swap match `free -b`; `/` matches `df -B1` (61.6 % vs df 62 % rounded);
  network download 50 MB / upload 10 MB, disk write 256 MiB / direct read
  193 MiB match independent `/proc` counters; CPU 100 % under `yes` load.
- Performance: idle app ≈0.4–0.5 % of a core, ~50 MB RSS (KDE theme libs);
  `metrics --watch` ≈0.05 % CPU, 18 MB RSS.
- M4 Plasma/Wayland: tray Activate and DBusMenu Dashboard fallback verified;
  CPU full load 39–40 fps, recovery 11; fixed curve unchanged. Dashboard hidden
  and visible both measured 0.467 % of one core; visible RSS +5.4 MiB. Root and
  distinct Home, live network/disk and 125% scaling verified visually.
- M6 Plasma/Wayland: isolated XDG discovery rendered a compatible Claude Code
  fixture in the scrollable Dashboard; Settings showed origin/status/path;
  Cat/Coffee cycle and the full 0→100→0 fixed-FPS tool passed. With windows
  closed at fixed 10 FPS, 0/5/20 snapshots measured 0.5/0.5/0.4% of one core,
  59.1/59.2/61.3 MiB RSS and 1/6/21 inotify watches (no reload loop).

---

## 10. Development / diagnostic tools (`tools/`)

All: binary from `--binary`, `$AQTRUNCAT_BIN` or `<repo>/build/src/aqtruncat`;
PID-exact cleanup; Ctrl+C safe (exit 130); `--help`.

| Tool | Verifies | Use when |
|---|---|---|
| `test-cpu-fps-curve.sh [--duration S]` | visual walk of the curve 0→100→0 % (21 steps) via `run --fps N` | changing curve/animation/tray |
| `test-cpu-runtime.sh [--out DIR]` | real load idle → half → full → recovery with `run --verbose`; per-phase raw CPU, applied FPS, changes; NewIcon count of the **runner** item (M7: picked by tooltip "AQTRunCat —" when the Metrics Bar adds more items) | changing sampling/smoothing/stabiliser/CPU pipeline (e.g. M4 shared sampler!) |
| `cycle-runners.sh [--fps N] [--duration S] [ID…]` | shows every valid runner (from `runner list`) in the tray at fixed FPS; honours `XDG_DATA_HOME` | runner/loader/tray changes |
| `test-metrics-runtime.sh [--download-url URL] [--disk-dir DIR] [--disk-mib N]` | `metrics --watch` through baseline → CPU load → optional download → disk write (fsync, temp file removed) → recovery | metrics changes |
| `plasmoid-preview.sh [--runner ID] [--build DIR] [--out DIR]` (F4.2) | offscreen PNGs (Breeze light + dark) of the Plasmoid popup and the runner at panel sizes, real `BackendClient`/`FullRepresentation`/`RunnerView` against the build's backend on a private bus (no service dirs), isolated XDG, demo Custom Metrics + fake Claude/Codex integrations; `QT_SCALE_FACTOR=2` for HiDPI | Plasmoid UI changes, doc screenshots |

Spike-only (M1.0 reproduction, spike binary): `spike/m1-tray/tools/perf.py`
(CPU/RSS of spike, plasmashell, kwin, bus), `dbus_count.py` (SNI traffic).
Build spike with `-DAQTRUNCAT_BUILD_SPIKES=ON`.

M8 testing lessons (keep): `pkill -f PATTERN` also kills the calling shell
whose command line contains PATTERN — use exact PIDs; zsh does not word-split
unquoted `$VAR` (pass list arguments explicitly); `dbus-run-session` can leave
activated portal services (xdg-desktop-portal-kde, ksecretd) bound to the dead
bus — kill those exact PIDs after checking their DBUS_SESSION_BUS_ADDRESS;
`cmd & pid=$!` with `timeout cmd` gives the timeout PID, not the app;
`ptrace_scope=1` allows eu-stack/gdb only on descendants; nested sway in a
container needs `cp /usr/sbin/sway /tmp` (file capability) and leaves
`/run/user/1000/wayland-1*` when killed (remove if no listener); soak scripts
using wall-clock schedules "catch up" after suspend; `spectacle -b -n -a`
captures only the active window (avoid personal desktop content).
M9 lessons: never edit a script while containers execute it from a live
mount (bash reads scripts incrementally → spurious exit 127); run at most
1–2 build containers at once (8 in parallel exhausted 31 GiB and the harness
killed the jobs); killing `docker run` does not stop the container (use
`docker stop`).

Useful validation techniques used before: D-Bus probing of the SNI item
(`busctl --user get-property <unique> /StatusNotifierItem org.kde.StatusNotifierItem IconPixmap|ToolTip`,
`dbus-monitor --profile "type='signal',sender='<unique>',member='NewIcon'"`);
Qt registers the SNI on a second bus connection of the process. Note
`pgrep -f` matches the calling shell's own command line — use `pgrep -x`.
Background job scripts started with `&` from a non-interactive shell ignore
SIGINT (test Ctrl+C through a pty).
F2-revision lessons (2026-10-20): `spectacle -b -n -a` captures whatever
window is active — the user works at the desktop, so first activate the
exact window with a KWin script (`qdbus org.kde.KWin /Scripting
loadScript/run`, match `w.pid` + caption, log `workspace.activeWindow` via
`console.info` → `journalctl --user`), capture immediately, and check the
image size before looking at it; delete unseen otherwise (two captures of
personal windows had to be discarded). Close own windows with a KWin script
(`w.closeWindow()`). DBusMenu clicks: `busctl --user call <unique> /MenuBar
com.canonical.dbusmenu Event isvu <id> clicked i 0 0` (busctl needs `--`
before negative ints). moc silently produces an empty .moc for test files
whose raw string literals contain `)"` (even with a custom delimiter): use
ordinary literals there. zsh does not word-split `$var` (use arrays/python
for arithmetic on captured pairs).
F3 lessons (2026-10-20): a modal QMessageBox opened from an AT-SPI action
does not appear in the AT-SPI tree of the app — check it with a KWin
script listing `w.caption`/`w.modal` for the PID; the user may be watching
and answer dialogs (ask before concluding). `Activate` on the runner SNI
toggles the Dashboard (a second call hides it). The installer test's
`--no-build` "up to date" check failed intermittently right after a CMake
reconfiguration and fixed sleeps raced on a loaded machine: now the test
brings the build up to date first and waits for the instance's exe and its
SIGTERM handler (`SigCgt` bit 15). Never clean leftovers with `pkill -f`
(done once by mistake on an exact "sleep 30" command line): exact PIDs only.

---

## 11. Known issues and limitations (still open)

1. Plasma tray slot is square (~22 logical px): runners wider than ~2:1 are
   tiny/illegible (engine 83 px borderline, cat-tail 100 px poor). Aspect is
   preserved, no clipping. Not solvable generically via SNI; the additive
   Plasma-specific route is now planned as F4, while SNI remains supported.
2. Tray animation cost is in plasmashell+kwin (≈0.42 %/FPS); 40 FPS ≈ 17 % of a
   core desktop-wide; per-frame payload doubled by KSNI's tooltip icon copy.
3. Generic-host tray contrast remains host-dependent when the panel colour
   differs from the app colour scheme (§11 item 19). M8 fixed the previously
   observed all-black dark-theme case with `QStyleHints::colorScheme()`, but
   SNI/XEmbed still cannot expose an arbitrary panel background colour.
4. plasmashell RSS grows ~30 MB at sustained 40 FPS then plateaus (not
   proven leak; long test pending).
5. Background CPU noise on the dev machine (clamd crash loop, browser,
   Teams) makes idle ~19–30 % and causes real FPS excursions in runtime tests.
6. Network: hardware-only policy → a connection that exists only as a virtual
   link with no physical NIC (e.g. legacy `ppp0` modem) is not counted
   (Unavailable if nothing physical exists); WWAN modems exposed as `wwan0`
   have a device link and ARE counted.
7. `--force` replace: microsecond window between the two renames where the id
   is absent.
8. Generic Qt command-line options not accepted (use env vars).
9. `aqtruncat metrics` takes ~1 s (two samples).
10. CLI storage shows only `/`; the Dashboard shows Root plus Home when their
    `st_dev` differs. Arbitrary filesystem selection is not implemented.
11. Gallery ZIPs carry no metadata → imported name/mode are derived (e.g.
    `--id cat` gives display name "Cat").
12. Runtime runner switching is implemented (`TrayPresenter::setRunner`,
    `RunnerAnimator::setSequence`) and is exposed by the M5 Settings window.
13. RunCat Neo custom runner folders (`<UUID>/frame-N.png`) load as frames
    only; names/isTemplate from `CUSTOM_RUNNERS.json` not imported.
14. No machine-readable output (`--json`) yet.
15. Wayland does not allow reliable client-side anchoring to the tray; the
    Dashboard uses standard KWin placement rather than X11/global-coordinate
    hacks. `Qt::Tool` task-switcher treatment remains compositor policy.
16. Metrics Bar: all AQTRunCat SNIs share `Id = Title = AQTRunCat`, so
    Plasma's per-item visibility settings apply to runner and bar together.
17. Metrics Bar order on the panel is best effort (async per-connection SNI
    registration; one misorder seen in the first run of the day).
18. Every distinct QIcon sent to a QSystemTrayIcon leaves a pixmap in Qt's
    global QPixmapCache (LRU 10 MiB): bar RSS can grow up to ~+12 MiB, then
    stays flat (proved with 12 000 icons). Not a leak; presenter reuses
    QIcons for identical content (QCache 128). Creating/destroying an SNI
    leaves ~8 KB in the Qt/KDE stack (plain Qt too).
19. Tray contrast on hosts whose panel colour differs from the app colour
    scheme (GNOME top bar always dark, waybar coloured tray, stalonetray dark
    without scheme) depends on the host: SNI/XEmbed do not expose the panel
    colour. A manual light/dark tray-colour preference would be a new feature
    (open decision for the user).
20. After a StatusNotifierWatcher restart the panel order of AQTRunCat items
    may change (parallel re-registration); restored by restarting the app.
21. With the generic Qt platform theme the runner is letterboxed to a square
    and the bar text uses the generic palette (M1 behaviour, unchanged).
22. Claude Code quotas without live updates change only when `/usage` (or an
    IDE usage view) is opened; validated 2026-10-20: 3% "as of 10:28" while
    `/usage` at 11:44 showed 31%. Live updates need Pro/Max, the user-level
    status line (project/managed status lines take precedence) and a
    Claude response. `cachedUsageUtilization` is an internal format.
23. F5 FPS exists only while MangoHud logs to a file; MANGOHUD_CONFIG env
    and Flatpak MangoHud (sandbox paths) are invisible; newest log wins.
24. F5 Intel GPUs: activity from RC6 residency, not per-engine utilization;
    no iGPU temperature/VRAM; AMD integrated/discrete unknown; only gt0 read.
25. F5 updates: counts as fresh as local metadata (stale note after 7
    days); apt cannot refresh without root; dnf --refresh writes the user's
    cache; one failing Flatpak installation blocks the Flatpak count.
26. F5 validated on real hardware only for Intel i915; AMD/xe/NVIDIA/NVML/
    nouveau, apt/dnf desktops and a real MangoHud game are fixture/
    container-tested or left for manual checks.

---

## 12. Deferred decisions

M8 and M9 are complete (docs/desktop-compatibility.md,
docs/installation-and-packaging.md). Items deferred there and not covered by
F1–F6 (backlog notes, not scheduled): Flatpak and AppImage *application
packaging* (distinct from the F5 Flatpak-updates metric; portal-based
autostart, sandbox paths for Custom Metrics/storage, bundled-Qt theme
integration), manual light/dark tray colour preference for generic hosts
(M8), AppStream metainfo (needs a stable app ID/homepage), machine-readable
CLI output (`--json`), arbitrary filesystem selection for storage, importing
RunCat Neo custom runner names (§11).

---

## 13. Feature history F1–F7 (1.1.0 – 1.7.4, shipped in 2.0.0)

All complete and committed by the user (F7 = 869743a on `feature/f7`, then
`feature/main`). Normative summaries: `docs/feature-reference.md`; records:
the topic documents in `docs/README.md`. The notes below keep the
implementation decisions and lessons of each feature; "uncommitted at that
time" refers to the state when the feature was finished.

- **F1 — Custom Metrics v2 / Executable Custom Metrics — COMPLETE
  (2026-10-13)**: extends, and does not replace, M6 with two source types:
  - **snapshot source**: the existing externally updated, RunCat Neo-compatible
    JSON snapshots, with current watch/retry/last-known-good compatibility;
  - **command source**: AQTRunCat periodically runs an explicitly configured
    `executable + arguments[]`, without an implicit shell, and derives the
    metric from bounded program output.
  Implemented in `custommetrics::CommandDefinition` and the existing
  `CustomMetricsService`: required `id`/`title`, explicit
  `source.type=command`, executable + string arguments, interval default/min
  1 s (max 24 h), timeout default 10 s (1–300 s), optional display prefix,
  suffix and numeric normalized range. One shared monotonic timer schedules
  jobs independently from CPU sampling; each next interval begins after the
  prior completion, so there is no overlap, catch-up or idle busy polling.
  Direct `QProcess` only: no shell, sudo/pkexec or privilege change. The
  process CWD, relative executable and relative file icon all resolve from the
  config directory. Stdout/stderr are bounded independently to 16 KiB;
  stdout is one trimmed nonempty UTF-8 scalar line and becomes both
  `formattedValue` and `metricsBarValue` after prefix/suffix. Missing or
  non-executable is Unavailable; start/crash/non-zero/timeout/parse is Error;
  Pending retains a previous valid snapshot. Stop terminates, then kills with
  bounded waits. F1 adds package discovery `<metrics>/<dir>/metric.json`,
  structured theme/file icons with fallback, and preserves standalone M6
  discovery, registration, watcher/retry/last-good and legacy symbol JSON.
  No structured multi-value command output or GUI editor: the snapshot format
  remains the multi-value path. Full contract: `docs/custom-metrics-commands.md`.
  **2026-10-16 audit/hardening** (against the original F1 prompt): fixed a
  zero-delay wake spin after a timeout kill; commands now run in their own
  process group so timeout/overflow/stop also kill script helpers; package
  subdirectories are watched so `mkdir pkg; cp metric.json pkg/` is noticed;
  snapshot and command parsers share `custommetrics::parseSymbol`; Dashboard
  defers custom-card rebuilds while hidden; Settings keeps the custom-row
  selection. Runtime on Plasma 6 Wayland (isolated XDG, `--fps 10`): no
  command 0.40 % / 60.9 MiB; 1 command@1 s 0.63 %; 5 commands@1–5 s 0.87 %;
  timeout script with `sleep 30 &` 0.50 % and no leftover; 2.5 s run with
  1 s interval → starts every 3.51 s; SIGTERM exit 0 in all cases. Visual:
  M6 legacy snapshot, relative SVG file icon, theme icon, missing-icon
  fallback, normalizedRange bar; bar SNI pixmap shows icon over value.
  **Refresh presentation (2026-10-16, final F1 adjustment):** a running
  refresh keeps the source internally Pending (service/scheduler unchanged),
  but once a valid value exists the Dashboard card keeps presenting the last
  settled state (Available → "Last updated", or Error/Unavailable with its
  message) instead of flickering to "Waiting…"; only a first execution (or
  a redefinition back to a skeleton without items) shows "Waiting…". The
  settled state is tracked in `DashboardWindow::updateCustomMetrics`, also
  while hidden. Metrics Bar unchanged (it already shows the retained custom
  value while Pending).
- **F2 — Claude Code Metric — COMPLETE (2026-10-16, `feature/f2`), REVISED
  2026-10-20**. Full record: `docs/integration-claude-code.md` (Part A =
  revision/current behaviour, Part B = first implementation, kept as
  history); normative summary in docs/feature-reference.md. The M6 Claude JSON example
  is still only an example snapshot, not how F2 works.
  - Architecture (unchanged): `src/providers/claude/` → static lib
    `aqtruncat_claude` (QtCore only: `claudecode.*` context + collect,
    `claudeusage.*` quotas/cache/live state, `statuslinebridge.*`
    settings.json editing) + executable `aqtruncat-claude-metric` (`main.cpp`:
    `snapshot [--context [--cwd DIR]] [--config-dir DIR]`, `enable [same]
    [--force]`, `disable`, `status [--json]`, `live-updates on|off`, internal
    `statusline`). Core/`aqtruncat_desktop` never link it. AQTRunCat runs it
    as an ordinary F1 command (`output: "snapshot"`, 10 s, timeout 5 s).
  - Revision request (user, 2026-10-20): official integration in Settings;
    primary values = the two `/usage` quotas with reset times, each with its
    own Dashboard bar; context window only optional; investigate sources
    without inventing values; installer must close the running instance.
  - Sources (investigated 2026-10-20, Claude Code 2.1.304):
    `~/.claude.json` (`$CLAUDE_CONFIG_DIR/.claude.json` if set)
    `cachedUsageUtilization` {fetchedAtMs, accountUuid, utilization.five_hour/
    seven_day {utilization %, resets_at ISO with 6-digit fractions}} = exactly
    what `/usage` fetched; refreshed only when `/usage`/IDE usage view opens
    (backups in `~/.claude/backups/` proved it). Status-line stdin
    `rate_limits.five_hour/seven_day.{used_percentage, resets_at epoch s}`
    (documented, per response, Pro/Max). Rejected: OAuth usage endpoint
    (credentials + network), `claude -p` (tokens), screen scraping, token
    stats. **User decision: hybrid** — cache by default, opt-in live
    updates, newest report per window wins.
  - Values: items `five_hour` "Current session (5h)" and `seven_day`
    "Current week (all models)": "31% used" (+ " — limit reached" ≥100),
    normalized %/100, detail "Resets at 13:30" / "Resets Oct 23, 20:00"
    (local TZ, 24 h, English month) + " · as of HH:MM" when >5 min old;
    bar value "31%"/"<1%", caption "5h"/"7d"; item `source` "Usage data";
    with `--context` the old context items (`context` with bar value and
    caption `ctx`, Model, Session, Running sessions). Expired (past reset) /
    >24 h → "Out of date" + reason (was "Unknown" until 2026-10-20), both
    `warning: true`; missing → "Not reported" + hint (warning). No report at
    all → 69; Claude absent → 69; unreadable/unsupported cache → 1 (retry
    once after 100 ms). Cache of another account / not signed in ignored.
  - Live updates: see §5 rows; status-line helper writes only the two
    windows (+ capture time per window, config dir) 0600; input without
    rate_limits never clears stored values; chains the previous status line
    with identical stdin; never fails.
  - Settings › Integrations + `ProviderIntegration` (§3); CLI → GUI sync via
    file watches; GUI live-updates asks for confirmation; `disable` also
    turns live updates off.
  - Installer: §5 row "Installer running instance"; `--help`, README and
    `docs/installation-and-packaging.md` note updated.
  - Validation 2026-10-20 (Plasma 6 Wayland, isolated XDG, `--fps 10`, real
    Claude data read-only): `/usage` at 11:44 = 31% / 11% (resets 1:30pm
    Rome) ↔ adapter 11:45 = 31% "Resets at 13:30" / 11% "Resets Oct 23,
    20:00"; Dashboard two bars + "as of 10:28" earlier; Metrics Bar SNI
    pixmaps `5h 3%`, `7d 7%` + tooltips; Settings Integrations rendered and
    followed a CLI `enable --context`; app 0.50 % + adapter ~0.1 % of a core
    (≈11 ms CPU per run); SIGTERM clean, no adapter left. Not done live:
    live updates on the user's real settings.json (no consent asked; full
    path covered by tests with the real executable).
  - Dashboard refinement (2026-10-20, F2 doc §A.13): provenance field and
    Integrations/Custom Metrics sections (§5 row "Integration provenance");
    validated on Plasma with a provider card, a same-titled legacy M6
    snapshot and a user command (`docs/integration-claude-code/
    dashboard-sections.png`). The user's real
    `~/.local/share/AQTRunCat/metrics/claude-code/` was written by the old
    adapter (no marker): it showed under Custom Metrics after the user's
    reinstall (Settings migration only runs when Settings opens). Fix:
    adapter `upgrade` (rewrites an enabled own package with its options,
    refreshes live-updates path; no-op otherwise) run by `install.sh` as the
    real user after autostart; warning on failure. Deleting the folder was
    rejected (installer never touches user data; would silently disable the
    integration and orphan a live status line).
  - Limitations: see §11 item 22 and the F2 doc §A.10.
- **F3 — Codex Integration — COMPLETE (2026-10-20, branch managed by the
  user; uncommitted at that time)**. Full record: `docs/integration-codex.md`;
  normative summary in docs/feature-reference.md (F3 "Implemented Behaviour").
  - Investigation (Codex CLI 0.163.0, ChatGPT Plus): `~/.codex/sessions/
    YYYY/MM/DD/rollout-*.jsonl` `event_msg`/`token_count` carry
    `rate_limits` {limit_id ("codex"; also "premium" with null windows;
    null in old versions), primary/secondary {used_percent, window_minutes,
    resets_at epoch s; old: resets_in_seconds}, plan_type,
    rate_limit_reached_type}; written only while Codex works (user's last
    data 13/10 → both windows already reset). `codex app-server` (stdio
    JSON-RPC, experimental, schema via `codex app-server
    generate-json-schema`) `initialize` → `initialized` →
    `account/rateLimits/read` {excludeResetCreditDetails:true} returns
    rateLimits / rateLimitsByLimitId["codex"] {primary/secondary
    {usedPercent, windowDurationMins, resetsAt}, planType,
    rateLimitReachedType}, ordinaryUsageAllowed, accountId; ~0.8 s wall,
    ~0.34 s CPU, Codex uses its own login, no tokens. In 1 620 events
    "primary" was the 7-day window (weekly-only plan) → labels/ids from the
    window length, never the position. Rejected: sqlite DBs (threads/logs),
    /status scraping, `codex exec`, auth.json + endpoint.
  - User decisions: hybrid (local by default, opt-in live every 5 min); a
    few real live reads allowed for validation.
  - Architecture: `src/providers/common/` (`aqtruncat_provider_common`:
    `quota.*` judgement/item format moved out of claudeusage/claudecode with
    unchanged Claude output, `package.*` helpers now also used by the Claude
    main); `src/providers/codex/` (`aqtruncat_codex`: `codexusage.*`;
    `aqtruncat-codex-metric`: snapshot [--live] [--codex-home], enable
    [--live] [--codex-home] [--force], disable, upgrade, status [--json],
    live-updates on|off; `codexicon.h` original green bracket-gauge icon).
    Package `metrics/codex/`, integration "codex", interval 15 s, timeout
    30 s. Live: QProcess in its own process group (setpgid), stop = close
    stdin, then group TERM/KILL; refresh 300 s, retry 60 s; state file
    `$XDG_RUNTIME_DIR/aqtruncat/codex-rate-limits.json` (0600, no account
    id, bound to the Codex home). Fresh 10 min, max age 24 h. Items
    `window_<m>m`, titles "5h limit"/"Weekly limit"/…, captions `cx5h`/
    `cx7d`, detail "N% left · Resets …".
  - App: `SettingsWindow` takes `codex` (6th ctor arg); Codex panel with
    confirmation dialog for live updates; shared
    `integrationAvailability()` for both panels; `trayapp.cpp` creates both
    ProviderIntegrations lazily. No change to Dashboard/Metrics Bar code
    (F2 provenance and item ids suffice).
  - Installer/packaging: CMake installs the adapter (install.sh, --local,
    PKGBUILD, Debian automatically); RPM %files; install.sh runs `upgrade`
    for both adapters; help text updated.
  - Validation (Plasma 6 Wayland, isolated XDG, a11y on): GUI activation of
    both integrations via AT-SPI; the live confirmation dialog was answered
    by the user on screen; live 0% (5h, resets 23:21) / 1% (weekly, Oct 27
    10:08), user compared with Codex `/status` and judged them matching (no
    transcribed values); Dashboard both cards under Integrations; SNI
    `cx5h 0%`, `cx7d 1%`, `5h —`; 163 AT-SPI samples over 45 s: no
    "Waiting…", stable values; app 0.45 % + adapters 0.22 % of a core;
    clean SIGTERM, no leftover; a live read verified not to modify
    config.toml/auth.json/top-level json files (Codex's own sqlite logs not
    checked).
  - Limitations: local data only while using Codex; experimental app-server
    protocol; accounts switched on the same CODEX_HOME (local data has no
    account); no Codex context-window option.
- **F4.1 — Headless Backend, Local API & CLI — COMPLETE (2026-10-21,
  uncommitted at that time)**. Full record and API contract:
  `docs/backend-dbus-api-cli.md` (+ Python Gio client example in
  `docs/backend-dbus-api-cli/aqtruncat-client.py`); normative
  summary in docs/feature-reference.md (F4.1 "Implemented Behaviour").
  - Decision: option B (dedicated `aqtruncatd`) over A (`aqtruncat
    --headless`): backend links only Qt6Core/Qt6DBus (~17 MB RSS vs ~60 MB
    with the GUI stack), clean activation `Exec`, clear ownership.
  - API: methods GetInfo/GetState/GetMetrics/GetRunner/GetSettings/
    GetIntegrations, SetSetting(key, jsonValue; bare word = string),
    Register/UnregisterCustomMetric, SetIntegrationEnabled/LiveUpdates
    (delayed reply); signals MetricsChanged(t, s{schema, generation,
    sampleTime, metrics:[changed], removed, order}), RunnerChanged, SettingsChanged,
    IntegrationsChanged. Errors …AQTRunCat1.Error.InvalidArgument/NotFound/
    Failed. Metric fields: id, category builtin|integration|custom, kind
    system|snapshot|command, integration, title, state, message, value (null,
    never invented 0), unit, formattedValue, detail, compactValue,
    compactLabel, normalized, updatedAt (null for system), icon, items[]
    (with warning), data (raw values / source info).
  - Client behaviour (BackendClient, CLI, Python example): GetState with
    autostart, apply deltas only for generation+1, else resync; watch the
    name owner; frontend 3 s grace then "the AQTRunCat backend (aqtruncatd)
    is not running", retry 3 s, start fallback at most every 10 s.
  - Settings: same INI; backend watches file+directory, reloads after 100 ms
    (`AppSettings::reload()` emits only changed values) → SettingsChanged →
    frontend `settings.reload()`.
  - CLI exit codes 0/1/2/3/4 (ok/backend failure/usage/unavailable/
    rejected); human output hides `custom:` ids.
  - Install: `data/io.github.psykedady.AQTRunCat.service.in` →
    `share/dbus-1/services`; install.sh sets Exec to `$bindir/aqtruncatd`,
    stops this installation's aqtruncat AND aqtruncatd (exact exe path),
    `ReloadConfig` on the user's bus after install/uninstall; PKGBUILD dbus,
    Debian default-dbus-session-bus | dbus-session-bus, RPM %files +
    Requires dbus.
  - Validation (Plasma 6 Wayland, isolated XDG): headless backend, CLI,
    tray connects to the same backend (one aqtruncatd), Metrics Bar enabled
    via CLI reached the tray (SNI tooltips incl. Claude/Codex items), tray
    animation 11 NewIcon/s at targetFps 11, two `watch --json` clients got
    identical generations/payloads, backend SIGTERM → tray restarted it via
    fallback (inherits the tray's environment), frontend exit left the
    backend running, adapters only as backend children. Perf: backend alone
    0.07–0.33 % / 17 MB, backend+tray 0.07+0.47 % / 17+61 MB (F3 single
    process 0.47 % / 84 MB), CLI one-shot 5–6 ms, 52 bus messages in 30 s.
    Dashboard read via AT-SPI (`gi.repository.Atspi`, no screenshot; open/
    close with SNI `Activate` on the runner item): same values as the CLI,
    cadence 1 s / 2 s. Two defects found and fixed: rebuilt snapshots had
    timeSeconds 0 so SnapshotThrottle passed only the first (CPU/network/
    disk stuck on "Waiting…"; the IPC test had bypassed the throttle) → the
    backend now publishes `sampleTime`; and the throttle's strict `>=`
    skipped 0.9996 s samples (pre-existing since M5) → 50 ms tolerance.
  - Limitations: dbus-broker needs ReloadConfig (installer asks) or the
    fallback; backend lives until logout; Qt Settings › Integrations still
    drives adapters directly (backend observes the adapter files);
    updatedAt null for system metrics; native packages not rebuilt in
    containers.
- **F4.2 — Native KDE Plasma 6 Plasmoid — COMPLETE (2026-10-21,
  uncommitted at that time)**. Full record: `docs/plasma-widget.md`
  (screenshots `docs/plasma-widget/*.png`, offscreen previews);
  normative summary in docs/feature-reference.md (F4.2 "Implemented Behaviour").
  Original intent kept: additive, parallel to the Qt/SNI frontend, a
  client of the F4.1 API, no duplicated business logic.
  - Decision: **QML-only Plasma package** (`plasmoid/package`, id
    `io.github.psykedady.aqtruncat`) over a C++ QML/applet plugin: plasmashell
    searches only Qt's own plugin/QML dirs, so a native part would not load
    from `/usr/local` or `--local`; QML also keeps native code out of
    plasmashell and needs no build dependency. D-Bus from QML through
    plasma-workspace's `org.kde.plasma.workspace.dbus` (verified API:
    `SessionBus.asyncCall({service,path,iface,member,arguments,signature})`
    → reply with `isError/error.name/error.message/values` (values wrapped
    as `{value: …}`); `SignalWatcher` calls `dbus<Signal>(args)` functions
    on itself (uint64 arrives as `{value: n}`); `DBusServiceWatcher`
    `registered`). Arguments typed with `new DBus.string(…)` /
    `new DBus.bool(…)` and a signature like `"(ss)"`.
  - API extension: `GetRunners` + capability `runners` (see §3, §5). Bundled
    runners also installed as files (`share/aqtruncat/runners/<id>/`, the
    same unmodified PNGs + metadata; build tree copy in
    `build/share/aqtruncat/runners`); `aqtruncatd` finds them at
    `<bindir>/../share/aqtruncat/runners`, else XDG data dirs.
  - Rendering findings: `Kirigami.Icon` does NOT tint PNG files with
    `isMask` and rounds to icon sizes (`roundToIconSize`); `MultiEffect`
    renders nothing without GPU (offscreen/software). Used: one `Canvas`
    per frame, `drawImage` + `globalCompositeOperation = "source-in"` fill
    (same as the tray), painted in device pixels (HiDPI ok at 2×), repaint
    on size/colour/`Screen.devicePixelRatio`; the timer only toggles
    visibility.
  - QML pitfalls met: a component property bound to an id with the same
    name (`client: client`) binds to itself (ids renamed: `backend`);
    `scale`/`action` are existing (FINAL) properties; moc cannot parse raw
    string literals with `)"` inside (use escaped strings in Q_OBJECT
    files); an async reply can arrive after the item is destroyed (guard
    `client` null); a `ScrollView` + wrapping delegates gave a
    `rightPadding` binding loop only on the real compositor (fixed with
    `contentWidth: availableWidth - margins`, the Plasma applets' pattern);
    ctest's `plasmoidviewer` needs `-c org.kde.panel` to create the compact
    representation (plain plasmoidviewer shows the full one).
  - Install: `install.sh` decides `plasmoid` (dry-run line `plasmoid = …`):
    Plasma 6 = every QML module the widget imports exists (`qmldir` under
    QT_INSTALL_QML or usual dirs; test override `AQTRUNCAT_QML_DIRS`) and
    `org/kde/plasma/plasmoid/kde-qmlmodule.version` is 6.x. Staged files
    filtered (works with `--no-build`); `--plasmoid` without Plasma → exit 1
    with missing modules + distro package command; `--no-plasmoid` removes a
    previously installed widget (stale manifest entries); empty owned dirs
    removed (`remove_owned_dirs`: doc/<name>, share/aqtruncat,
    plasmoids/<id>); warning when a system datadir is not in XDG_DATA_DIRS;
    never calls plasmashell/kpackagetool/kquitapp (test fakes record calls).
    CMake option `AQTRUNCAT_INSTALL_PLASMOID` (ON).
  - Packages: same package; PKGBUILD optdepends plasma-workspace, RPM and
    Debian `Suggests: plasma-workspace`; RPM %files gained
    `%{_datadir}/%{name}/` and the plasmoid dir.
  - Validation: offscreen previews (Breeze light/dark via
    `QT_QPA_PLATFORMTHEME=kde` + kdeglobals, scale 1 and 2);
    `plasmoidviewer` on the real Wayland compositor with a private bus:
    loads in horizontal/vertical panel containment and desktop with zero
    QML messages; startup = 1 GetState + 1 GetRunners; one backend with two
    widgets + tray; MetricsChanged 10–12 / 10 s regardless of clients;
    runner change from CLI → one GetRunners per widget; backend restart →
    widgets and tray retry and reconnect. CPU: viewer 0.23–0.30 %, backend
    0.07–0.10 %, tray (no SNI host) 0.07 %. Not done (user's session not
    touched): real panel add, popup by click, CPU inside plasmashell.
  - Limitations: activation only (QML cannot start processes); no
    translations; `symbol` icons not shown; tray Settings window not
    reachable from the widget; no runner-list change signal; auto-hide
    panels keep animating; `org.kde.plasma.workspace.dbus` is a
    plasma-workspace (not Frameworks) module.
- **F5 — Additional Built-in Metrics — COMPLETE (2026-10-22, 1.5.6,
  then uncommitted on `feature/f5`)**. Full record: `docs/additional-built-in-metrics.md`
  (+ `docs/additional-built-in-metrics/{dashboard,plasmoid-light,plasmoid-dark}.png`);
  normative summary in docs/feature-reference.md (F5 "Implemented Behaviour").
  - User prompt (2026-10-22): seven metrics as Built-in Metrics in the
    existing architecture, one backend, explicit states, no invented
    values, no root, no installs, no silent network, optional FPS only from
    a genuine source; settings only where needed; Metrics Bar opt-in.
  - Investigation: host = Intel i915 TigerLake only (no AMD/NVIDIA),
    pacman + checkupdates + flatpak installed, no MangoHud. MangoHud
    sources read (logging.cpp: per-sample append+flush; control.cpp: no
    FPS). `flatpak remote-ls --cached` verified offline with strace;
    `apt-get -s` (Ubuntu 25.04) and `dnf -C check-upgrade` (Fedora 44, uid
    1500) verified as non-root in containers.
  - Code: `src/metrics/{uptime,systeminfo,gpu,fps,packageupdates}`,
    `src/app/{boundedprocess,updatesservice,processgroup.h}`; snapshot,
    sampler, apischema (both ways), backend (UpdatesService, settings),
    BackendClient (unavailable on loss), MetricsBarModel, Dashboard
    (GPU/Gaming/Updates/System), Settings (Built-in Metrics group),
    trayapp (GPU list), `aqtruncat metrics` report, `aqtruncatctl` help,
    Plasmoid (section "Built-in Metrics", shared settings, logic.js
    gpuChoices/updatesIntervalChoices/indexOfValue).
  - Settings (INI / API): `gpu/device` `gpuDevice`, `fps/source`
    `fpsSource`, `updates/intervalMinutes` `updatesIntervalMinutes`,
    `updates/remoteRefresh` `updatesRemoteRefresh`; signal
    `AppSettings::systemMetricsSettingsChanged`; Restore Defaults resets
    them.
  - Performance (60 s, private bus): backend F4 0.100 % / 21.0 MiB → F5
    0.150 % / 21.8 MiB; one check round ≈ 1.8 s CPU in children (pacman
    -Qu with gpg ≈ 0.7 s, 4 flatpak calls ≈ 1 s) → ≈ 0.05 % hourly.
  - Limitations: see §11 items 23–26 and the F5 doc §17.
- **F7 — Additional Built-in Animated Runners — COMPLETE (2026-10-22,
  1.7.4; committed by the user)**: record
  `docs/built-in-runners.md`.
  - F7.1 Walking Goose (`walkinggoose`, 6 frames 24×36) → 1.7.1, F7.2 Party
    Parrot (`partyparrot`, 10 frames 50×36) → 1.7.2, F7.3 shared
    integration/packaging/validation → 1.7.3 (the user's F7 brief; it
    replaced the earlier documentation numbering, renamed "Crazy Dancing
    Parrot" to "Party Parrot", chose ids `walkinggoose`/`partyparrot`
    instead of the proposed `goose`/`parrot`, and excluded Engine).
  - Assets (user-approved 2026-10-22, not modified by F7):
    `third_party/walkinggoose/runners/goose/goose-frame-{0..5}.png`,
    `third_party/partyparrot/runners/parrot/parrot-frame-{0..9}.png`;
    monochrome outlines derived from Tenor GIFs (author not verifiable,
    licence unknown, included by the user's decision; provenance, GIF
    SHA-256 and exact processing in each folder's README.md and
    THIRD_PARTY_NOTICES.md; reusable ImageMagick pipeline for the user:
    `~/Codici/bash/gif2runner.sh`, outside the repo). Goose processing
    detail: opaque white background/body, the dark brown (49,43,37) both
    edges lines and fills the shadow → opening to remove thick shadow,
    orange beak/feet solid, specks < 150 px below y 160 dropped (rules
    "contains black" and "size only" failed: shadow has black pixels; the
    eye is small).
  - Integration: only `src/CMakeLists.txt` + metadata. One list
    `AQTRUNCAT_BUNDLED_RUNNERS` = `cat;coffee;walkinggoose;partyparrot;engine`;
    per-id frames dir `runner_frames_<id>` (F7 folders), fallback
    `third_party/runcatneo/runners/<id>`; one `qt_add_resources` per runner
    (PREFIX `/runners/<id>`), install + build-tree copy to
    `share/aqtruncat/runners/<id>/`; the installed dir name is the id.
    Cache migration: `AQTRUNCAT_BUNDLED_RUNNERS_DEFAULT` (INTERNAL) records
    the default; a cached value equal to the previous default (or
    `cat;coffee` when unrecorded) follows the new default, a customised
    list is kept. `data/runners/{walkinggoose,partyparrot}/metadata.json`
    (type monochrome, author "Unknown (…via Tenor)").
  - No change to backend/API/`GetRunners`, tray, Plasmoid QML, animator,
    sampling; speed follows CPU (GIF timings not reproduced, by design).
  - Packaging: share/aqtruncat already covered everywhere; Debian
    `copyright` stanza for the two folders "License: unknown".
  - Tests: new `bundledrunners` (real installed files vs sources, list,
    selection, persistence, Restore Defaults, missing/broken fallbacks);
    `backendipc` GetRunners, `plasmoidclient` selection, `plasmoidlogic`
    `test_f7RunnersToShow`, installer checks. ctest 53/53, clean build 0
    warnings.
  - Runtime (isolated): tray with saved runnerId each runner + fake SNI
    host (≈10 fps, F6 tooltip names the runner); plasmoid-preview
    `--runner walkinggoose|partyparrot` (panels 22/34/48 h, 34/48 v).
  - F7.4 Engine (`engine`, 1.7.4; the F7 brief had excluded it, then the
    user asked "metti anche engine"): RunCat Neo frames unmodified via the
    RunCat Neo fallback folder, `data/runners/engine/metadata.json`
    (Kyome22), THIRD_PARTY_NOTICES updated (no longer spike-only), tests
    extended; cache migration updated the existing build by itself.
    Accepted limit: ≈ 22×10 px in Plasma's square tray slot, ≈ 34×15 px in
    a 34 px vertical panel.
  - Manual checks left: real tray/Plasmoid with the new runners (a
    Plasmoid only needs the 1.7.4 backend installed; its QML is unchanged).
  - Next: 2.0.0 is a separate milestone after the user accepts F7; do not
    start it without an instruction.
- **F6 — Hover Information — COMPLETE (2026-10-22, 1.6.3, then uncommitted on
  `feature/f6`)**: record `docs/hover-information.md`.
  - F6.1 tray: `hover::summaryLines`/`runnerToolTip` (`src/app/
    hoversummary.*`, engine lib, QtCore) from the snapshot `BackendClient`
    already emits + `connectionChanged`; `TrayPresenter::setSummary`.
  - F6.2 Plasmoid: `main.qml` `toolTipSubText: Logic.hoverText(status,
    runner, metrics, metricsCurrent)`, `toolTipTextFormat: Text.PlainText`;
    `CompactRepresentation` Binding → `root.hovered`; `BackendClient.
    metricsCurrent`; `logic.js` `hoverLines`/`hoverStateText`/`hoverText`.
  - F6.3: Metrics Bar `Item::context` + multi-line `detail` (memory
    available, storage used/total); popup `MetricDelegate` HoverHandler +
    PlasmaComponents.ToolTip with `Logic.rowToolTip` when details are off.
  - Shared: `metrics::formatMemoryDetail/formatSwapDetail` (also used by
    `apischema` → API texts unchanged), `hover::whenText` (moved from
    `apischema.cpp`). No API/schema change.
  - Tests: `hoversummary` (new), metricsbarpresenter, metricsbarmodel,
    plasmoidlogic, plasmoidclient (`hoverFollowsMetricsOnlyWhileShown`,
    `hoverSummaryMatchesTheQtTray`: the JS and C++ lines compared on one
    real `GetMetrics` payload, passed to JS as JSON text — a QVariantList
    does not arrive as a JS Array).
- **General principles** (requirements.md): the generic Qt/Linux
  implementation stays the cross-desktop baseline; Plasma functionality is an
  additional layer, never KDE-only; reuse core/models/metrics; reuse the
  metric/state architecture and avoid duplicate samplers; source-specific
  schedules need not match CPU sampling; optional features fail gracefully;
  unsupported hardware/desktops/tools are unavailable/unsupported, not zero
  or fatal; avoid unnecessary polling; prefer native interfaces over
  external commands when practical (F1 command sources are the deliberate
  extension); keep system-wide and `--local` install; keep user data separate
  and safe on uninstall/upgrade; never sacrifice the generic app for a
  desktop-specific frontend.

---

## 13b. v2 history (2.1.0 onwards)

### v2F1.4 — `display.showBar` (2.1.4, 2026-10-23)

- `custommetrics::parseShowBar()` (both parsers): optional bool; non-bool
  or non-object `display` = validation error. Command configs: with
  snapshot output `display` may hold `showBar` alone. Snapshot files: other
  `display` keys ignored.
- `CommandDefinition::showBar`, `Snapshot::showBar` (default true; set in
  `snapshotFromCommandOutput` for both outputs — config wins — and in the
  command skeletons). Presentation only: execution/values/normalized unchanged.
- API schema **4**: `showBar` on custom/integration metrics (absent = true).
- Dashboard `customCard`: bar only if normalizedValue && showBar (not
  created otherwise; card key includes showBar). Plasmoid `Logic.itemRows`/
  `metricRow`: fraction null when `showBar === false`. Metrics Bar has no
  bars (unchanged). No Settings editor (none exists for display props).

### v2F1.3 — Execution warnings (2.1.3, 2026-10-23)

- No new state or API field: Pending/Available/Unavailable/Error + message
  already reach every frontend; schema stays 3.
- Backend (`CustomMetricsService`): missing executable → Error "Executable
  not found: …", non-executable → Error "Permission denied: … is not
  executable." (deliberate change from F1's Unavailable); non-zero ≠ 69 →
  "Command exited with status N." + `diagnosticLine()` (first non-empty
  stderr line, ≤200 chars); output validator errors prefixed "Invalid
  output: "; exit 69 unchanged (stderr ≤512 or "Source is not available.").
  `setOutcome()` sets state/message and logs on category
  `aqtruncat.custommetrics` (Q_STATIC_LOGGING_CATEGORY in the engine lib):
  warning on a new/different failure, info on recovery, using
  `CommandJob::lastOutcome/lastMessage`.
- Dashboard `customCard`: Error → `customError_<i>` icon (dialog-error,
  tooltip = reason), values "—", bars hidden with retainSizeWhenHidden,
  footer "Error: <reason>" (word-wrapped). Unavailable unchanged.
- Metrics Bar model: Error clears shortValue/detail ("!" + reason only).
  Settings status column: dialog-error icon on Error. Plasmoid unchanged
  (already dialog-error vs dialog-warning, "—", reason).
- Test helper mode `control <file>` ("status\nstdout\nstderr…") drives
  transitions without changing the configuration.

### v2F1.2 — Custom Metrics `enabled` (2.1.2, 2026-10-23)

- Optional top-level `"enabled"` in snapshot files and command configs
  (`custommetrics::parseEnabled`; missing = true, non-bool = validation
  error, terminal for commands). `Source::enabled`,
  `CommandDefinition::enabled`, `ParseResult::enabled`.
- Backend (`CustomMetricsService`) = single source of truth: a disabled
  source stays listed, `enabled=false`, state Unavailable, message
  "Disabled.", a snapshot with only title/symbol/icon (no values; its name
  shows in Settings); disabled command = no job (in-flight run
  stopped by `syncCommandJobs` with the process-group rules; disconnected,
  so no late result); disabled sources never arm the recovery timer;
  re-enabled = new job run at once.
- `custommetrics::writeEnabled()`: text edit of only the top-level flag
  (replace value, or insert as first key with the first key's
  indentation), verified by re-parsing, then `QSaveFile` (perms kept,
  follows symlinks). `CustomMetricsService::setEnabled()` writes + `reload(true)`
  at once (unchanged jobs keep running); integrations' sources refused.
- API schema **3** (additive): `enabled` on `custom`/`integration` metric
  objects (absent = true for older backends), D-Bus
  `SetCustomMetricEnabled(s path, b enabled)` (NotFound / InvalidArgument for
  integration sources / Failed on write errors). `CustomMetricsModel::setEnabled`
  (BackendClient → D-Bus).
- Frontends filter: Dashboard, `MetricsBarModel` (selection kept, item
  returns in place), Plasmoid `Logic.hiddenMetric` (`enabled === false`).
  Settings lists disabled sources with a checkbox (column 0, applied queued
  outside itemChanged; write error → warning + row reverts) and shows a
  selected disabled bar entry as "Custom — disabled"; Plasmoid config has
  the same checkbox (`client.setCustomMetricEnabled`).
- Known limit: a producer rewriting a snapshot file without the flag
  re-enables it (flag lives in the file).
- Same 2.1.2, after user feedback: Plasmoid `Logic.syncModel` is now
  incremental (remove/move/insert rows, never `clear()`), so the popup keeps
  its scroll position when a metric comes or goes (bug since F4.2), and
  `Logic.settleRefreshes` keeps a Custom Metric that already has values in
  its last settled state while its command runs again (pending used to
  collapse the row → height jump → scroll moved), as the Qt Dashboard does;
  Dashboard replaces only the Custom Metrics cards whose presentation key
  changed (`customCardKey`, same source list) instead of rebuilding all on
  every command run; Settings Custom Metrics list columns are now Name
  (title, checkbox) / File / Origin / Status.

### v2F1.1 — Rickroll runner (2.1.1, 2026-10-23)

- 2.1.0 baseline documented only; version 2.0.0 → 2.1.1 directly.
- Assets prepared with the user (photo GIF → silhouette + stencil of face/
  collar/shirt, variant chosen by the user; duplicate GIF frame 1 dropped):
  `third_party/rickroll/runners/rickroll/rickroll-frame-{0..30}.png`,
  33×36, monochrome; `third_party/rickroll/make-frames.py` reproduces them
  byte for byte from the GIF (SHA-256 in the folder README; GIF not in
  the repo). Attribution: Rick Astley, "Never Gonna Give You Up", the
  Rickroll meme; no licence claimed, user's decision.
- Integration exactly like F7: id in `AQTRUNCAT_BUNDLED_RUNNERS`,
  `runner_frames_rickroll`, `data/runners/rickroll/metadata.json`; no code
  change in loader, tray, backend, API or Plasmoid. Tests extended in
  `bundledrunners`, `backendipc`, `plasmoidclient`, `tst_logic.qml`,
  installer. Debian copyright stanza `third_party/rickroll/runners/*`.
- Record: docs/built-in-runners.md §10.

---

## 14. Important files map

```text
requirements.md                v2 roadmap (2.0.0 baseline, v2 runner convention, v2F1 Custom Metrics v3 + Rickroll 2.1.x, v2F2 Independent Autostart 2.2.x, v2F3 Standalone Settings 2.3.x, v2F4 Dependency Advisor 2.4.x), versioning, general principles. Older "requirements §n" references in docs point to the former M0–M9 spec (decisions kept in §5 here)
docs/README.md                 documentation index: architecture overview, topic documents, milestone → document map
docs/feature-reference.md      normative summaries of F1–F7 (formerly the body of requirements.md)
THIRD_PARTY_NOTICES.md         runner frame provenance: RunCat Neo (Apache-2.0); Walking Goose / Party Parrot (Tenor GIFs, licence unknown, user's decision)
docs/upstream-runcat-neo-analysis.md   upstream behaviour (runners, metrics, Custom Metrics schema §6, Linux risks)
docs/tray-spike.md          SNI/Plasma findings, cost measurements (B classification)
docs/cpu-and-runner-speed.md          CPU semantics, sampling/smoothing/stabiliser tuning, runtime test
docs/runners.md       runner model, loader, discovery, Gallery compatibility
docs/runner-import-and-cli.md  ZIP security, install/remove, CLI grammar, exit codes
docs/system-metrics.md        metrics semantics, policies, validation, performance
docs/dashboard.md           Dashboard/shared sampler architecture, UI/runtime/performance evidence
docs/settings-and-desktop-integration.md    typed settings, preview, speed/network policy, XDG autostart, validation
docs/custom-metrics.md      historical M6 snapshot schema, registry/watch/retry, Dashboard/Settings, validation
docs/custom-metrics-commands.md   F1 command schema/output/security, scheduler/lifecycle, packages/icons, validation
docs/integration-claude-code.md  F2: Part A revision 2026-10-20 (quota sources, semantics, live updates, Integrations, installer stop, tests, validation); Part B first implementation (context window)
docs/metrics-bar.md         Plasma SNI spike, model/renderer/presenter, formatting, statuses, settings, ordering, cost
docs/desktop-compatibility.md  distro/desktop matrix, suspend, tray restart, fixes, perf, soak
README.md                      user-facing: features, supported systems, install/uninstall, packages, usage, limitations
LICENSE                        Apache-2.0
install.sh                     user installer (system / --local, autostart, uninstall); `--help`
packaging/                     make-source-tarball.sh, arch/PKGBUILD, rpm/aqtruncat.spec, debian/
data/aqtruncat.desktop, data/aqtruncat.1, data/icons/   desktop entry, man page, original icon
tests/installer/test_install.sh, tests/packaging/check_versions.sh   bash tests registered in ctest
docs/installation-and-packaging.md  layout, installer, deps, packages, Flatpak/AppImage evaluation
CMakeLists.txt                 Qt/libarchive detection, tests, AQTRUNCAT_BUILD_SPIKES
src/CMakeLists.txt             libraries, bundled runner qrc, AQTRUNCAT_BUNDLED_RUNNERS / _DEFAULT_RUNNER
src/core/                      Qt-free CPU math (cpustat), Ema, speed curve + stabiliser, FramePlayback
src/runner/                    Runner model, loader, natural sort, catalog/XDG, animator, archive/installer/remover, RunnerService
src/metrics/                   Qt-free metrics: parsers, CounterRateTracker, SystemMetricsSampler, formatter
src/custommetrics/             shared Snapshot/Source model, bounded M6 parser, F1 command config/scalar parser
src/cli/                       commandline (grammar/help/exit codes), runnercommands, metricscommand
src/app/                       runtime/UI: AppSettings, MetricSampler, CustomMetricsService, CPU controller, Dashboard, Settings/preview, autostart
src/providers/claude/          F2 Claude Code adapter (aqtruncat_claude lib: claudecode context/collect, claudeusage quotas, statuslinebridge settings.json edit; aqtruncat-claude-metric exe + icon); not linked by the core
src/providers/common/          shared provider code (aqtruncat_provider_common: quota judgement/item format, package helpers)
src/providers/codex/           F3 Codex adapter (aqtruncat_codex lib: codexusage local/live/state/collect; aqtruncat-codex-metric exe + icon); not linked by the core
docs/integration-codex.md        F3 investigation/sources, semantics, freshness, accounts, architecture, Settings, tests, Plasma validation
src/app/providerintegration.*  Settings-side driver of an official integration's adapter CLI (status --json, enable, disable, live-updates)
src/api/                       F4.1 public API: apischema (identity, versions, JSON schema both ways), backendservice (D-Bus object), backendclient (Qt client, CustomMetricsModel)
src/daemon/main.cpp, src/ctl/main.cpp   F4.1 aqtruncatd backend and aqtruncatctl CLI
data/io.github.psykedady.AQTRunCat.service.in   D-Bus activation file template (Exec=@AQTRUNCAT_DBUS_EXEC@)
docs/backend-dbus-api-cli.md   F4.1 architecture, lifecycle, complete API contract, CLI, security, performance, limits (+ Python client example dir)
plasmoid/package/              F4.2 KDE Plasma 6 widget (QML only): metadata.json, contents/code/logic.js (pure, tested), contents/config/{main.xml,config.qml}, contents/ui/{main,BackendClient,CompactRepresentation,RunnerView,FullRepresentation,MetricDelegate,configGeneral,configAppearance}.qml
src/api/runnercatalog.*        F4.2 GetRunners payload (lib aqtruncat_runner_catalog with app/runnerselection.*; QtGui; linked by aqtruncatd and the tray only)
docs/plasma-widget.md   F4.2 strategy, package, IPC, compact/popup, settings, coexistence, install/packaging, security, perf, tests, runtime validation, limits (+ screenshots dir)
tests/tst_plasmoidclient.cpp, tests/tst_plasmoidpackage.cpp, tests/plasmoid/tst_logic.qml   F4.2 tests (client on a private bus with activation wrapper; package/plasmoidviewer; qmltestrunner)
tools/plasmoid-preview.{sh,qml}   offscreen popup/runner previews with demo data (light/dark PNG), private bus, isolated XDG (F5: update checks off)
src/metrics/{uptime,systeminfo,gpu,fps,packageupdates}.*   F5 Qt-free sources: /proc/uptime, uname + os-release, DRM/sysfs/NVML GPUs, MangoHud log reader, update-check parsers/commands
src/app/updatesservice.*, boundedprocess.*, processgroup.h   F5 backend update checks (two non-overlapping jobs), bounded subprocess (shared process-group rules with F1)
docs/built-in-runners.md   F7 assets, CMake integration/cache migration, selection, packaging, tests, isolated runtime, deferred Engine, manual checks
third_party/{walkinggoose,partyparrot}/   F7 runner frames (derived from Tenor GIFs, licence unknown, user's decision) + README provenance
third_party/rickroll/          2.1.1 runner frames runners/rickroll/rickroll-frame-{0..30}.png (Rick Astley, "Never Gonna Give You Up", Rickroll meme GIF; no licence claimed, user's decision), README provenance (GIF SHA-256), make-frames.py (reproduces the frames byte for byte from the GIF, not in the repo)
tests/tst_bundledrunners.cpp   F7 real bundled runners: files, list, selection, persistence, fallbacks
src/app/hoversummary.*   F6 hover summary (CPU/RAM/swap lines, backend states, whenText), QtCore only
docs/hover-information.md   F6 existing tooltips, shared format, tray/Plasmoid/details, API unchanged, performance, tests, isolated runtime validation, manual checks
docs/additional-built-in-metrics.md   F5 investigation, ids/sources/units, GPU/FPS/updates semantics, API/CLI/UI, settings, performance, security, tests, runtime validation, limits (+ images dir)
data/runners/<id>/metadata.json   AQTRunCat-authored metadata for bundled runners
third_party/runcatneo/         upstream LICENSE + frames (cat, coffee bundled; engine spike-only)
tests/                         Qt Test suites (tst_*.cpp), including F1 deterministic command helper/service cases and F2 Claude fixtures (synthetic registry/transcripts/fake /proc)
tools/                         permanent diagnostic scripts (§10)
spike/m1-tray/                 throwaway M1.0 prototype + its measurement tools (not built by default)
```

---
