# AQTRunCat — Post-Project Features

This document defines the AQTRunCat feature roadmap after completion of the
M0–M9 project roadmap. M0–M9 are complete and remain the historical and
architectural foundation for this work.

F1, F2 and F3 are complete. F4–F6 are not started; recording them here does
not imply that they are currently available or supported.

The post-project roadmap is:

    F1  Custom Metrics v2 / Executable Custom Metrics
    F2  Claude Code Metric
    F3  Codex Metric
    F4  KDE Plasma Plasmoid
    F5  Additional Built-in Metrics
    F6  Hover Information


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


## F4 — KDE Plasma Plasmoid

Provide a native KDE Plasma Plasmoid in parallel with the generic Qt/SNI
application.

The generic application remains the portable, supported baseline on Plasma
and other Linux desktops. The Plasmoid is additive and must not replace or
disable it.

Both frontends should reuse AQTRunCat core functionality, metrics, settings,
runner state, models, configuration and application logic where practical.
Business logic must not be duplicated merely to support the Plasma frontend.


### F4.1 — Installation

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


### F4.2 — Plasma Integration

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


## F5 — Additional Built-in Metrics

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


## F6 — Hover Information

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
