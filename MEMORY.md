# MEMORY.md — AQTRunCat handoff for an AI agent

> Audience: an AI agent continuing development with no access to earlier
> conversations. M0–M9 implementation state was verified against the
> repository on 2026-10-10; the post-project roadmap was revised and F1 was
> implemented on 2026-10-13, then audited, hardened and runtime-validated on
> 2026-10-16 (branch `feature/f1`). F2 was investigated, implemented and
> runtime-validated on 2026-10-16 on branch `feature/f2`; the user committed
> F1 and F2 ("f2 terminato", 3115c56). On 2026-10-20 F2 was **revised** on
> `feature/f2` (uncommitted at that time; the user owns all Git writes):
> official Integrations section in Settings, Claude plan usage quotas
> (5-hour + weekly, as in `/usage`) as primary values, optional live updates
> via the Claude Code status line, and `install.sh` closing a running
> instance before replacing it (§13).
> Precedence when sources disagree: **actual code/tests > this MEMORY (frozen
> decisions §5) > docs/M*.md > historical prompts**. `requirements.md` is now
> the **Post-Project Features** document (F1–F6), the current source for
> future work; the former M0–M9 normative spec it contained was replaced by
> the user on 2026-10-10 (see §0 and §13).
>
> First instruction you will likely receive: *"Read MEMORY.md and continue
> AQTRunCat."* → **the initial roadmap M0–M9 is COMPLETE; M9 concluded it.**
> AQTRunCat is in post-project development. The roadmap features are F1–F6 in
> requirements.md (§13 here). **F1 and F2 are COMPLETE. F3–F6 are NOT
> STARTED; do not start any of them without an explicit user instruction.**

---

## 0. Working rules (user-imposed, apply to every task)

- **Git: no commit/push/history rewrite unless explicitly authorised.** Since
  2026-10-10 the user has a Git repository again (`main`, remote
  `origin git@psdadygithub:PsykeDady/AQTRunCat.git`, first commits by the
  user; `MEMORY.md` and `requirements.md` were untracked at that time). Read-only
  `git status`/`git log`/`git diff --check` are fine for verification. No git
  config changes.
- Work **only on the requested milestone**; stop at its end and report. Do not
  implement later milestones because they "look easy".
- `requirements.md` is the user's **Post-Project Features** document (F1–F6):
  do not modify it unless asked. M0–M9 normative decisions now live in §5 of
  this file and in `docs/M*.md` (historical reports: do not rewrite their
  observed results; add pointers/notes instead).
- **Stop conditions**: if a choice with significant user-visible semantics has
  several equally reasonable options, stop and report *Option A / Option B /
  trade-offs / recommendation* instead of picking arbitrarily. If it is
  clearly standard Linux practice, implement and document it. If a new
  significant dependency is needed, stop and ask first.
- Post-project Git model: `feature/base` is the dedicated common baseline;
  feature branches are created from it as `feature/f1`, `feature/f2`,
  `feature/f3`, … . (Observed 2026-10-20: the repository has `main`,
  `feature/main`, `feature/f1`, `feature/f2` and **no** `feature/base`; ask
  the user which baseline to use before F3.) The slash is only part of the branch name and implies no
  Git hierarchy. Do not create, switch, merge, commit or push branches unless
  explicitly requested.
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
  personal data. M4 evidence explicitly lives in `docs/M4-dashboard/*.png`;
  the older M1 spike captures remain ignored historical artifacts.
- End-of-milestone checklist the user always asks for: clean build, 0
  avoidable warnings, all tests, real Plasma/Wayland run, earlier tools still
  work, no leftover processes/temp files, no personal paths, docs
  (`docs/M<n>-*.md`) + requirements update, list of created/modified files,
  test counts, runtime results, open decisions. Report in Italian (the user
  writes Italian; `docs/*.md` are in Italian; code/comments/requirements in
  English).
- Licensing: AQTRunCat is Apache-2.0. Do not use RunCat branding/icon as
  AQTRunCat identity. Do not import `runcat-tray` assets (unclear/GPL
  provenance). Record any copied upstream asset in `THIRD_PARTY_NOTICES.md`
  (repo, commit, original path, license).

---

## 1. Project identity

- Name **AQTRunCat**, tagline **"A Cute Run Cat for Linux"**, executable
  **`aqtruncat`**, version `0.0.1` (CMake `PROJECT_VERSION`).
- Goal: bring the RunCat Neo (macOS) experience to Linux — an animated tray
  runner whose speed follows CPU load, plus system metrics, dashboard, custom
  metrics, metrics bar — as a *proper Linux/Qt application*, not a mechanical
  macOS port and not a fork of `runcat-tray`.
