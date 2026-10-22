# AQTRunCat — Post-Project Features

This document defines the AQTRunCat feature roadmap after completion of the
M0–M9 project roadmap. M0–M9 are complete and remain the historical and
architectural foundation for this work.

F1, F2, F3, F4 (F4.1 and F4.2), F5 and F6 are complete. F7 is not
started; recording it here does not imply that it is currently available or
supported.

The post-project roadmap is:

    F1  Custom Metrics v2 / Executable Custom Metrics
    F2  Claude Code Metric
    F3  Codex Metric
    F4  Backend Independence (F4.1) and KDE Plasma Plasmoid (F4.2)
    F5  Additional Built-in Metrics
    F6  Hover Information
    F7  Additional Built-in Animated Runners


## Development Baseline and Branching

Post-project development starts from the dedicated branch:

    feature/base

`feature/base` is the common logical baseline for the post-project roadmap.
Individual feature branches are created from that baseline and use names such
as:

    feature/f1
    feature/f2
    feature/f3

The slash in these names is only part of the Git branch name. It does not
create or imply a Git branch hierarchy. `feature/base` is the logical/common
parent because the feature branches are created from its commit history.

Branch creation, switching, merging and release sequencing are operational
actions and happen only when explicitly requested; this document defines the
development model but does not perform those actions.


## Versioning

The AQTRunCat version is `project(VERSION)` in `CMakeLists.txt`; the package
recipes (PKGBUILD, RPM spec, Debian changelog) and the man page must carry the
same value (checked by the `packaging-versions` test). Every executable prints
it with `-v` / `--version`, and the tray menus show it.

Rule (user decision, 2026-10-21):

- `1.0.0` is the completed M0–M9 project roadmap.
- Each completed post-project feature raises the **minor** version:
  F1 → 1.1.0, F2 → 1.2.0, F3 → 1.3.0, F4 → 1.4.x, F5 → 1.5.x,
  F6 → 1.6.x (F6.1–F6.3 → 1.6.3), …
- Each completed sub-feature raises the **patch** version within its feature:
  F4.1 → 1.4.1, F4.2 → 1.4.2; F5.1 → 1.5.1, and so on.
- After the seventh feature (F7) the version moves to **2.0.0** (confirmed
  by the user, 2026-10-21).

Current version: **1.5.6** (F5 complete: F5.1–F5.6, 2026-10-22). Earlier states were never
released under their own number (the project stayed at 0.0.1 until F4.1).


## F1 — Custom Metrics v2 / Executable Custom Metrics

Extend Custom Metrics so that a metric may either consume an externally
maintained snapshot or actively obtain its value by executing a configured
program.

The intended source model is:

    Custom Metrics
    ├── Snapshot source
    │   └── JSON values produced and updated externally
    └── Command source
        └── AQTRunCat periodically executes a configured executable and
            obtains the metric value from its output


### Relationship to M6

M6 implemented Custom Metrics as consumers of externally generated,
RunCat Neo-compatible JSON snapshots. AQTRunCat does not execute commands for
those M6 snapshot sources. That statement remains historically correct and
continues to describe the currently implemented snapshot mechanism.

F1 deliberately extends that design. It does not rewrite M6 history and does
not remove or deprecate snapshot-backed metrics. Snapshot-backed and
command-backed sources coexist behind the common Custom Metric model.

An existing snapshot must never be reinterpreted implicitly as an executable
command configuration. A command source must be identified explicitly as
such.


### Implemented Configuration

A command-backed configuration is identified only by an explicit
`source.type` of `command` and supports:

```json
{
  "id": "my-metric",
  "title": "My Metric",
  "symbol": {
    "type": "theme",
    "value": "some-icon"
  },
  "source": {
    "type": "command",
    "executable": "/path/to/executable",
    "arguments": [],
    "intervalSeconds": 5,
    "timeoutSeconds": 10
  },
  "display": {
    "prefix": "",
    "suffix": " units",
    "normalizedRange": { "minimum": 0, "maximum": 100 }
  }
}
```

`id` and `title` are required. `id` is 1–100 characters and matches
`[A-Za-z0-9][A-Za-z0-9._-]{0,99}`. `arguments` defaults to an empty array and
is limited to 256 bounded strings. `intervalSeconds` defaults to and has a
minimum of 1 second, with a maximum of 86400. `timeoutSeconds` defaults to 10
seconds and is limited to 1–300. `display`, `prefix`, `suffix` and
`normalizedRange` are optional; a normalized range requires finite
`minimum < maximum` and numeric command output.

The process working directory is the configuration file's directory.
Relative executable and file-icon paths are resolved against that directory.
Automatic discovery keeps the M6 standalone `metrics/*.json` files and adds
one package level: `metrics/<package>/metric.json`. Explicit registration
accepts either source type.


### Command Execution

AQTRunCat periodically executes the configured program and uses its output to
update the corresponding Custom Metric.

Normal execution must use an executable plus an argument array. AQTRunCat must
not insert an implicit shell or treat a free-form command string as shell
syntax. Users who need pipes, redirection, expansion or other complex
processing can explicitly configure their own script as the executable.

The implemented execution model provides:

- a configurable refresh interval per metric;
- a bounded process timeout;
- non-zero exit status;
- missing or non-executable programs;
- invalid output;
- bounded output size;
- prevention of overlapping executions of the same metric;
- cleanup or termination of owned processes during AQTRunCat shutdown;
- unavailable and error states;
- avoidance of unnecessary polling and expensive execution.

Command-backed metrics must integrate with AQTRunCat's existing metric state
semantics where applicable rather than creating an unrelated status system.
A missing, failed or malformed command source must affect only its own metric.
It must not silently become a valid zero value or bring down the application.

The scheduler supports different intervals for different Custom Metrics.
Command-backed metrics are not required to run at the one-second CPU sampling
frequency. A cheap local metric may refresh frequently, while an update check
may need a much slower cadence. The implementation should share scheduling
infrastructure where practical without creating one timer or sampler per
displayed number unnecessarily.

One shared monotonic scheduler drives all command sources. A source runs once
immediately, then its next run is scheduled from completion; it never overlaps
with itself and missed intervals are not replayed. Missing/non-executable
programs are Unavailable. Startup failure, crash, non-zero exit, timeout and
invalid output are Error. Pending with no result shows the configuration
skeleton; Pending during refresh retains the last valid value. Each command
runs as the leader of its own process group: timeout and output overflow kill
the whole group, and shutdown first terminates and then forcibly kills every
still-running owned group with bounded total waits, so helpers spawned by a
script are cleaned up as well.

