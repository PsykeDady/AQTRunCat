# AQTRunCat

**A Cute Run Cat for Linux.** A small animated runner lives in your system
tray and runs faster as your CPU gets busier. Click it for a Dashboard of
system metrics; add snapshot- or command-backed Custom Metrics; optionally show
up to three short values right in the tray with the Metrics Bar.

AQTRunCat is a Qt 6 application for Linux, inspired by
[RunCat Neo](https://github.com/runcat-dev/RunCatNeo) for macOS and compatible
with its runner and Custom Metrics formats. It is an independent project, not
affiliated with RunCat Neo. It uses the
standard StatusNotifierItem tray, so it needs no desktop-specific components;
the primary target is KDE Plasma 6 on Wayland.

![Runner and Metrics Bar in the Plasma tray](docs/M7-metrics-bar/metrics-bar-basic.png)

![Settings and Dashboard](docs/M8-desktop-compatibility/windows-100.png)

## Features

- **CPU-driven runner**: 10 FPS when idle, up to 40 FPS at full load (or the
  reverse with "Slow down under load"). Monochrome runners follow the panel
  text colour in light and dark themes.
- **Runners**: Cat (default) and Coffee are built in; import any
  [RunCat Runner Gallery](https://github.com/runcat-dev/RunnerGallery) ZIP.
- **Dashboard**: CPU, memory, swap, network, Root/Home storage and disk
  activity, read directly from `/proc`, `/sys` and `statvfs`.
- **Custom Metrics**: keep using RunCat Neo-compatible JSON snapshots, or let
  AQTRunCat periodically run an explicitly configured executable.
- **Metrics Bar** (off by default): up to 3 compact values, built-in or
  custom, as extra tray icons next to the runner.
- **Settings** window, start at login, and a command line (`aqtruncat --help`).

## Supported systems

| Distribution | Status |
|---|---|
| Arch Linux | supported: package (PKGBUILD) or installer; build, tests and desktop validated |
| Fedora 44 | supported: RPM or installer; build, tests and package validated |
| Debian 13, Ubuntu 26.04 LTS | supported: `.deb` or installer; build, tests and package validated |
| Ubuntu 24.04 LTS | **not supported** (Qt 6.4, see below) |

Desktops: KDE Plasma 6 Wayland is the reference. Other trays implementing
StatusNotifierItem/AppIndicator (validated: waybar) and classic X11 trays
(validated: stalonetray) work with the limitations listed below. Pure GNOME
has no tray; AQTRunCat keeps running but is not visible there without an
AppIndicator extension.

> [!WARNING]
> **Ubuntu 24.04 LTS is not supported.** It ships Qt 6.4.2, while AQTRunCat
> requires **Qt 6.5 or newer**; CMake stops with "Could not find a
> configuration file for package Qt6 that is compatible with requested version
> 6.5" and the installer reports the same. Qt 6.5 is the oldest Qt 6
> long-term-support release AQTRunCat builds and tests against. Use Ubuntu
> 26.04 LTS or newer, or another distribution with Qt ≥ 6.5.

## Install

### With the installer (any supported distribution)

From the source tree, as your normal user:

```bash
./install.sh            # system-wide into /usr/local, start at login
./install.sh --local    # only for you, no root: ~/.local/bin, ~/.local/share
```

The installer checks the build requirements (and prints the exact package
command for Arch, Fedora or Debian/Ubuntu if something is missing), builds a
Release binary in a temporary directory and installs it. A system-wide install
uses `sudo` only to copy the files; `--local` never uses it. Running it again
updates the installation in place.

| Option | Meaning |
|---|---|
| *(none)* | install system-wide (`--prefix=DIR`, default `/usr/local`) |
| `--local` | install for the current user (XDG directories, no root) |
| `-A`, `--autostart=true` | start at login (**the default**) |
| `-na`, `--autostart=false` | do not start at login |
| `-U`, `--uninstall` | uninstall (add `--local` for a local installation) |
| `--dry-run` | show what would happen |
| `-h`, `--help` | full help |

Start at login is a per-user preference, also for a system-wide install: it is
the XDG entry `~/.config/autostart/aqtruncat.desktop`, the same one the
Settings window and `aqtruncat autostart enable|disable` manage.

Uninstalling removes exactly the files the installer recorded, plus your
autostart entry if it starts that installation. **Your settings
(`~/.config/AQTRunCat`), user runners and Custom Metrics
(`~/.local/share/AQTRunCat`) are always kept.**

```bash
./install.sh --local -U        # remove the --local installation
./install.sh -U                # remove the system-wide installation
```

If `~/.local/bin` is not in your `PATH`, the installer says so; the menu
entry and autostart use the full path, and your shell files are never edited.

### As a distribution package

Recipes for native packages are in [`packaging/`](packaging/):

| Format | Build |
|---|---|
| Arch (`PKGBUILD`) | `packaging/make-source-tarball.sh packaging/arch && cd packaging/arch && makepkg -si` |
| Fedora (RPM) | `packaging/make-source-tarball.sh ~/rpmbuild/SOURCES && rpmbuild -ba packaging/rpm/aqtruncat.spec` |
| Debian/Ubuntu (`.deb`) | extract the source tarball, copy `packaging/debian` to `debian/`, run `dpkg-buildpackage -us -uc -b` |

Flatpak and AppImage are not provided yet; the reasons are in
[docs/M9-packaging-installation.md](docs/M9-packaging-installation.md).

### Build manually

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt ≥ 6.5 (Widgets; Test for
the test suite), libarchive ≥ 3.0, and an installed font.

| Distribution | Development packages |
|---|---|
| Arch Linux | `cmake ninja gcc qt6-base qt6-wayland libarchive` |
| Fedora | `cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtwayland libarchive-devel` |
| Debian 13 / Ubuntu 26.04 | `cmake ninja-build g++ qt6-base-dev qt6-wayland libarchive-dev libgl-dev` |

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
cmake --install build            # honours CMAKE_INSTALL_PREFIX and DESTDIR
```

`-DBUILD_TESTING=OFF` skips the tests. At run time AQTRunCat needs the Qt 6
libraries, libarchive and, for native Wayland windows, the Qt Wayland
plugin (`qt6-wayland` / `qt6-qtwayland`); without it Qt uses XWayland.

## Using AQTRunCat

Start it from the application menu or with `aqtruncat`. Left-click the
runner to show or hide the Dashboard; right-click for Settings and Quit.

```bash
aqtruncat                       # start the tray runner
aqtruncat runner list           # bundled and user runners
aqtruncat runner import beagle-frames.zip
aqtruncat metrics --watch       # metrics on the terminal
aqtruncat autostart status      # enable | disable | status
```

### Runners

Bundled runners: Cat and Coffee. Import a Runner Gallery `*-frames.zip` with
`aqtruncat runner import FILE.zip` (validated and extracted safely; nothing is
installed if anything is wrong) or put a folder of PNG frames into
`~/.local/share/AQTRunCat/runners/<id>/`. Choose the runner in Settings.

### Custom Metrics

AQTRunCat supports two source types. Existing RunCat Neo-compatible snapshot
files remain unchanged: register one in Settings, or drop it into
`~/.local/share/AQTRunCat/metrics/` to have it discovered:

```json
{
  "title": "Claude Code",
  "symbol": "staroflife",
  "metricsBarValue": "5.4%",
  "metrics": [ { "title": "Context", "formattedValue": "5.4%", "normalizedValue": 0.054 } ],
  "lastUpdatedDate": "2026-06-05T04:50:40Z"
}
```

The Claude Code object above is only an example snapshot. AQTRunCat does not
include a Claude Code monitor; an external producer must create and update the
file. The future Claude feature is separate from the generic command support.

Files are watched: updates (including atomic replace) appear immediately,
and a broken or missing file only affects its own card.

A command-backed metric uses an explicit executable and argument array—never
an implicit shell—and reads one UTF-8 scalar line from standard output:

```json
{
  "id": "example-temperature",
  "title": "Temperature",
  "symbol": { "type": "file", "value": "./icons/temperature.svg" },
  "source": {
    "type": "command",
    "executable": "./read-temperature",
    "arguments": [],
    "intervalSeconds": 5,
    "timeoutSeconds": 3
  },
  "display": { "suffix": " °C" }
}
```

Put this at, for example,
`~/.local/share/AQTRunCat/metrics/temperature/metric.json`; the relative
executable and icon are resolved from that package directory. Commands run as
the AQTRunCat user, without privilege escalation. See
[docs/F1-custom-metrics-v2.md](docs/F1-custom-metrics-v2.md) for the complete
schema, limits, states and security model.

### Metrics Bar

Enable it in Settings and choose up to **three** metrics (CPU, memory, swap,
network down/up, Root/Home storage, disk read/write, or a Custom Metric with
a `metricsBarValue`). Each one is a small tray icon next to the runner; hover
it for the full value. The runner itself is always there and is not counted.

## Known limitations

- Tray icons are square on Plasma: runners wider than about 2:1 look small,
  and a Metrics Bar icon fits about four characters (the tooltip has the rest).
- All AQTRunCat tray icons share one identity, so the tray's own per-icon
  visibility settings apply to all of them; their order is decided by the
  tray (it can change after the tray service restarts).
- On non-KDE desktops the tray text colour follows the reported light/dark
  scheme, but trays whose panel colour differs from that scheme (for example
  GNOME's always-dark top bar) can show low contrast.
- Wayland does not let applications position windows next to the tray; the
  Dashboard and Settings are normal windows placed by the compositor.

Details: [docs/M8-desktop-compatibility.md](docs/M8-desktop-compatibility.md).

## License and attribution

AQTRunCat is licensed under the [Apache License 2.0](LICENSE). The bundled
Cat and Coffee runner frames come unmodified from
[RunCat Neo](https://github.com/runcat-dev/RunCatNeo) (Apache-2.0, © Kyome22); see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The RunCat
and RunCat Neo names and icon are not used as AQTRunCat branding; the
AQTRunCat icon is original artwork.

## Documentation

- [requirements.md](requirements.md): specification and roadmap
- [docs/](docs/): design and validation reports per milestone (runners,
  metrics, Dashboard, Settings, Custom Metrics, Metrics Bar, desktop
  compatibility, packaging, executable Custom Metrics)
- [`tools/`](tools/): diagnostic scripts for developers (not needed by users)