- Stack: C++20, Qt 6 (Widgets/Gui/Core/Test), CMake ≥ 3.21, libarchive ≥ 3.0.
- Primary target: Arch Linux, **KDE Plasma 6 + Wayland**. Avoid KDE-specific
  dependencies in the generic application (no KDE Frameworks or LayerShellQt).
  Planned F4 adds an optional native Plasmoid alongside, never in place of,
  that portable baseline. Metrics core must not depend on Qt GUI, KDE or
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
POST-PROJECT  F3–F6 roadmap features (requirements.md), NOT STARTED, need an explicit instruction
```

- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
  Clean build: **0 warnings** (`-Wall -Wextra -Wpedantic` on all targets).
- Tests: **38 ctest suites (36 Qt Test suites + Bash `installer` and
  `packaging-versions`), all passing** on the host (2026-10-20); containers
  skip 4 Qt cases (locale ×2, root permissions ×2).
- Dependencies: Qt 6.5+ (`Widgets`, `Test`; dev machine has 6.11.2), libarchive
  ≥ 3.0 (CMake `FindLibArchive`; configure fails with an explanatory message if
  missing). At runtime on Plasma, Qt's KDE platform theme loads ~28 KF6 libs
  (not a build dependency).
- CLI: `aqtruncat [run|runner list|runner import|runner remove|metrics|autostart]` (§6).
- Install: `cmake --install` (GNUInstallDirs) is the single source of truth;
  `install.sh` (system /usr/local with sudo only for copies, or `--local` XDG
  without sudo; autostart ON by default; manifest-based uninstall keeping user
  data); native packages in `packaging/` (Arch PKGBUILD, Fedora RPM, Debian);
  Flatpak/AppImage deferred. License Apache-2.0 (`LICENSE`); original icon.
- Runner system: generic runners, bundled `cat` (default) + `coffee`, user
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
  `docs/F1-custom-metrics-v2.md`). F2 adds `source.output: "snapshot"`
  (multi-value command output) and exit 69 → Unavailable.
- Claude Code integration (F2, revised 2026-10-20): separate QtCore
  executable `aqtruncat-claude-metric` (lib `aqtruncat_claude`, not linked by
  the core) run as a normal F1 command; primary values = plan usage quotas
  (Current session 5h, Current week all models) with reset times; managed
  from Settings › Integrations or its CLI (`enable`/`disable`/`status`/
  `live-updates`), which share one configuration owned by the adapter
  (`docs/F2-claude-code-metric.md`). Snapshot items may carry `id`/`detail`/
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
Plasma/KWin 6.7.5, KF 6.30, Qt 6.11.2, libarchive 3.8.9, i7-11390H (8 threads),
two NVMe (root ext4 on nvme0n1p7, /home ext4 on nvme1n1p2 with ecryptfs home),
zram swap, Wi-Fi `wlan0`, `docker0`; eDP-1 1920×1080 at scale 1.25 (tray here)
+ HDMI 1.0; panel thickness 33/34; Breeze Light. `clamd` (ClamAV) is in a
crash loop on this machine, keeping ~1 core busy → idle CPU reads ~19–30 %.

---

## 3. Architecture (as implemented)

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
| `aqtruncat_desktop` | `app/` + `custommetrics/`: AppSettings, shared MetricSampler, CustomMetricsService/parser, Dashboard/Settings/preview, storage selection, tray/autostart | metrics, runner, Qt6::Widgets |
| `aqtruncat` (exe) | `app/`: main, trayapp, logging; bundled runners as qrc | desktop, CLI + Qt6::Widgets |

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
  colour runners untouched; tooltip "AQTRunCat — <name>"; context menu
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

### M0 — Upstream analysis (docs/M0-upstream-analysis.md)
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
- **M1.0 tray spike** (docs/M1-tray-spike.md): result **B — Qt tray is
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
- **M1.1 CPU runner** (docs/M1-cpu-runner.md): `/proc/stat`, 1 s sampling,
  EMA α 0.4, FpsStabilizer (immediate ≥3 FPS, smaller changes need 3
  consecutive samples) chosen by replaying recorded data (step 2 → 7 flips in
  24 s at half load; step 3 → 1 flip). Runtime: idle 11 fps, half 26–27, full
  39–40, recovery fast; real tray FPS confirmed via D-Bus `NewIcon` rate.
- **M1.2 cleanup/freeze**: iowait = idle (final); slow-down curve fixed
  (mirrored, not implemented); permanent tools moved to `tools/`; spike tools
  stay in `spike/m1-tray/tools/`.
- Curve: `fps = round(10 + 30·(cpu/100)²)`, cpu clamped [0,100] → 10–40 FPS.

### M2 — Runner system (docs/M2-runner-system.md, docs/M2.1-runner-lifecycle-cli.md)
- Generic `Runner` model; nothing in code knows "cat" (default id only in CMake).
- Bundled `cat` + `coffee` (RunCat Neo frames, `third_party/`, + own metadata).
- Loader with fatal/warning split, natural ordering, Gallery layouts, metadata,
  monochrome detection; catalog with XDG user dir and user > bundled precedence.
- **M2.1**: libarchive ZIP import (secure extraction → validation → atomic
  install), `--force` replace with rollback, removal (bundled protected), the
  official sub-command CLI with legacy aliases and exit codes 0–5.
- Verified: all 43 Gallery ZIPs import/list/run; frame order verified end-to-end
  through D-Bus on 7 runners incl. a 24-frame one.

### M3 — Core metrics (docs/M3-core-metrics.md)
- Qt-free metrics layer, typed snapshot, explicit status (unavailable ≠ 0),
  one CPU implementation shared with the runner, real-elapsed-time rates,
  hardware-only network aggregation, whole-physical-disk I/O aggregation, df
  storage semantics, locale-independent binary formatter, `aqtruncat metrics`
  and `--watch`.
- Validated against `free`, `df`, independent `/proc` counters; ~108 µs per
  snapshot, `metrics --watch` ≈0.05 % CPU.

### M4 — Dashboard (docs/M4-dashboard.md)
- Compact Qt Widgets Dashboard with CPU, memory, swap, aggregate network,
  Root/Home capacity and aggregate physical-disk I/O; explicit N/A/waiting.
- One desktop MetricSampler feeds both Dashboard and runner. CpuMonitor now
  retains only the M1 EMA; curve and stabiliser are unchanged.
- Primary tray activation toggles one reusable `Qt::Tool` window; DBusMenu
  supplies Show/Hide fallback and Quit. Closing the window only hides it.
- Root is always sampled; Home only when `stat(2).st_dev` differs. Plasma 6
  Wayland, fractional scale, runtime tools and hidden/visible performance were
  validated; screenshots are under `docs/M4-dashboard/`.

### M5 — Settings and Desktop Integration (docs/M5-settings-desktop.md)
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

### M9 — Packaging, Installation & Release Readiness (docs/M9-packaging-installation.md)
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

### M8 — Desktop Compatibility & Polish (docs/M8-desktop-compatibility.md)
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

### M7 — Metrics Bar (docs/M7-metrics-bar.md)
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
  Details: docs/M7-metrics-bar.md §10–§11. Final: 30 suites / 472 cases.
- M7 fixed `tools/test-cpu-runtime.sh` to count the runner item's NewIcon.

### M6 — Custom Metrics (docs/M6-custom-metrics.md)
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
| Bundled runners | `cat` (default) + `coffee` only | don't add more without being asked |
| XDG | runners: `$XDG_DATA_HOME/AQTRunCat/runners/`; settings: `$XDG_CONFIG_HOME/AQTRunCat/AQTRunCat.ini`; autostart: `$XDG_CONFIG_HOME/autostart/aqtruncat.desktop` | Qt/XDG standard locations, data and config separate |
| Custom Metrics snapshot contract (M6, preserved by F1) | external RunCat Neo JSON snapshots; `formattedValue` stays opaque; snapshot sources run no commands/providers/scheduler | compatibility with existing producers; F1 deliberately adds a separate command-backed source while retaining this snapshot contract |
| Custom Metrics command contract (F1) | explicit `source.type=command`; required `id`, `title`, `executable`; direct executable + ≤256 args, no shell/elevation; scalar UTF-8 stdout; prefix/suffix and optional normalized range | invalid/malformed config is never executed or treated as snapshot |
| Command scheduling/lifecycle (F1) | one shared monotonic timer; immediate first run; next interval (1–86400 s) starts after completion; timeout default 10 s/range 1–300 s; no overlap/catch-up; each command is its own process-group leader (`setpgid` child modifier); timeout/overflow SIGKILL the group, then re-check every 1 s (no spin); stop = SIGTERM all groups, ≤250 ms total wait, SIGKILL, final group SIGKILL; helpers left in background after a *successful* run are not killed | independent schedules without CPU-sampler coupling or idle polling; scripts' children never leak |
| Command publish cost (F1) | service publishes at start and end of every run; hidden Dashboard stores the list and rebuilds cards only on show; Settings keeps the selected custom row across rebuilds | 5 commands cost 2.2 % → 0.87 % of a core after deferral |
| Command output/state (F1) | stdout/stderr each ≤16 KiB; one trimmed nonempty scalar line; missing/non-executable = Unavailable; startup/exit/timeout/parse = Error; refresh = Pending retaining last value; Dashboard presents the last settled state during a refresh (no "Waiting…" flicker) | bounded failure, diagnostic stderr, never silent zero |
| Claude Code integration (F2, revised 2026-10-20) | adapter outside the core, run as F1 `output:"snapshot"` command every 10 s; **primary = plan quotas** five_hour / seven_day from `~/.claude.json` `cachedUsageUtilization` (what `/usage` fetched; same account only) and, opt-in, status-line `rate_limits` captured to `$XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json`; newest report per window wins; >5 min → "as of", past reset or >24 h → not shown; context window only with `--context`; opt-in, never auto-added to the bar | exactly the `/usage` concepts; no credentials/network/tokens; never derived from context tokens; never 0 for missing |
| Live updates (F2) | only the `statusLine` member of Claude Code's user settings.json, textual edit verified by parse-compare, refuse otherwise; backup (value + exact text) in `$XDG_DATA_HOME/AQTRunCat/integrations/claude-code/statusline-backup.json`; helper chains the previous command via memfd + `execl("/bin/sh","-c",…)`; off/disable restore byte-exactly; user-changed statusLine left alone; explicit confirmation in Settings | user decision 2026-10-20 (hybrid source); never silently modify Claude config |
| Integration configuration ownership (F2) | the adapter owns package + backup; Settings runs the adapter CLI (`status --json` etc.), so GUI = CLI | one configuration, no second metric engine |
| Integration provenance (F2) | optional top-level `"integration": "<id>"` in a **command** configuration (written by the adapter) → `CommandDefinition::integration` → `Source::integration`; empty = user Custom Metric; M6 snapshots never; never by title/icon/path. Dashboard: **Integrations** section before **Custom Metrics**, same card renderer, each source once, empty sections hidden (no "No custom metrics" label). Old unmarked package: Settings rewrites it once (`integrationMarked` in `status --json`) | user requirement 2026-10-20: provider ≠ user metric |
| Snapshot item extensions (F2) | optional `id` (`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`, unique), `detail`, `metricsBarValue`, `metricsBarLabel`; bar id `custom-item:<item>:<source>`; item without compact value = Unavailable; caption replaces the source icon | multi-value providers without Claude-specific UI code |
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
| Dashboard refresh | Settings throttles presentation to 1/2/5 s; sampler and runner stay 1 Hz | preserves M1 EMA/sample semantics |
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
| Installer autostart | default ON; always through `aqtruncat autostart enable|disable` as the real user | app re-applies the INI preference at start-up (M9) |
| Packages | Arch/RPM/DEB recipes in packaging/, versions checked by ctest `packaging-versions`; Flatpak/AppImage deferred | M9 evaluation |

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
from `third_party/runcatneo/runners/<id>/`, metadata from `data/runners/<id>/`;
CMake cache vars `AQTRUNCAT_BUNDLED_RUNNERS="cat;coffee"`,
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

Sampling cost: `sample()` ≈ 108 µs (≈50 syscalls: 6 opens, 18 reads,
~14 stats for classification, 1 statfs); classification is redone every
sample on purpose (hot-plug) — caching not needed.

---

## 9. Tests and validation

Automated (38 ctest suites: 36 Qt suites + 2 Bash suites, 0 warnings, verified 2026-10-20 after the F2 revision — new `claudeusage` 19 and `claudestatusline` 12, `claudecodeintegration` 12, `claudecodemetric` 28 (now `--context`), custommetric 22, commandmetric 13, dashboard 14, metricsbarmodel 17, appsettings 14, installer 189 checks; earlier: 36 suites verified 2026-10-16 after F2; the per-suite table below predates F1 — F1 added `commandmetric` 10 and `commandmetricsservice` 11, and raised custommetric 16, dashboard 11, metricsbarmodel 16, metricsbarrenderer 23; F2 added `claudecodemetric` 28 and `claudecodeintegration` 6 (QtTest totals incl. data rows/init/cleanup) and +2 each to commandmetric/commandmetricsservice; tests never read the real
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

---

## 12. Deferred decisions

M8 and M9 are complete (docs/M8-desktop-compatibility.md,
docs/M9-packaging-installation.md). Items deferred there and not covered by
F1–F6 (backlog notes, not scheduled): Flatpak and AppImage *application
packaging* (distinct from the F5 Flatpak-updates metric; portal-based
autostart, sandbox paths for Custom Metrics/storage, bundled-Qt theme
integration), manual light/dark tray colour preference for generic hosts
(M8), AppStream metainfo (needs a stable app ID/homepage), machine-readable
CLI output (`--json`), arbitrary filesystem selection for storage, importing
RunCat Neo custom runner names (§11).

---

## 13. Post-project features F1–F6 (requirements.md)

M0–M9 are complete; M9 concluded the initial roadmap. `requirements.md`
("AQTRunCat — Post-Project Features") is the current source for future work
and supersedes the earlier F1–F4 post-project ordering. **F1 and F2 are
COMPLETE. F3–F6 are NOT STARTED; do not start one without an explicit user
instruction.**

Development baseline: future post-project work starts from `feature/base`.
Each feature branch (`feature/f1`, `feature/f2`, `feature/f3`, …) is created
from that common history. The slash is naming only, not Git hierarchy. Do not
create or switch branches, commit or push without explicit permission.

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
  remains the multi-value path. Full contract: `docs/F1-custom-metrics-v2.md`.
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
  2026-10-20**. Full record: `docs/F2-claude-code-metric.md` (Part A =
  revision/current behaviour, Part B = first implementation, kept as
  history); normative summary in requirements.md. The M6 Claude JSON example
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
    >24 h → "Unknown" + reason; missing → "Not reported" + hint. No report at
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
    `docs/M9` note updated.
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
    snapshot and a user command (`docs/F2-claude-code-metric/
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
- **F3 — Codex Metric** (NOT STARTED; F2 left the path: a sibling
  `src/providers/codex/` adapter emitting an F1 snapshot with exit 69 for
  absent data, no shared AI framework yet): investigate reliable Codex usage/context/status data
  and use the same F1 architecture principles as F2. Claude and Codex must
  coexist; share common scheduling/state/configuration/presentation rather
  than duplicating provider architecture.