The F1 output contract is deliberately scalar: stdout must contain exactly
one non-empty UTF-8 line after outer whitespace is trimmed. `prefix` and
`suffix` form both the displayed value and `metricsBarValue`. Structured or
multi-value command output is deferred; an external producer can continue to
use the M6 snapshot contract for that case. Stdout and stderr are independently
limited to 16 KiB. Stderr is diagnostic only. Command sources retry at their
normal interval; there is no additional backoff in F1.

Commands execute directly as `executable + arguments[]`, with the same user
privileges and environment as AQTRunCat. AQTRunCat uses no implicit shell and
performs no privilege escalation. Only a fully valid, explicitly typed
command configuration may launch a process.


### Symbols and Icons

Custom Metric symbols must support both named theme icons and filesystem icon
paths.

Theme icon example:

```json
{
  "symbol": {
    "type": "theme",
    "value": "staroflife"
  }
}
```

File icon example:

```json
{
  "symbol": {
    "type": "file",
    "value": "/path/to/icon.svg"
  }
}
```

Filesystem paths may be absolute or relative. A relative icon path must be
resolved relative to the Custom Metric configuration file that contains it,
never relative to AQTRunCat's process working directory. This permits
self-contained packages such as:

    claude/
    ├── metric.json
    ├── claude-metric
    └── icons/
        └── claude.svg

A missing, unreadable, unsupported or invalid icon must degrade gracefully to
an appropriate fallback and must not make the metric itself unusable.

F1 retains the existing snapshot string symbol representation. Structured
symbols map onto the shared Custom Metric model and are available to both
source types. Theme icons use the installed Qt theme; file icons use formats
supported by the installed Qt image plugins. Invalid icons use a fallback.


### Compatibility

Existing externally updated JSON snapshot Custom Metrics remain supported.
Their file watching, atomic-replace handling, last-known-good behaviour,
registration/discovery and isolation guarantees remain relevant unless an F1
change explicitly extends them without breaking compatibility.

F1 adds a second source model; it does not replace the M6 snapshot
architecture. Snapshot-backed and command-backed metrics share presentation,
state and model infrastructure while keeping acquisition separate. The final
architecture and validation record are documented in
`docs/F1-custom-metrics-v2.md`.


## F2 — Claude Code Metric

Add an officially supported Claude Code metric.

The Claude Code JSON shown in existing documentation is an example of the M6
snapshot format produced by an external program. It is not evidence of an
already implemented Claude Code monitor in AQTRunCat.

F2 must first investigate which Claude Code data sources and values can be
obtained reliably. The exact values to expose are decided from that evidence,
not assumed in advance.

Use the generic Custom Metrics v2 infrastructure from F1 wherever practical.
Do not add unnecessary Claude-specific monitoring infrastructure to the
AQTRunCat core. If a small provider-specific adapter or executable is needed,
it should feed the generic metric infrastructure and keep provider acquisition
logic isolated.

### F2 Revision (2026-10-20): Integration, Usage Quotas, Installer Lifecycle

After the first implementation the user asked for a final functional revision:
Claude Code must be an official integration managed from Settings, its
primary values must be the two plan-usage quotas of Claude Code's `/usage`
screen (current 5-hour session window and current week, all models) with
reset times, not the context window, and `install.sh` must close a running
AQTRunCat before replacing or removing it. The context window may remain as
an optional secondary metric. The record of the revision is in
`docs/F2-claude-code-metric.md`.

### Implemented Behaviour (F2 complete, revised 2026-10-20)

The investigation, the evaluated sources and the validation record are in
`docs/F2-claude-code-metric.md`. The following decisions are normative.

Architecture:

- Claude Code acquisition lives only in the separate QtCore executable
  `aqtruncat-claude-metric` (library `aqtruncat_claude`). The AQTRunCat core
  does not link it and knows no Claude format; it runs the adapter as an
  ordinary F1 command source (`"output": "snapshot"`, interval 10 s, timeout
  5 s). There is no Claude-specific scheduler, watcher or presentation.
- Generic, provider-independent, backward-compatible F1/M6 extensions:
  - `source.output` = `"scalar"` (default) or `"snapshot"` (stdout is one M6
    snapshot object, ≤ 16 KiB; the configuration owns title and symbol;
    `display` is rejected), and exit status 69 (`EX_UNAVAILABLE`) mapping to
    Unavailable with stderr as the message;
  - optional snapshot item fields `id` (`[A-Za-z0-9][A-Za-z0-9._-]{0,63}`,
    unique in the snapshot), `detail` (secondary line shown under the value
    in the Dashboard), `metricsBarValue` and `metricsBarLabel` (the item's own
    compact value and short caption);
  - Metrics Bar ids `custom-item:<item id>:<source id>` select one item of a
    multi-value source; an item is selectable with an `id` and a nonempty
    `metricsBarValue`, and is shown with its caption instead of the source
    icon. An item without a compact value is Unavailable, never empty or 0.
    Settings offers such a source per item. `custom:<source id>` is
    unchanged.

Official integrations vs Custom Metrics:

- Provenance is explicit registration metadata, never inferred from title,
  symbol or path: an optional top-level `"integration": "<id>"` field
  (same syntax as `id`) of a **command** configuration, written by the
  provider's adapter (`claude-code`), parsed into
  `CommandDefinition::integration` and carried as `Source::integration`.
  Empty = a user-defined Custom Metric. M6 snapshot files can never be
  integrations (the key is ignored there). Presentation only: execution,
  scheduling, states and the Metrics Bar are unchanged.
