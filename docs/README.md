# AQTRunCat Documentation

Technical documentation of AQTRunCat **2.1.3**, by topic. Users start with
the main [README](../README.md); the v2 roadmap is
[requirements.md](../requirements.md). The topic documents are design and
validation records written in Italian; [feature-reference.md](feature-reference.md)
(English) summarises the normative behaviour of the 1.x features.

## Architecture in brief

```text
            aqtruncatd  (headless backend, one per session, D-Bus activated)
            ├── MetricSampler → SystemMetricsSampler (/proc, /sys, statvfs, DRM, NVML, MangoHud log)
            ├── UpdatesService (bounded read-only package/Flatpak checks)
            ├── CustomMetricsService (snapshot files, command sources, Claude Code / Codex adapters)
            ├── CpuMonitor → RunnerSpeedController (runner fps)
            ├── AppSettings (shared INI) + AutostartManager
            └── D-Bus API io.github.psykedady.AQTRunCat1 (GetState, deltas, GetRunners, settings, integrations)
                 │
   ┌─────────────┼───────────────────────┬──────────────────────────┐
aqtruncat (Qt tray)          KDE Plasma widget (QML)        aqtruncatctl / any client
runner animation, tooltip,   runner, tooltip, popup,        status, metrics, watch,
Dashboard, Settings,         configuration pages            settings, integrations
Metrics Bar
```

Static libraries: `aqtruncat_core` (CPU, smoothing, speed curve),
`aqtruncat_runner` (runner format, loader, import), `aqtruncat_metrics`
(Qt-free metric sources and formatting), `aqtruncat_engine` (settings,
sampler, Custom Metrics, updates), `aqtruncat_api` (schema, backend
service, client), `aqtruncat_desktop` (Widgets UI). Details: `MEMORY.md` §3.

## Topics

| Topic | Document |
|---|---|
| Backend, D-Bus API (complete contract), `aqtruncatctl`, Python client example | [backend-dbus-api-cli.md](backend-dbus-api-cli.md) |
| CPU measurement, smoothing and runner speed curve | [cpu-and-runner-speed.md](cpu-and-runner-speed.md) |
| Runner format, discovery, precedence | [runners.md](runners.md) |
| Runner ZIP import/removal and the `aqtruncat` command line | [runner-import-and-cli.md](runner-import-and-cli.md) |
| Built-in runners Walking Goose, Party Parrot, Engine, Rickroll (assets, build integration) | [built-in-runners.md](built-in-runners.md) |
| System metrics (CPU, memory, swap, network, storage, disk I/O) | [system-metrics.md](system-metrics.md) |
| Uptime, GPU, FPS, package/Flatpak updates, kernel, distribution | [additional-built-in-metrics.md](additional-built-in-metrics.md) |
| Dashboard | [dashboard.md](dashboard.md) |
| Settings, autostart, desktop integration | [settings-and-desktop-integration.md](settings-and-desktop-integration.md) |
| Custom Metrics: RunCat Neo-compatible snapshots | [custom-metrics.md](custom-metrics.md) |
| Custom Metrics: command sources, packages, icons | [custom-metrics-commands.md](custom-metrics-commands.md) |
| Claude Code integration | [integration-claude-code.md](integration-claude-code.md) |
| Codex integration | [integration-codex.md](integration-codex.md) |
| Metrics Bar | [metrics-bar.md](metrics-bar.md) |
| Hover information (tooltips) | [hover-information.md](hover-information.md) |
| KDE Plasma widget | [plasma-widget.md](plasma-widget.md) |
| Desktop compatibility (Plasma, GNOME, XFCE, …) | [desktop-compatibility.md](desktop-compatibility.md) |
| Installation and packaging | [installation-and-packaging.md](installation-and-packaging.md) |
| RunCat Neo upstream analysis (compatibility reference) | [upstream-runcat-neo-analysis.md](upstream-runcat-neo-analysis.md) |
| M1.0 tray spike (prototype kept behind `-DAQTRUNCAT_BUILD_SPIKES=ON`) | [tray-spike.md](tray-spike.md) |
| Normative summary of features F1–F7 | [feature-reference.md](feature-reference.md) |

Runner assets and attribution: [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
and the README of each `third_party/` folder.

## Milestone names

Commit messages, code comments and the documents themselves refer to the
1.x development milestones. They map to the documents above:

| Milestone | Release | Document |
|---|---|---|
| M0 upstream analysis | 1.0.0 | upstream-runcat-neo-analysis.md |
| M1 / M1.0 CPU runner, tray spike | 1.0.0 | cpu-and-runner-speed.md, tray-spike.md |
| M2 / M2.1 runner system, lifecycle, CLI | 1.0.0 | runners.md, runner-import-and-cli.md |
| M3 core metrics | 1.0.0 | system-metrics.md |
| M4 Dashboard | 1.0.0 | dashboard.md |
| M5 Settings and desktop integration | 1.0.0 | settings-and-desktop-integration.md |
| M6 Custom Metrics | 1.0.0 | custom-metrics.md |
| M7 Metrics Bar | 1.0.0 | metrics-bar.md |
| M8 desktop compatibility | 1.0.0 | desktop-compatibility.md |
| M9 packaging and installation | 1.0.0 | installation-and-packaging.md |
| F1 Custom Metrics v2 | 1.1.0 | custom-metrics-commands.md |
| F2 Claude Code | 1.2.0 | integration-claude-code.md |
| F3 Codex | 1.3.0 | integration-codex.md |
| F4.1 backend, D-Bus API, CLI | 1.4.1 | backend-dbus-api-cli.md |
| F4.2 Plasma widget | 1.4.2 | plasma-widget.md |
| F5 additional built-in metrics | 1.5.6 | additional-built-in-metrics.md |
| F6 hover information | 1.6.3 | hover-information.md |
| F7 built-in runners | 1.7.4 | built-in-runners.md |

2.0.0 consolidated 1.7.4 without functional changes.

| v2 milestone | Version | Document |
|---|---|---|
| v2F1.1 Rickroll runner (with the 2.1.0 baseline) | 2.1.1 | built-in-runners.md §10 |
| v2F1.2 optional Custom Metrics `enabled` | 2.1.2 | custom-metrics-commands.md §8 |
| v2F1.3 Custom Metric execution warnings | 2.1.3 | custom-metrics-commands.md §9 |