- **F4 — KDE Plasma Plasmoid**: an additional native Plasma frontend
  that runs **in parallel** with the generic application and **never replaces
  the generic Qt/SNI frontend** (which stays fully supported, also on Plasma);
  both reuse the same core (metrics, settings, runner state, models,
  configuration, application logic) without duplicating business logic.
  - F4.1 install: `install.sh` detects a compatible Plasma; if detected the
    Plasmoid is installed **by default** alongside the app; `--no-plasmoid`
    disables it, `--plasmoid` requests it explicitly when Plasma is not
    detected (if dependencies/compatibility are satisfied); documented in
    `--help`; works with system-wide and `--local`; uninstall removes the
    Plasmoid only when it belongs to the installation being removed, never
    other widgets or user data; all existing installer guarantees (manifest,
    user data preservation, autostart) keep applying.
  - F4.2 integration: behave like a normal Plasma widget, with a native
    Plasma popup comparable to the Network/Wi-Fi Plasmoid; integrate with the
    panel, popup behaviour, Plasma themes, panel sizing, orientation where
    practical and Plasma configuration conventions.
- **F5 — Additional Built-in Metrics**: system uptime (F5.1, prefer direct
  interfaces); GPU metrics (F5.2: utilization, VRAM, temperature…,
  vendor/driver differences, graceful degradation); FPS metrics for gaming
  (F5.3: method to be investigated first, no unacceptable overhead);
  system/package updates
  (F5.4: per distribution family behind a common metric interface, failures
  isolated); Flatpak updates (F5.5: optional, systems without Flatpak
  unaffected); kernel and distribution information (F5.6: separate concepts,
  no single required source — `uname`-equivalent kernel interfaces,
  `/etc/os-release` when available, distribution-specific files, fallbacks;
  unknown/unavailable instead of failure). All keep the valid / pending /
  unavailable / error distinction and the existing metric architecture.