- The Dashboard shows **Integrations** (sources with provenance) before
  **Custom Metrics** (all others), each with the same card renderer, each
  source exactly once; an empty section is hidden with its heading (no "No
  custom metrics" placeholder any more). A user metric with the same title
  as a provider stays under Custom Metrics.
- Settings has an **Integrations** section, separate from the user's Custom
  Metrics. For Claude Code: enable/disable, availability and error status,
  a short description, live updates (below) and the optional context window
  with an optional project directory.
- The adapter owns the configuration: the metric package
  `$XDG_DATA_HOME/AQTRunCat/metrics/claude-code/` (written by `enable`,
  removed by `disable`) and the live-updates backup. The Settings window runs
  the adapter's own commands (`status --json`, `enable [--context [--cwd
  DIR]]`, `disable`, `live-updates on|off`) and watches the files they report,
  so GUI and CLI always show the same state. The package is listed only under
  Integrations; the Metrics Bar list labels its entries "Integration". A
  package written before the provenance field existed (`status --json`
  `integrationMarked: false`) is rewritten with its own options by
  `aqtruncat-claude-metric upgrade`, which `install.sh` runs as the real user
  after every installation (no-op when the integration is off; never
  creates, deletes or touches a user-managed package; failure is only a
  warning), and once by the Settings window as a fallback. The installer
  itself never deletes anything in the user's data directory. Packages not written by the adapter are never replaced or
  removed (`enable --force` only on explicit request).

Usage quotas (primary values):

- Current session (5h) and Current week (all models): percentage used, a
  progress bar each (normalized value = percentage / 100) and the reset time
  in the local time zone ("Resets at 13:30" today, "Resets Oct 23, 20:00"
  otherwise). ≥ 100% is shown as "… — limit reached". Metrics Bar values
  `3%` / `<1%` with captions `5h` / `7d`; nothing is added to the bar
  automatically. A "Usage data" item names the source in use.
- Sources (local, read-only, no network, no tokens, no credentials):
  1. Claude Code's own usage cache `cachedUsageUtilization` in
     `~/.claude.json` (`$CLAUDE_CONFIG_DIR/.claude.json` when set): what
     `/usage` last fetched, with its fetch time; ignored when it belongs to
     another account than the signed-in one. Claude Code refreshes it only
     when `/usage` or an IDE usage view is opened. Internal format.
  2. Opt-in live updates: the documented status-line `rate_limits` captured
     by `aqtruncat-claude-metric statusline` into
     `$XDG_RUNTIME_DIR/aqtruncat/claude-rate-limits.json` (0600, only the two
     percentages, reset times and capture times).
  For each window the most recent report wins.
- Freshness: a report older than 5 minutes is shown with "as of <time>"; a
  report from before its window's reset is not shown ("The window reset at
  …; no newer report yet"); one older than 24 hours is not shown. A missing
  window is "Not reported" with what to do. Unavailable usage is never 0%.
- States: Claude Code absent, or no usage report at all → Unavailable (exit
  69); unreadable or unsupported cache → Error (exit 1); one window only →
  Available with the other "Not reported".
- Quotas are never derived from context-window tokens; no screen scraping;
  no polling that consumes tokens.

Live updates (opt-in, explicit confirmation in Settings or `live-updates
on`):

- Sets only the `statusLine` member of Claude Code's user `settings.json`,
  edited textually and verified to differ from the original in that member
  only (refused, file untouched, if the file is not a strict JSON object or
  has duplicate keys). A previous `statusLine` is backed up (value and exact
  text) in `$XDG_DATA_HOME/AQTRunCat/integrations/claude-code/statusline-backup.json`
  and keeps running: the helper replaces itself with `/bin/sh -c <previous
  command>` fed with the identical input. `live-updates off` and `disable`
  restore it byte for byte; a `statusLine` the user changed meanwhile is left
  alone.

Optional context window (`--context`): the previous F2 metric (input +
cache tokens of the latest main-conversation response of the most recently
active running session / 200 000 or 1 000 000 for `[1m]`), plus model,
session and running sessions; no session or unreadable data is reported in
its items, never by failing the quota snapshot.

Installer lifecycle (`install.sh`):

- Before replacing (install/update) or removing (uninstall) files, the
  installer closes this user's processes whose `/proc/<pid>/exe` is exactly
  the installation's `bin/aqtruncat` (or the same path "(deleted)"), with
  SIGTERM, waiting up to 10 s (start time checked against PID reuse,
  zombies count as exited). No name matching decides anything; nothing else
  is signalled; never SIGKILL. Not exiting in time → error, nothing changed.
  All preflight steps (build, staging, manifest checks, `sudo -v`) run
  before closing it. It is not restarted. `--dry-run` lists what would be
  closed. Under `sudo` other users' instances are reported, not stopped.
- Build (user request 2026-10-20): every installation still builds from
  scratch (Release, no tests), now in the persistent
  `${XDG_CACHE_HOME:-~/.cache}/aqtruncat/install-build` (emptied only if it
  carries the installer's marker or is empty; root uses root's cache).
  `--no-build` installs that build without compiling, only if it exists,
  comes from this source tree, is Release and is up to date (build tool dry
  run); otherwise error and nothing installed. `--build-dir=DIR` builds all
  targets (previously only `aqtruncat`).


## F3 — Codex Metric

Add an officially supported Codex metric using the same architectural
principles as F2.

F3 must investigate which Codex usage, context or status information can be
obtained reliably and expose useful values through the generic Custom Metrics
v2 infrastructure wherever practical.

Claude Code and Codex monitoring must coexist. Provider-specific acquisition
may differ, but common scheduling, state, configuration and presentation
concepts should not be duplicated unnecessarily.

### Implemented Behaviour (F3 complete, 2026-10-20)

Record, investigation and validation: `docs/F3-codex-metric.md`. Normative:

- Official integration `codex`, adapter `aqtruncat-codex-metric` (library
  `aqtruncat_codex`, QtCore only, not linked by the core), run as an F1
  command (`output: "snapshot"`, interval 15 s, timeout 30 s), package
  `$XDG_DATA_HOME/AQTRunCat/metrics/codex/` with `"integration": "codex"`
  (Dashboard › Integrations, never Custom Metrics). Shared provider code in
  `src/providers/common/` (`aqtruncat_provider_common`: quota judgement and
  item format, package helpers), also used by Claude Code without changing
  its output.
- Primary values: Codex usage limits of the `codex` limit bucket (other
  buckets ignored; no `limit_id` = `codex`), one item per reported window,
  labelled and identified by its length (`window_minutes` /
  `windowDurationMins`: 300 → "5h limit"/`window_300m`/`cx5h`, 10080 →
  "Weekly limit"/`window_10080m`/`cx7d`, others "Daily", "N-day", "Nh",
  "N-minute"), never by position: percentage used, remaining
  (`100 − used`), reset in the local time zone, progress bar; "limit
  reached" at ≥ 100 % and from `rate_limit_reached_type` /
  `ordinaryUsageAllowed: false`. Never the context window, token counts,
  API billing or credits.
- Sources (user decision: hybrid): default = the newest `token_count`
  `rate_limits` in `$CODEX_HOME` (or `~/.codex`) `sessions/**/rollout-*.jsonl`
  and `archived_sessions/` (newest 8 files by modification time, tails up to
  8 MiB; only lines with `"token_count"` and `"rate_limits"` are parsed);
  opt-in live updates (Settings checkbox with confirmation, or
  `live-updates on`, recorded only in the package as `--live`) = `codex
  app-server` over stdio, `initialize` → `account/rateLimits/read`
  (`excludeResetCreditDetails`), at most every 5 minutes, failures retried
  after 60 s, cached in `$XDG_RUNTIME_DIR/aqtruncat/codex-rate-limits.json`
  (0600; no account id; bound to the Codex home). Newest report per window
  wins. Codex configuration and credentials are never read or written by
  AQTRunCat; no prompt, no usage consumed.
- Freshness: > 10 min "as of"; past reset or > 24 h not shown ("Out of
  date", warning); never 0 for missing.
- Warnings (both integrations, user request 2026-10-20): snapshot items may
  carry `"warning": true` (generic, optional, boolean), shown by the
  Dashboard with the theme's warning icon; out-of-date windows are "Out of
  date" and missing ones "Not reported", both warnings; with live updates
  off and a window not current, the "Usage data" item is a warning that
  says when values change and where to turn live updates on. Codex absent or no report → Unavailable (69);
  unreadable data or failed live read without any data → Error (1).
- Settings › Integrations: Codex panel (enable, status, live updates,
  note), independent from Claude Code; GUI and CLI (`enable [--live]`,
  `disable`, `upgrade`, `status --json`, `live-updates on|off`) share the
  package. Metrics Bar: per-window items, captions `cx…`, nothing added
  automatically, 3-item limit unchanged.
- Installation: installed by all mechanisms (CMake install; RPM `%files`);
  `install.sh` runs `upgrade` for both integrations; Codex optional at
  runtime.


## F4 — Backend Independence and KDE Plasma Plasmoid

F4 is divided into two parts:

- **F4.1 — Headless Backend, Frontend Independence, Local API & CLI**
  (complete): AQTRunCat becomes a reusable local metric service with one
  authoritative backend per session, a documented IPC API and a reference
  CLI.
- **F4.2 — Native KDE Plasma Plasmoid** (complete): a Plasma frontend that
  consumes the F4.1 API.

F4.1 is a mandatory prerequisite for F4.2.


### F4.1 — Headless Backend, Local API & CLI

Make the AQTRunCat metric engine independent of any graphical frontend and
publish it through a local, versioned IPC API, so that the Qt/SNI tray, the
future Plasmoid, a CLI and third-party (GTK, Python, Rust, TUI, Waybar)
clients consume the same authoritative state. The backend must run without
Qt Widgets, a system tray, KDE or a graphical session; all frontends share
one sampler, one Custom Metrics service, one execution of the Claude/Codex
adapters and one configuration store.

#### Implemented Behaviour (F4.1 complete, 2026-10-21)

Record, API contract and validation: `docs/F4.1-headless-backend-ipc-cli.md`
(with a Qt-free Python client example). Normative:

- Process model (option B): a dedicated backend `aqtruncatd` (QtCore +
  QtDBus only) owns MetricSampler, CustomMetricsService with the F2/F3
  adapters, CpuMonitor → RunnerSpeedController, AppSettings, autostart and
  official-integration management. `aqtruncat` / `aqtruncat run` is the
  Qt/SNI frontend and a client of the backend; `aqtruncatctl` is the
  reference CLI. Libraries: `aqtruncat_engine` (QtCore), `aqtruncat_api`
  (QtCore + QtDBus: schema, BackendService, BackendClient),
  `aqtruncat_desktop` (Widgets, UI only).
- Lifecycle: single instance through the session-bus name
  `io.github.psykedady.AQTRunCat` (never queued, never replaced; a second
  backend exits 0). Activation by a D-Bus service file
  (`share/dbus-1/services/io.github.psykedady.AQTRunCat.service`, `Exec` =
  the installed `aqtruncatd`); fallback: the frontend and the CLI start the
  `aqtruncatd` next to themselves (detached; the frontend at most every
  10 s). systemd is not required. The backend stops on SIGTERM/SIGINT or at
  the end of the session, not when the last client leaves. Closing a
  frontend never stops the backend; clients recover after a backend restart
  (3 s grace without flicker, then an explicit unavailable state, retry
  every 3 s).
- API: object `/io/github/psykedady/AQTRunCat`, interface
  `io.github.psykedady.AQTRunCat1` (major version in the name), `api` 1,
  `schema` 1, `capabilities` list. Methods `GetInfo`, `GetState`,
  `GetMetrics`, `GetRunner`, `GetSettings`, `GetIntegrations`,
  `SetSetting(key, jsonValue)`, `RegisterCustomMetric`,
  `UnregisterCustomMetric`, `SetIntegrationEnabled`,
  `SetIntegrationLiveUpdates`; signals `MetricsChanged(generation, changes)`,
  `RunnerChanged`, `SettingsChanged`, `IntegrationsChanged`. Payloads are
  compact JSON strings; errors `…AQTRunCat1.Error.InvalidArgument`,
  `.NotFound`, `.Failed`. Additive changes raise `schema` only; clients
  ignore unknown fields.
- Metric schema: `id`, `category` (`builtin` | `integration` | `custom`),
  `kind` (`system` | `snapshot` | `command`), `integration`, `title`,
  `state` (`ok` | `pending` | `unavailable` | `error`), `message`, `value`
  (null when absent, never an invented 0), `unit`, `formattedValue`,
  `detail`, `compactValue`, `compactLabel`, `normalized`, `updatedAt`,
  `icon`, `items[]` (multi-value metrics such as quota windows, with
  `warning`) and `data` (raw values). Built-in ids `cpu`, `memory`, `swap`,
  `network.download|upload`, `storage.root`, `storage.home` (separate
  filesystem only), `disk.read|write`; others `custom:<canonical path>`,
  the Metrics Bar ids.
- Updates: complete state with `GetState`, then coalesced deltas carrying
  only changed metrics, removed ids, the full order and `sampleTime`
  (CLOCK_MONOTONIC seconds of the latest system sample), with a generation
  incremented by one per delta; a client that misses a generation or sees a
  new name owner takes the whole state again. One sampler for any number of
  clients; no per-frame IPC (frontends animate locally at the `fps`
  received).
- Settings: one store, the existing `AQTRunCat.ini` (no migration). The
  backend applies effects and watches the file, so changes from the API, the
  CLI, the Qt Settings window or `aqtruncat autostart` reach every client
  through `SettingsChanged`. Writable keys with validation: `runnerId`,
  `speedMode`, `networkMode`, `networkInterfaces`,
  `dashboardRefreshSeconds`, `autostart`, `metricsBarEnabled`,
  `metricsBarItems`; `customMetricFiles` only through the register methods.
- CLI `aqtruncatctl [--json] [--no-start]`: `status`, `metrics [ID...]
  [--category …]`, `watch`, `integrations`, `integration enable|disable
  ID`, `integration live-updates ID on|off`, `settings`, `settings set KEY
  VALUE`, `custom add|remove PATH`. Exit codes 0 success, 1 backend
  failure, 2 usage, 3 backend unavailable, 4 request rejected.
- Qt/SNI frontend: Dashboard, Metrics Bar, Settings and the runner speed come
  from the backend; the frontend no longer samples, runs Custom Metrics or
  adapters, computes the speed or manages autostart. Visual behaviour and
  configuration semantics are unchanged; `--fps` stays a local override.
  The Dashboard refresh cadence (1/2/5 s) uses the backend's sample time and
  tolerates the sampler's timer jitter (50 ms), so a 1 s setting no longer
  skips periods (a pre-existing M5 defect found during F4.1 validation).
- Security: session bus only (same-user policy), no network, no privilege
  change, no method executing client-supplied commands (integration methods
  take a known id and fixed adapter arguments), no credentials, tokens,
  conversation contents, provider files or environment exposed.
- Installation: CMake install, `install.sh` (system and `--local`),
  PKGBUILD (`dbus`), Debian (`default-dbus-session-bus |
  dbus-session-bus`) and RPM (`%files`, `Requires: dbus`) ship
  `aqtruncatd`, `aqtruncatctl` and the activation file. The installer also
  closes this installation's running `aqtruncatd` (exact path, SIGTERM, as
  for the tray) and asks the session bus to `ReloadConfig` after install and
  uninstall. Autostart still starts the frontend, which brings the backend;
  existing entries remain valid.
- Tests: `tst_apischema` and `tst_backendipc` (isolated `dbus-run-session`
  and XDG directories: registration, version, single instance, deltas,
  classification, settings both ways, shared backend for frontend and
  clients, frontend exit, backend restart and recovery, start fallback, CLI
  text/JSON/exit codes, D-Bus activation), installer checks for the new
  files; complete suite 42/42.
- Known limitations: dbus-broker needs `ReloadConfig` (requested by the
  installer) or the start fallback; the backend stays until logout; the Qt
  Settings › Integrations panel still runs the adapters directly (the
  adapter remains the configuration owner, the backend observes it);
  `updatedAt` is null for system metrics; native packages were updated and
  checked as a `/usr` install layout, not rebuilt in distribution
  containers.


### F4.2 — Native KDE Plasma Plasmoid

Provide a native KDE Plasma Plasmoid in parallel with the generic Qt/SNI
application, as a client of the F4.1 backend API.

The generic application remains the portable, supported baseline on Plasma
and other Linux desktops. The Plasmoid is additive and must not replace or
disable it.

Both frontends should reuse AQTRunCat core functionality, metrics, settings,
runner state, models, configuration and application logic where practical.
Business logic must not be duplicated merely to support the Plasma frontend.


#### F4.2.1 — Installation

Integrate Plasmoid installation into the existing `install.sh` workflow.

Expected behaviour:

- Plasma detected, no explicit Plasmoid option: install the generic
  application and the Plasmoid.
- Plasma detected with `--no-plasmoid`: install the generic application only.
- Plasma not detected, no explicit Plasmoid option: install the generic
  application only.
- Plasma not detected with `--plasmoid`: explicitly request Plasmoid
  installation, subject to compatibility and dependency checks.

Both options must be documented by `install.sh --help`:

    ./install.sh --no-plasmoid
    ./install.sh --plasmoid

The existing installer guarantees remain in force, including:

- system-wide installation;
- `--local` installation;
- safe uninstall;
- manifest-based ownership where applicable;
- preservation of user data;
- autostart behaviour.

`--local` must install the Plasmoid into appropriate user-local locations when
selected. Uninstall must remove a Plasmoid only when it belongs to the
AQTRunCat installation being removed, and must not remove unrelated widgets or
user data.


#### F4.2.2 — Plasma Integration

The Plasmoid must behave like a native Plasma widget instead of reproducing
Plasma behaviour with generic Qt windows. Opening it should provide a native
popup experience comparable in behaviour and integration to standard Plasma
widgets such as the Network/Wi-Fi Plasmoid.

It should integrate naturally with:

- the Plasma panel;
- native popup and hover behaviour;
- Plasma themes;
- panel sizing;
- panel orientation where practical;
- Plasma configuration conventions.

The generic Qt/SNI frontend must remain fully supported when the Plasmoid is
installed.


#### Implemented Behaviour (F4.2 complete, 2026-10-21)

Record, design and validation: `docs/F4.2-plasma-plasmoid.md`. Normative:

- Form: a **QML-only Plasma 6 package** (`plasmoid/package`, id
  `io.github.psykedady.aqtruncat`, `KPackageStructure` `Plasma/Applet`,
  `X-Plasma-API-Minimum-Version` 6.0) installed to
  `share/plasma/plasmoids/io.github.psykedady.aqtruncat/`. No native code
  in plasmashell and no build dependency; it works from `/usr`,
  `/usr/local` and `--local` alike (a C++ QML/applet plugin would not be
  found by plasmashell outside Qt's own directories).
- Frontend only: no sampling, no provider files, no command execution, no
  scheduler, no settings store of its own. `BackendClient.qml` talks to the
  F4.1 API with Plasma's QML D-Bus module
  (`org.kde.plasma.workspace.dbus`): asynchronous calls only, D-Bus
  activation, signal deltas by generation (a gap or a lower generation
  takes `GetState`), 3 s grace then "unavailable", retries at 3 s doubling
  to 30 s, "incompatible" for another major API, degraded mode for a
  backend without the `runners` capability. With the popup closed metric
  deltas are not parsed; opening it takes `GetMetrics`.
- API extension (compatible, schema 1): `GetRunners` with capability
  `runners` — runner catalogue with absolute frame files, playback
  `sequence`, `monochrome`, frame size, validity and the `selected` runner
  (same fallback as the tray). Bundled runners are also installed as files
  in `share/aqtruncat/runners/<id>/`. The catalogue lives in
  `aqtruncat_runner_catalog` (QtGui for PNG validation), linked only by
  `aqtruncatd` and the tray; `aqtruncat_api` and `aqtruncatctl` stay
  QtCore + QtDBus. `aqtruncatd` still needs no graphical session.
- Compact representation: the runner only, sized from the panel thickness
  and the runner's aspect ratio (horizontal and vertical panels, desktop),
  transparent; frames painted once per size/colour/screen scale into
  Canvases and switched by a local timer at the backend's fps; monochrome
  runners tinted with the theme's text colour (source-in, as the tray).
- Expanded representation: a Plasma popup (`PlasmaExtras.Representation`,
  list with section headers) with System, Integrations and Custom Metrics
  by the backend's `category` only; values, details, bars only with a
  normalized value, multi-value items (Claude/Codex windows) with reset
  lines and warnings; pending/unavailable/error/warning shown as such,
  never as zero or as an invented percentage.
- Configuration: page "AQTRunCat" changes the shared settings through the
  API (runner, speed, network, integrations and live updates, Custom
  Metrics files), immediately, kept in sync by `SettingsChanged`; page
  "Appearance" holds widget-only options in Plasma's configuration
  (`animate`, `showBuiltin`, `showIntegrations`, `showCustom`,
  `showDetails`). Start at login, Metrics Bar and Dashboard refresh remain
  tray settings.
- Coexistence: tray and widget are independent clients of one backend;
  installing or adding either never enables the other; autostart concerns
  the tray only.
- Installation (F4.2.1): `install.sh` installs the widget when Plasma 6's
  QML modules are found (none needed elsewhere), `--no-plasmoid` skips or
  removes it, `--plasmoid` requests it and fails with the missing modules
  and the distribution's package command when they are absent; system,
  `--local`, dry run, manifest ownership, safe uninstall (only its own
  files and empty directories), `--no-build` and the version question are
  preserved. The installer never adds the widget to a panel, never edits
  Plasma's configuration and never restarts plasmashell; it warns when a
  system prefix is not in `XDG_DATA_DIRS`.
- Packaging: part of the main package, weak dependency on
  `plasma-workspace` (PKGBUILD optdepends, RPM/Debian Suggests).
- Tests: `plasmoidlogic` (qmltestrunner), `plasmoidclient` (the real
  `BackendClient.qml` against the real backend on a private bus with
  activation), `plasmoidpackage` (metadata, structure, configuration,
  frontend-only rules, loading in `plasmoidviewer` in horizontal and
  vertical panels and on the desktop), `backendipc` (`GetRunners`),
  installer and `packaging-versions` checks.
- Runtime: validated on Plasma 6.8 / Wayland in `plasmoidviewer` windows
  on a private bus (loading, signals, one backend with two widgets and the
  tray, settings and runner changes, backend restart, CPU); adding the
  widget to the user's real panel and opening the popup by click are left
  for manual verification (the user's session was not modified); the user
  then ran the widget in the real Plasma session, found it OK and closed F4
  on 2026-10-21.


## F5 — Additional Built-in Metrics

Status: COMPLETE (2026-10-22, version 1.5.6). Record, investigation and
validation: `docs/F5-additional-built-in-metrics.md`; normative summary in
"Implemented Behaviour (F5 complete)" below.

Evaluate and add additional built-in metrics:

- system uptime;
- GPU utilization and useful related GPU information;
- FPS/frame-rate information, particularly for gaming;
- available system/package updates;
- available Flatpak updates;
- kernel information;
- Linux distribution information.

Each metric must integrate with the existing AQTRunCat metric architecture
and preserve the distinction between valid, pending, unavailable and error
where applicable. Failure of one source must not affect unrelated metrics.


### F5.1 — Uptime

Provide system uptime information. Prefer a direct Linux/system interface over
invoking an external command where practical.


### F5.2 — GPU Metrics

Provide useful GPU information where supported, potentially including
utilization, VRAM usage, temperature and other relevant device information.

GPU acquisition differs across vendors, drivers and device configurations.
F5 must investigate those differences, isolate provider-specific acquisition
behind common metric concepts where practical, and degrade gracefully on
unsupported hardware or unavailable values.


### F5.3 — FPS Metrics

Investigate frame-rate monitoring before selecting an implementation. FPS
availability may depend on the graphics stack, compositor, game environment
or optional external interfaces. Monitoring must not introduce unreasonable
overhead merely to provide the metric.


### F5.4 — System Updates

Provide information about available system/package updates. Account for
different Linux distribution and package-manager families, and isolate
distribution-specific acquisition behind a common metric interface where
practical. A missing, unsupported or failing package manager must not affect
unrelated metrics.


### F5.5 — Flatpak Updates

Provide available Flatpak update information when Flatpak is installed and
available. Flatpak integration is optional; systems without Flatpak must
continue to work normally.


### F5.6 — Kernel and Distribution Information

Kernel information and Linux distribution information are separate concepts
and must remain separate values.

Do not assume that one distribution-identification source exists everywhere.
Appropriate sources and fallbacks may include:

- native kernel/system interfaces;
- information equivalent to `uname`;
- `/etc/os-release` when available;
- distribution-specific release information;
- other appropriate standard fallbacks.

Prefer direct/native interfaces over spawning commands where practical.
Unknown information must be represented as unavailable/unknown rather than
causing a metric or application failure.


### Implemented Behaviour (F5 complete, 2026-10-22)

- All F5 metrics are **built-in** metrics (`category: builtin`,
  `kind: system`): sections of the one `SystemMetricsSnapshot`, published by
  the one backend, read by the Qt/SNI frontend, `aqtruncatctl` and the
  Plasmoid through the existing API. API schema **2** (additive; `api` 1).
  Ids: `uptime`, `gpu:<device id>` (one per GPU, PCI slot, ordered by id),
  `fps`, `updates.packages`, `updates.flatpak`, `kernel`, `distribution`;
  units `seconds`, `fps`, `count`; `kernel`/`distribution` have no `value`
  (text). Every field keeps ok / pending / unavailable / error; missing
  values are `null`, never 0; counts, durations and text are never
  percentages.
- **Uptime**: `/proc/uptime` (system, not process), each sample; days,
  hours, minutes.
- **GPU**: DRM discovery (connectors, firmware framebuffers and non-display
  PCI classes skipped); amdgpu sysfs (busy %, VRAM, hwmon temperature);
  Intel i915/xe *activity* = time out of RC6 (no privileges needed; not
  per-engine utilization); NVIDIA through NVML loaded at run time with
  `dlopen` (no build or runtime dependency); other drivers: hwmon
  temperature only. Per-field availability; integrated Intel GPUs report no
  VRAM or temperature of their own. A runtime-suspended GPU is never read.
  Shared setting `gpuDevice` (`auto` = first discrete, else first) selects
  the GPU of the Metrics Bar entries `gpu.utilization`, `gpu.memory`,
  `gpu.temperature`.
- **FPS**: only measured values from the CSV log MangoHud appends while
  logging (`output_folder` in `~/.config/MangoHud/*.conf`); conditions
  measured (≤ 3 s), stale (≤ 60 s, no current value), inactive, missing,
  not-configured, disabled, unsupported. Never inferred from GPU/CPU load or
  refresh rate; no injection, no overlay requirement. Shared setting
  `fpsSource` (`mangohud` default, `off`).
- **Package updates**: manager detected from what is installed (pacman,
  apt, dnf; os-release only breaks ties). Read-only commands with local
  metadata by default (`pacman -Qu`, `apt-get -s … dist-upgrade`,
  `dnf -C check-update`); opt-in remote refresh (`updatesRemoteRefresh`:
  `checkupdates` for pacman, `dnf --refresh`; apt impossible without root),
  at most hourly. Count, manager, last successful check, metadata age
  (stale after 7 days), errors as errors.
- **Flatpak updates**: `flatpak remote-ls --updates --cached` per
  installation (user, system, `installations.d`), apps and runtimes; count =
  distinct refs per installation, summed; any failing installation makes
  the metric an error; no Flatpak → unavailable.
- **Kernel / distribution**: `uname(2)`; os-release parsed as data (never
  executed), fallbacks `/usr/lib/os-release`, `lsb-release`,
  distribution-specific files; re-read every 10 minutes.
- **Scheduling**: uptime, GPU and FPS with the 1 Hz system sample; update
  checks in the backend's `UpdatesService`, one non-overlapping bounded
  command per job (direct executable, fixed arguments, own process group,
  C locale, stdin `/dev/null`, 120 s timeout, 4 MiB output limit), first
  check 30 s after start, then `updatesIntervalMinutes` (60 default; 0 =
  off) after the previous one finished, plus one debounced re-check when the
  package databases change. Never sudo/pkexec, never install/upgrade/remove,
  no silent network access (verified with strace).
- **Frontends**: Qt Dashboard sections GPU, Gaming, Updates, System; the
  Plasmoid presents them generically under "Built-in Metrics" and offers
  the four shared settings; Metrics Bar ids `uptime`, `gpu.*`, `fps`,
  `updates.packages`, `updates.flatpak`, none selected by default, the
  3-item limit unchanged, kernel/distribution not offered.
- **Packaging**: no new mandatory dependency; weak dependencies hwdata /
  pci.ids, flatpak, pacman-contrib (Arch), mangohud.


## F6 — Hover Information

Status: COMPLETE (2026-10-22, version 1.6.3: F6.1 tray, F6.2 Plasmoid,
F6.3 metric-specific details). Record and validation:
`docs/F6-hover-information.md`; normative summary in "Implemented
Behaviour (F6 complete)" below.

Provide compact information when the pointer hovers over an AQTRunCat
tray/applet item. At minimum, expose:

- CPU usage;
- memory usage.

The information must be available without opening the Dashboard.

The generic Qt/SNI frontend and the Plasma frontend should expose equivalent
information where their APIs support it, while following their native
presentation conventions:

- generic frontend: normal tray/SNI tooltip behaviour;
- Plasma frontend: native Plasma hover/tooltip behaviour.

Reuse the already sampled CPU and memory information. Do not create another
polling mechanism solely for hover information.

### Implemented Behaviour (F6 complete)

- **Summary** (both frontends, same text, the backend's formatting):
  `CPU: 18.4 %`, `RAM: 46.5 % · 7.2 GiB / 15.5 GiB used`,
  `Swap: 2.4 % · 300.0 MiB / 12.0 GiB`; no Swap line without swap; a
  missing value is `waiting for data`, `unavailable` or `error`, never 0 and
  never the backend's technical message; `Connecting to the backend…` /
  `Backend not running` instead of values when the backend is not there
  (after the client's 3 s grace period).
- **F6.1 Qt/SNI tray**: the runner item's tooltip is `AQTRunCat — <runner>`
  followed by the summary, built from the snapshot the tray already
  receives (`src/app/hoversummary.*`); sent only when the text changes,
  together with the next animation frame (250 ms fallback); menu,
  activation, animation and Settings unchanged; no extra tray item.
- **F6.2 Plasmoid**: Plasma's own applet tooltip (`toolTipSubText`, plain
  text) shows the runner and the summary (`logic.js hoverLines/hoverText`,
  from the API's `formattedValue`/`detail`); the widget follows the metrics
  only while the pointer or keyboard focus is on the runner (or the popup
  is open) and shows values only while they are current.
- **F6.3 details**: Metrics Bar tooltips add slowly changing details from
  the snapshot (logical CPUs, available memory, used/total storage, counted
  interfaces/disks, GPU driver/type, FPS source, package manager, last
  successful update check, stale metadata); Plasmoid popup rows show their
  details on hover when the widget's detail lines are off. The Dashboard is
  unchanged.
- **Constraints kept**: no new sampler, collector, command or timer for
  hover; API 1 / schema 2 unchanged; backend sampling frequency unchanged.

Completion criteria (met): equivalent summary in the tray and the
Plasmoid, unavailable/disconnected states without stale or zero values,
automated tests for formatting, missing values, disconnection, rounding,
optional details, the Plasmoid's hover lifecycle without extra sampling and
the identical output of the two implementations on one backend payload;
clean build without warnings and the whole test suite passing.


## F7 — Additional Built-in Animated Runners

Status: NOT STARTED — BLOCKED on the final animation assets of the goose
and the parrot. F7 is the last
planned feature milestone before 2.0.0. Requirements recorded on
2026-10-21 and restored in full on 2026-10-22 (documentation only: nothing
is implemented, no asset exists in the repository).

Add three official built-in runners, shipped with AQTRunCat like Cat and
Coffee:

- Walking Goose (new artwork);
- Crazy Dancing Parrot (new artwork);
- Engine (RunCat Neo's `engine` runner, already in the repository; added
  to F7 by the user on 2026-10-22).

They are built-in runners, separate from user (custom) runners, and follow
the existing runner format, selection and presentation in every frontend
(tray, Plasmoid, `GetRunners`).

### Proposed Sub-features

These are documentation milestones, not completed implementations:

    F7.1  Walking Goose
    F7.2  Crazy Dancing Parrot
    F7.3  Engine (promote the existing RunCat Neo frames to a built-in runner)
    F7.4  Shared asset integration, packaging and validation

The sections after F7.3 (asset preparation, architecture, settings,
installation) are requirements shared by all three runners and belong to
F7.4.

### F7.1 — Walking Goose

- A cartoon goose walking, as a frame-based animation in the existing
  runner format, with a recognizable walking cycle.
- Transparent-background PNG frames; a simple, readable style suitable for
  a small tray icon or Plasma panel.
- Supports the existing runner animation controls where applicable (speed
  following CPU load, Normal/Slow-down, the widget's "Animate the runner"
  option, fixed `--fps`).
- Integrated in the existing built-in runner selection; works in the Qt
  system tray and in the KDE Plasma 6 Plasmoid.

### F7.2 — Crazy Dancing Parrot

- A colourful cartoon parrot dancing energetically, as a frame-based
  animation with transparent-background PNG frames and a lively, looping
  dance cycle.
- Readable at small icon sizes; consistent style and proportions across
  frames.
- Integrated in the existing built-in runner selection; works in the Qt
  system tray and in the Plasmoid; existing animation behaviour and
  controls preserved.

### F7.3 — Engine

- RunCat Neo's `engine` runner (a piston and crank: 10 frames, 83×36 px,
  monochrome/template), one of RunCat Neo's eight built-in runners.
- Its frames are already in the repository, unmodified:
  `third_party/runcatneo/runners/engine/engine-frame-{0..9}.png` (Apache-2.0,
  recorded in `THIRD_PARTY_NOTICES.md`, which today says they are used only
  by the M1.0 tray spike). It is **not blocked** by the asset dependency
  below and needs no new artwork.
- Becomes an official built-in runner like Cat and Coffee: an AQTRunCat
  `data/runners/engine/metadata.json` (`displayName` "Engine", `author`
  Kyome22, `type` `monochrome`, as for Cat and Coffee), the id `engine`
  added to `AQTRUNCAT_BUNDLED_RUNNERS`, installed under
  `share/aqtruncat/runners/engine/`; `THIRD_PARTY_NOTICES.md` updated to
  the new use; the files stay byte-for-byte copies.
- Known limit to validate, not to work around silently: the frame is wide
  (aspect ≈ 2.3:1). Plasma's square tray slot shrinks it to about 22×10 px
  (`docs/M0-upstream-analysis.md`, `docs/M1-tray-spike.md`,
  `docs/M2-runner-system.md` §11: "at the limit" of readability); horizontal and
  vertical Plasmoid panels keep its aspect ratio. Any change to the frames
  (cropping the empty left margin, rescaling) is a modification of
  third-party files and needs the user's approval and a note in
  `THIRD_PARTY_NOTICES.md`.
- An existing user runner with the id `engine` keeps precedence (user >
  bundled), as for every built-in runner.

### Asset Preparation and Integration (F7.1, F7.2; part of F7.4)

The final graphical assets of the goose and the parrot are **not available
yet** (Engine's frames exist, see F7.3). The user provides or
approves the definitive animation frames before implementation.

Expected asset characteristics:

- PNG, transparent background;
- about 4–8 frames per runner as an initial target, adjustable for the
  quality of the animation;
- one canvas size and a consistent alignment within each runner;
- a limited, readable palette; no background or decorative elements;
- a smooth loop.

Rules until then:

- no substitute or generated graphics;
- no temporary placeholder sprites presented as final assets;
- no third-party sprites downloaded without the user's explicit approval
  (any approved third-party asset is recorded in `THIRD_PARTY_NOTICES.md`
  with repository, commit, original path and licence, and must be
  compatible with Apache-2.0);
- F7.1 and F7.2 are not ready for implementation until the assets are
  available.

When the assets are supplied, inspect the existing runner asset pipeline
and adapt them to its conventions (the ones Engine already follows): frame
files `<id>-frame-<n>.png`, a
`metadata.json` with `displayName`, `author` and `type` (`monochrome`
frames are tinted with the panel's text colour, `color` frames are shown
unchanged — a colourful parrot needs `color`), the runner id added to
`AQTRUNCAT_BUNDLED_RUNNERS` (compiled into `aqtruncat` and installed as
files under `share/aqtruncat/runners/<id>/` for the backend's `GetRunners`
and the Plasmoid). Proposed ids `goose` and `parrot` (to be confirmed with
the assets).

### Architecture and Compatibility (F7.4)

- Official built-in runners: not Custom Metrics, not user (custom) runners,
  not external plugins.
- Reuse the existing runner model, loader, animator, settings and selection
  (`RunnerService`, `RunnerAnimator`, the shared `runnerId` setting, the
  start-up runner fallback); no new animation engine unless demonstrably
  necessary.
- Cat stays the default runner; Cat, Coffee, user runners and the user >
  bundled precedence keep their behaviour.
- The backend lists them through the existing runner catalogue
  (`GetRunners`, capability `runners`); the Qt frontend and the Plasmoid
  discover and render them the same way. D-Bus API compatibility preserved
  (no new method is expected).
- No change to the metric sampling frequency; no extra CPU, memory or
  wake-up cost beyond that of an equivalent runner (frames prepared once,
  animation timer as today).

### Settings and User Experience (F7.4)

- Walking Goose, Crazy Dancing Parrot and Engine appear in the existing runner
  selectors (tray Settings, Plasmoid configuration) and in `aqtruncat
  runner list`, with the same recognizable names everywhere;
  `aqtruncatctl status` shows the selected id as for any runner.
- Selecting one changes the animation through the existing behaviour; the
  choice persists in the existing settings and across restarts.
- Correct rendering in horizontal and vertical Plasma panels and in the
  tray (aspect ratio, light/dark themes), with the existing scaling and
  animation settings.

### Installation, Packaging and Tests (F7.4)

When F7 is implemented:

- the assets are part of every supported installation (system-wide and
  `--local` `install.sh`, native packages) and available to the Plasmoid
  through the backend's runner catalogue, as for Cat and Coffee;
- installer, uninstall, dry run, cached build and `--no-build` behaviour
  unchanged;
- missing or invalid assets fail gracefully (the existing runner
  validation and fallback: a saved runner that is missing falls back to the
  default), never a crash;
- automated tests for discovery, asset availability, selection,
  persistence and packaging; Qt tray and Plasma rendering validated; clean
  build and the relevant suites passing.

### Dependencies

- The final frames of the goose and the parrot, provided or approved by
  the user (blocking for F7.1, F7.2 and therefore for completing F7).
  Engine (F7.3) has its frames and could be implemented on its own, but
  only on the user's explicit instruction.
- The licence and attribution of the assets (the user's own work, or
  third-party assets approved by the user and recorded in
  `THIRD_PARTY_NOTICES.md`).

### Acceptance Criteria

F7 is complete only when:

1. the goose and the parrot ship with the final, user-approved assets, and
   Engine with the unmodified RunCat Neo frames (or user-approved changes);
2. they are listed, selectable and persistent in the tray and the
   Plasmoid (and listed by `aqtruncat runner list`), with the same names;
3. they render correctly in the tray and in horizontal/vertical Plasma
   panels, in light and dark themes, at the existing speeds (for Engine:
   tray readability at its wide aspect checked and reported);
4. Cat (default), Coffee and user runners behave as before;
5. installation, packaging and uninstallation include and remove them like
   the existing built-in runners;
6. the automated tests above pass with a clean build without warnings.

### Versioning

By the rule above the sub-features would raise the patch within 1.7
(F7.1 → 1.7.1 … F7.4 → 1.7.4), and 2.0.0 follows once F7 is
implemented and validated. Whether the completed F7 is first released as
1.7.x or directly as 2.0.0 is left to the user. The current version is not
changed by recording F7.


## General Principles for Post-Project Features

Future development must preserve the principles established during M0–M9:

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

13. Existing M0–M9 behaviour and compatibility contracts remain in force
    unless a feature explicitly and deliberately extends them, as F1 does for
    the M6 Custom Metrics acquisition model.

14. Features in this document are future work. Their presence here does not
    imply that they are currently implemented or supported.