- **F6 — Hover Information**: hovering the tray/applet item shows a compact
  summary of at least CPU and memory usage without opening the Dashboard, in
  both the generic SNI frontend (tray tooltip) and the Plasma frontend
  (native hover/tooltip); reuse the existing sampled metrics, no second
  polling mechanism.
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
  desktop-specific frontend; F3–F6 remain future work, not implemented.

## (historical) M9 brief
Install target, `.desktop`, own icon, Arch PKGBUILD, Flatpak/AppImage
evaluation; completed in docs/M9-packaging-installation.md.

## (historical) M8 brief
Plasma X11, other SNI hosts, suspend/resume, tray restart, performance,
error handling; completed in docs/M8-desktop-compatibility.md.

## (historical) M7 — Metrics Bar brief

Goal (requirements §12 and M7): investigate and implement the best reliable
Linux equivalent of RunCat Neo's textual Metrics Bar without compromising the
runner or performance.

Preserve during M7:

- the M6 `CustomMetricSnapshot` model, including opaque `metricsBarValue`;
- CustomMetricsService ownership, watching and error isolation;
- the single M3 MetricSampler and typed SystemMetricsSnapshot;
- M1–M6 tray, Dashboard, Settings and CLI contracts.

SNI on Plasma has no native text label. M0/M1 indicate that a separate or
combined rendered pixmap is likely, but square tray slots, D-Bus traffic and
runner readability require a measured design decision before implementation.

---

## 14. Important files map

```text
requirements.md                Post-Project Features F1–F6 and feature/base → feature/fN branch model (user document, current source for future work; replaced the M0–M9 spec on 2026-10-10 and was reorganized on 2026-10-13). Former §-references below (e.g. §5.4, §8, §12) point to that M0–M9 spec, whose decisions are kept in §5 here and docs/M*.md:
                               §9 metrics, §13a CLI, §20 tools, §24 milestones (status), §26 source control
THIRD_PARTY_NOTICES.md         RunCat Neo frame provenance (Apache-2.0)
docs/M0-upstream-analysis.md   upstream behaviour (runners, metrics, Custom Metrics schema §6, Linux risks)
docs/M1-tray-spike.md          SNI/Plasma findings, cost measurements (B classification)
docs/M1-cpu-runner.md          CPU semantics, sampling/smoothing/stabiliser tuning, runtime test
docs/M2-runner-system.md       runner model, loader, discovery, Gallery compatibility
docs/M2.1-runner-lifecycle-cli.md  ZIP security, install/remove, CLI grammar, exit codes
docs/M3-core-metrics.md        metrics semantics, policies, validation, performance
docs/M4-dashboard.md           Dashboard/shared sampler architecture, UI/runtime/performance evidence
docs/M5-settings-desktop.md    typed settings, preview, speed/network policy, XDG autostart, validation
docs/M6-custom-metrics.md      historical M6 snapshot schema, registry/watch/retry, Dashboard/Settings, validation
docs/F1-custom-metrics-v2.md   F1 command schema/output/security, scheduler/lifecycle, packages/icons, validation
docs/F2-claude-code-metric.md  F2: Part A revision 2026-10-20 (quota sources, semantics, live updates, Integrations, installer stop, tests, validation); Part B first implementation (context window)
docs/M7-metrics-bar.md         Plasma SNI spike, model/renderer/presenter, formatting, statuses, settings, ordering, cost
docs/M8-desktop-compatibility.md  distro/desktop matrix, suspend, tray restart, fixes, perf, soak
README.md                      user-facing: features, supported systems, install/uninstall, packages, usage, limitations
LICENSE                        Apache-2.0
install.sh                     user installer (system / --local, autostart, uninstall); `--help`
packaging/                     make-source-tarball.sh, arch/PKGBUILD, rpm/aqtruncat.spec, debian/
data/aqtruncat.desktop, data/aqtruncat.1, data/icons/   desktop entry, man page, original icon
tests/installer/test_install.sh, tests/packaging/check_versions.sh   bash tests registered in ctest
docs/M9-packaging-installation.md  layout, installer, deps, packages, Flatpak/AppImage evaluation
CMakeLists.txt                 Qt/libarchive detection, tests, AQTRUNCAT_BUILD_SPIKES
src/CMakeLists.txt             libraries, bundled runner qrc, AQTRUNCAT_BUNDLED_RUNNERS / _DEFAULT_RUNNER
src/core/                      Qt-free CPU math (cpustat), Ema, speed curve + stabiliser, FramePlayback
src/runner/                    Runner model, loader, natural sort, catalog/XDG, animator, archive/installer/remover, RunnerService
src/metrics/                   Qt-free metrics: parsers, CounterRateTracker, SystemMetricsSampler, formatter
src/custommetrics/             shared Snapshot/Source model, bounded M6 parser, F1 command config/scalar parser
src/cli/                       commandline (grammar/help/exit codes), runnercommands, metricscommand
src/app/                       runtime/UI: AppSettings, MetricSampler, CustomMetricsService, CPU controller, Dashboard, Settings/preview, autostart
src/providers/claude/          F2 Claude Code adapter (aqtruncat_claude lib: claudecode context/collect, claudeusage quotas, statuslinebridge settings.json edit; aqtruncat-claude-metric exe + icon); not linked by the core
src/app/providerintegration.*  Settings-side driver of an official integration's adapter CLI (status --json, enable, disable, live-updates)
data/runners/<id>/metadata.json   AQTRunCat-authored metadata for bundled runners
third_party/runcatneo/         upstream LICENSE + frames (cat, coffee bundled; engine spike-only)
tests/                         Qt Test suites (tst_*.cpp), including F1 deterministic command helper/service cases and F2 Claude fixtures (synthetic registry/transcripts/fake /proc)
tools/                         permanent diagnostic scripts (§10)
spike/m1-tray/                 throwaway M1.0 prototype + its measurement tools (not built by default)
```

---

## Historical task prompts (condensed; original intent and acceptance criteria)

<details>
<summary>M0 — Upstream Analysis</summary>

Goal: before implementing anything, understand from **source code** (not
READMEs) what RunCat Neo really does and how much `runcat-tray` covers.
Questions: Neo features; runcat-tray features; gaps; Neo internals relevant
to AQTRunCat; macOS-specific parts and Linux/Qt equivalents; compatible
formats; legally reusable assets; KDE Plasma 6 + Wayland problems; minimal
clean path to M1. Runners: format, assets, frames, animation, speed, CPU→FPS,
orientation, custom runners, Gallery, install/remove; can AQTRunCat load Neo
runners directly? Metrics: for each — what it measures, macOS source, update
frequency, transforms, display, Linux equivalent; don't assume semantics.
Custom Metrics: exact JSON schema, watched paths, naming, update/watching,
error handling, formatting, icons/colours/units, producer samples, Claude
Code/Codex integrations. Do not implement.
</details>

<details>
<summary>M1.0 — Tray Spike</summary>

Question: is Qt 6 / QSystemTrayIcon good enough on Plasma 6 + Wayland?
Minimal sacrificial Qt app: tray icon, licensed Neo runner, correct frame
animation, clean exit; fixed FPS via CLI (2/10/20/40). Measure rendering
(real size in tray, aspect, scaling, interpolation, sharpness, legibility,
clipping, wide runners, light/dark) and performance per FPS (effective FPS,
smoothness, app CPU/RAM, plasmashell impact, warnings, frame drops/coalescing,
tray update cost, D-Bus traffic) using external tools only. Investigate
QSystemTrayIcon → SNI/D-Bus → Plasma, separating observed / documented /
hypothesis. No CPU-driven animation, no Plasma workarounds, no
settings/dashboard/plasmoid/KF. Deliver docs/M1-tray-spike.md with
environment, implementation, rendering, performance table, findings and a
classification A (sufficient) / B (with compromises) / C (investigate Plasma
integration) with evidence. Result: B.
</details>

<details>
<summary>M1.1 — CPU-driven runner</summary>

Approved post-spike decision: curve `fps = round(10 + 30·(cpu/100)²)`
(not the linear upstream curve, not a plain 20 FPS cap); keep separate
CPU measurement → smoothing → mapping → animation timer → runner. Tasks: git
init + .gitignore without commit (later superseded: git removed); `/proc/stat`
delta with user/nice/system/idle/iowait/irq/softirq/steal, nice active, no
99.9 cap, clear active/idle/total, iowait attention, public 0–100; sampling
not tied to FPS; simple smoothing (EMA or small average) against 18↔19 flicker
with documented parameters; mapping as independent tested function (min 10,
max 40, monotonic, clamp); animation continues from current frame, no
flicker, no per-tick resource rebuild; only the M1.0 cat; optional `--fps N`
diagnostic override; tests for parsing, deltas (idle/full/partial/nice/iowait/
first/zero/regression), mapping table, smoothing; real idle/load/recovery test
with documented load command and no leftover processes; debug output of raw
CPU / smoothed / target FPS without spam; docs/M1-cpu-runner.md; update
requirements only for the curve.
</details>

<details>
<summary>M1.2 — Cleanup & Freeze</summary>

Not a feature milestone. Confirm iowait = idle (CPU-work indicator; disk/
network pressure belongs to other metrics). Keep normal curve. Record future
slow-down = mirrored curve `round(10 + 30·(1 − cpu/100)²)` in requirements
without implementing. Separate spike-only tools from permanent `tools/`;
create `tools/test-cpu-fps-curve.sh` (uses real app `--fps`, 0→100→0 without
repeating 100, default 5 s/step, `--duration`, finds binary without personal
paths, precise PID, cleanup on interruption, no pkill, clear error if binary
missing, plain Bash). Don't compensate the external crash-loop noise. Don't
rewrite historical reports. Verify normal build excludes spike and spike
builds with AQTRUNCAT_BUILD_SPIKES=ON. First commit (done then; repo later
deleted by the user).
</details>

<details>
<summary>M2 — Runner System</summary>

Goal: the core knows only "Runner"; the cat is just the bundled default.
At M2 time: don't break M1 (CPU, curve, timers; slow-down was not implemented). Generic
minimal Runner model (id, name, frames, order, dimensions, metadata, source,
monochrome/colour if useful) without deep OOP; separation Runner → Loader →
Animation → Tray; remove every cat assumption; Neo/Gallery-compatible PNG
format; deterministic natural ordering (not runcat-tray's bug); robust
validation distinguishing fatal from harmless extras; no width==height
assumption, preserve aspect, don't fix very wide runners; generic bundled
default mechanism; licensing/provenance for copied assets; bundled + XDG user
discovery testable without the real home; simple deterministic precedence
(user > bundled) with a collision test; diagnostic `--runner` (prefer no
silent fallback) and optional `--list-runners`; Gallery structural
compatibility only (no network/HTTP/download); ZIP import only after the
directory loader, separated extraction/validation/installation, no path
traversal, stop and ask before adding a significant dependency (→ led to
libarchive decision in M2.1); XDG separation; frames prepared once; runner
switch without restarting the engine; tests for loader/discovery/animation/
CLI; manual Plasma tests (default, forced runner, --fps, tools) and
multi-runner visual test; optional cycle tool; docs/M2-runner-system.md.
Screenshots must not include personal tray info.
</details>

<details>
<summary>M2.1 — Runner Lifecycle + CLI Foundation</summary>

Complete safe ZIP import, user runner lifecycle, and make the CLI an official
structured interface; GUI and CLI must share the same services (RunnerCatalog/
Loader/Importer/Installer/Remover), no business logic in CLI parsing. Grammar
`aqtruncat` = `aqtruncat run`, `run [--runner] [--fps]`, `runner list|import
FILE.zip|remove ID`; prefer sub-commands over `--x-runner` options; keep M1/M2
forms as documented aliases; per-command help explaining purpose, short;
simple documented exit codes (usage / not found / invalid / filesystem),
errors to stderr. **libarchive mandatory** (no Qt private ZIP, custom parser,
unzip/bsdtar, KArchive), clean CMake detection with a clear error. Untrusted
archive: reject traversal, absolute paths, symlinks/hardlinks, special files;
entry/total/per-file limits not ridiculously low vs Gallery; temp extraction,
no partial installs, cleanup. Support the real Gallery layout, fail on
ambiguous/multi-runner archives. Installer service (validated runner, XDG
destination, no partial installs, collisions): bundled collision = explicit
override; user collision = fail, optional safe `--force`. Remover: user only,
bundled protected, unknown error, ambiguous not guessed, override removal
restores bundled (tested). `runner list` structured (not concatenation in
main.cpp). No preview, no extra bundled runners. Tests for archives (valid,
malformed, security, limits, nothing written outside temp), lifecycle,
CLI parsing/dispatch; manual Plasma tests incl. real Gallery import → run →
remove; import performance; docs; requirements (CLI, sub-commands, lifecycle,
libarchive, safe extraction, removal, shared services). M2 complete criteria
met. No git.
</details>

<details>
<summary>M3 — Core Metrics + CLI</summary>

Linux-native metrics layer (CPU, memory, network, disk/storage) reusable by
runner, Dashboard, Metrics Bar, CLI. Don't rewrite or duplicate the M1 CPU
pipeline (no two diverging CPU implementations). Typed snapshots (not a single
generic type), explicit units, raw values separate from formatting, no CLI
logic in the model. CPU usage % with approved semantics (logical count /
per-core optional; per-core must share semantics). Memory from /proc/meminfo,
used = total − available, documented fallback without MemAvailable, swap
separate. Network from /proc/net/dev (+ /sys), rate = Δbytes / actual elapsed;
no hard-coded names; sensible interface policy (exclude lo, document
inclusion/exclusion and double-counting risk, keep per-interface stats);
handle first sample, zero Δ, resets, appearing/disappearing interfaces, zero/
invalid elapsed. Storage usage (statvfs, documented free vs available, at
least `/`, extensible to more mounts) distinct from disk activity (/proc,/sys,
rates, documented device policy for physical/partition/NVMe/dm/loop/zram, stop
before fragile heuristics). Common sampling layer without per-number timers,
~1 s, real elapsed time, no UI coupling. Error model: available / temporarily
unavailable / parse error / unsupported; 0 ≠ unknown. Formatter separate,
binary units. CLI `aqtruncat metrics` (compact report) and optional `--watch`
(Ctrl+C), no JSON. No Dashboard/Metrics Bar/Custom Metrics. Tests: memory,
network, storage, disk activity, formatting boundaries, integration fixtures
→ providers → snapshot → CLI formatter; keep all earlier tests. Runtime
validation vs free/df/ip//proc with controlled activity; measure sampling
cost; optional `tools/test-metrics-runtime.sh`; Linux portability; docs/
M3-core-metrics.md; requirements for normative decisions; stop conditions for
network aggregation, disk aggregation and filesystem semantics (handled as
standard Linux choices: hardware NICs, whole physical disks, df semantics).
</details>

<details>
<summary>F2 revision (2026-10-20) — GUI Integration, Usage Quotas, Installer Lifecycle</summary>

Read requirements/MEMORY/F2 first; keep F1 and the adapter. (1) Claude Code
as an official integration in a Settings **Integrations** section distinct
from Custom Metrics: enable/disable, availability/error, short description,
project/session option only where relevant; no need for the CLI `enable`, CLI
kept; GUI and CLI share one configuration; still F1-based, no second engine;
never overwrite/remove user metrics. (2) Primary values = `/usage` quotas:
Current session (5-hour rolling window) and Current week (all models), %
used (remaining derivable), reset date/time, two Dashboard progress bars;
never confuse with context window, token consumption, session duration; no
quotas from context tokens; context window may stay optional/secondary.
(3) Reinvestigate read-only sources (documented interfaces, status line,
local caches, CLI, others); no invented values, no fragile screen scraping,
no token-consuming polling, no unnecessary secrets; status-line integration
only explicit/opt-in, never silently modify `~/.claude/settings.json` or
overwrite an existing status line, document restoring; if no reliable
source, report and ask before invasive changes. (4) Use F1 `output:
"snapshot"`, separate normalized ranges, independent bars, local-time
resets, missing resets not invented; compact Metrics Bar with per-quota
selection via the smallest generic extension, no Claude code in generic
Dashboard/Metrics Bar. (5) States: available, temporarily unavailable, no
Claude, no session, missing/expired cache, partial, source/auth failure,
reset changes, 100%; never 0% for unavailable, no indefinitely stale values,
show age; keep F1 no-flicker. (6) install.sh stops the running AQTRunCat of
the installation/user before replacing: SIGTERM, bounded wait, verify exit,
never kill unrelated processes or by broad name, never Claude Code/Codex,
fail clearly if it does not exit, preflight before shutdown, `--dry-run`
shows only, `--uninstall` lifecycle, system and `--local`, no restart unless
specified, no hidden privilege escalation, other users not silently
terminated, documented. (7) Tests for all of it with synthetic fixtures;
clean build, full suite; Plasma/Wayland runtime with isolated config;
compare with `/usage` at about the same moment, no unverified claims.
(8) Update requirements, MEMORY, F2 doc, README, installer docs. (9) No Git
writes. (10) Report the 9 points, then STOP; do not start F3.
</details>
