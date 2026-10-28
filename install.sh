#!/usr/bin/env bash
# AQTRunCat installer: build from this source tree and install system-wide or
# for the current user only. Run "./install.sh --help" for the full usage.
#
# What gets installed is decided by CMake ("cmake --install" into a staging
# directory); this script only maps the staged bin/ and share/ trees to their
# destinations, records an install manifest and uses the application itself
# ("aqtruncat autostart enable|disable") for the per-user autostart entry.

set -Eeuo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
readonly SCRIPT_DIR
readonly MANIFEST_HEADER="# AQTRunCat install manifest"

usage() {
    cat <<'EOF'
AQTRunCat installer - A Cute Run Cat for Linux

Usage:
  ./install.sh [--local] [--no-build] [-A | --autostart=true | -na | --autostart=false]
               [--plasmoid | --no-plasmoid] [--restart-plasma | --no-restart-plasma]
  ./install.sh [--local] -U | --uninstall
  ./install.sh -h | --help

Installation (default):
  Builds AQTRunCat from this source tree (Release, without tests) and installs
  it. Run it as your normal user: sudo is used only for copying files into a
  system directory that you cannot write to, never for building. sudo is
  checked after the build: if it refuses (wrong or expired password), on a
  terminal you are asked whether to retry ("Retry installing the build?
  [y/N]"; fix an expired password with passwd in another terminal first);
  N, or no terminal, stops with nothing changed and the build kept.

  Every installation builds from scratch in
  ${XDG_CACHE_HOME:-~/.cache}/aqtruncat/install-build (emptied first, kept
  afterwards). To install again without compiling:
  --no-build              reuse that build as it is. It must exist, come from
                          this source tree, be a Release build and be up to
                          date with the sources; otherwise nothing is installed
                          and you are told to run without --no-build.
                          If that build is another AQTRunCat version than these
                          sources, you get a warning and the question "Install
                          the previous version X (y) or build the new version
                          Y (N)? [y/N]": y installs the previous build as it
                          is, N (default, also without a terminal) builds the
                          sources.

  System-wide (default)  prefix /usr/local, change with --prefix=DIR:
                           DIR/bin/aqtruncat (tray application)
                           DIR/bin/aqtruncatd (backend), aqtruncatctl (command line)
                           DIR/share/dbus-1/services/io.github.psykedady.AQTRunCat.service
                             (starts the backend on demand)
                           DIR/share/dbus-1/services/io.github.psykedady.AQTRunCat.Settings.service
                             (opens Settings on demand)
                           DIR/bin/aqtruncat-claude-metric, aqtruncat-codex-metric
                             (optional Claude Code / Codex integrations, off
                             until enabled in Settings > Integrations)
                           DIR/share/aqtruncat/runners/ (bundled runners, for the Plasmoid)
                           DIR/share/plasma/plasmoids/io.github.psykedady.aqtruncat/
                             (KDE Plasma 6 widget, see "Plasma widget" below)
                           DIR/share/applications/aqtruncat.desktop
                           DIR/share/applications/io.github.psykedady.AQTRunCat.Settings.desktop
                           DIR/share/icons/hicolor/*/apps/aqtruncat.{png,svg}
                           DIR/share/man/man1/aqtruncat.1
                           DIR/share/doc/aqtruncat/ (README, licenses)
                           DIR/share/aqtruncat/install-manifest.txt
  --local                for the current user only, no root and no sudo:
                           ${XDG_BIN_HOME:-~/.local/bin}/aqtruncat, aqtruncatd, aqtruncatctl,
                             aqtruncat-claude-metric, aqtruncat-codex-metric
                           ${XDG_DATA_HOME:-~/.local/share}/applications, dbus-1/services,
                             icons, man, doc, aqtruncat/runners,
                             plasma/plasmoids/io.github.psykedady.aqtruncat
                           ${XDG_STATE_HOME:-~/.local/state}/aqtruncat/install-manifest.txt
                         Your shell files are never edited; if the bin directory
                         is not in PATH you get a warning (the menu entry and
                         autostart use the full path anyway).

  Installing again over an existing installation updates it in place; files
  that are no longer part of AQTRunCat are removed, user data is kept.

Running AQTRunCat:
  Before files are replaced (install, update) or removed (uninstall), a running
  AQTRunCat of this installation is closed: only processes of your user whose
  executable is exactly this installation's bin/aqtruncat (tray application)
  or bin/aqtruncatd (backend) get SIGTERM, the same clean shutdown as "Quit
  AQTRunCat" in the tray menu. Nothing is matched
  by name and nothing else is ever signalled (not Claude Code, not other
  programs, not an AQTRunCat started from another location). The installer
  waits up to 10 seconds; if the application has not exited by then, it stops
  with an error and changes nothing, and is never killed forcibly. Everything
  that can fail first (build, staging, checks, sudo) happens before closing it.
  AQTRunCat is not restarted afterwards: start it again from the menu.
  Enabled Claude Code and Codex integrations from an earlier version are then
  updated by "aqtruncat-claude-metric upgrade" / "aqtruncat-codex-metric
  upgrade" (run as you; keep their options, do nothing when off, never remove
  your data).
  With "sudo" for a system-wide prefix, instances of other users are reported
  but not stopped; they keep running the version they started.

Autostart (start AQTRunCat at login) - ON by default:
  -A, --autostart=true    enable autostart (the default)
  -na, --autostart=false  do not start at login (removes an existing entry)
  Autostart is a per-user preference even for a system-wide installation: it
  is the XDG entry ${XDG_CONFIG_HOME:-~/.config}/autostart/aqtruncat.desktop,
  written by "aqtruncat autostart enable" exactly like the Settings window
  does. With "sudo ./install.sh" it is applied to the user who ran sudo.
  Giving both an enabling and a disabling option is an error.

Plasma widget (Plasmoid, F4.2) - installed when KDE Plasma 6 is found:
  (no option)             Plasma 6 found: the tray application and the widget;
                          not found: the tray application only
  --plasmoid              install the widget, also when this is not a Plasma
                          session; fails with the missing Plasma 6 QML modules
                          (and the packages to install) when they are absent
  --no-plasmoid           do not install the widget (removes one installed
                          earlier by this installation)
  "Plasma 6 found" means the Qt 6 QML modules the widget uses: Plasma
  (org.kde.plasma.plasmoid, components, extras, core, workspace.dbus),
  Kirigami, KCMUtils and Qt Quick Dialogs. Nothing is built for the widget
  (QML only) and nothing KDE is needed by the rest of AQTRunCat.
  Installing the widget does not put it anywhere: add it yourself (panel or
  desktop > Add or Manage Widgets > "AQTRunCat"). Panels and desktop layouts
  are never changed. A widget that is already shown keeps the previous
  version until Plasma starts again: when an installed widget is updated
  while your Plasma is running, on a terminal you are asked "Restart Plasma
  now ...? [y/N]" (N, or no terminal: Plasma is left alone and loads the
  new version at the next login). Restarting Plasma makes panels and the
  desktop disappear for a while, up to about a minute; open windows are not
  affected. The installer waits until Plasma is back and, if it is not,
  says how to start it again.
  --restart-plasma        answer yes without asking (also without a terminal)
  --no-restart-plasma     answer no without asking
  To restart Plasma yourself:  systemctl --user restart plasma-plasmashell
  If it does not come back:    systemctl --user start plasma-plasmashell
  (without systemd: kquitapp6 plasmashell; kstart plasmashell)
  The widget and the tray application are independent: either, both or
  none can run; autostart only concerns the tray application.

Uninstallation:
  -U, --uninstall         remove the system-wide installation (prefix /usr/local
                          or --prefix=DIR)
  --local -U              remove the --local installation of the current user
  Only the files listed in the install manifest are removed, and this user's
  autostart entry if it points at the removed program. Never removed: your
  settings (~/.config/AQTRunCat), user runners and Custom Metrics
  (~/.local/share/AQTRunCat), or Custom Metrics producer files. Without a
  manifest nothing is removed (no guessing).

Other options:
  --prefix=DIR            system-wide prefix (default /usr/local); not with --local
  --dry-run               show what would be done, change nothing
                          (every run first prints its effective options:
                          action, installation type, rebuild, autostart, …,
                          each with its origin, default or option)
  --build-dir=DIR         use an existing CMake build tree instead of the default
                          one: it is brought up to date (all targets), or with
                          --no-build only checked
                          in a temporary directory (development and tests)
  -h, --help              show this help

Requirements (build): CMake >= 3.21, a C++20 compiler, Qt >= 6.5 (Widgets),
libarchive >= 3.0. The installer detects Arch, Fedora and Debian/Ubuntu (and
derivatives through /etc/os-release ID_LIKE) and prints the exact package
command when something is missing; it never installs packages itself.
Ubuntu 24.04 LTS ships Qt 6.4 and is not supported.

Examples:
  ./install.sh                     system-wide, autostart on
  ./install.sh -na                 system-wide, no autostart
  ./install.sh --local             only for me, autostart on, no root
  ./install.sh --local --autostart=false
  ./install.sh --local -U          remove my --local installation
  ./install.sh --no-plasmoid       without the Plasma widget
  ./install.sh --restart-plasma    update and reload a widget already shown
  ./install.sh --local --plasmoid -na
                                   only for me, with the widget, no tray autostart
  sudo ./install.sh --uninstall    remove the system-wide installation

Exit status: 0 success, 1 failure, 2 invalid command line.
EOF
}

say() { printf '%s\n' "$*"; }
warn() { printf 'install.sh: warning: %s\n' "$*" >&2; }
die() {
    printf 'install.sh: error: %s\n' "$*" >&2
    exit 1
}
usage_error() {
    printf 'install.sh: %s\nRun "./install.sh --help" for usage.\n' "$*" >&2
    exit 2
}

# --- command line ---------------------------------------------------------

action=install
scope=system
autostart=""
autostart_option=""
prefix=/usr/local
prefix_given=0
build_dir=""
no_build=0
reuse_previous_version=0
dry_run=0
plasmoid=""        # "", true or false (command line)
plasmoid_option=""
restart_plasma=""  # "", true or false (command line); "" asks
restart_plasma_option=""

set_autostart() {
    if [[ -n $autostart && $autostart != "$1" ]]; then
        usage_error "conflicting autostart options: $autostart_option and $2"
    fi
    autostart=$1
    autostart_option=$2
}

for arg in "$@"; do
    case $arg in
        -h | --help) usage; exit 0 ;;
        --local) scope=local ;;
        -A | --autostart=true) set_autostart true "$arg" ;;
        -na | --autostart=false) set_autostart false "$arg" ;;
        --autostart=*) usage_error "invalid value in \"$arg\" (use --autostart=true or --autostart=false)" ;;
        -U | --uninstall) action=uninstall ;;
        --prefix=*) prefix=${arg#--prefix=}; prefix_given=1 ;;
        --build-dir=*) build_dir=${arg#--build-dir=} ;;
        --no-build) no_build=1 ;;
        --dry-run) dry_run=1 ;;
        --plasmoid | --no-plasmoid)
            value=true
            [[ $arg == --no-plasmoid ]] && value=false
            if [[ -n $plasmoid && $plasmoid != "$value" ]]; then
                usage_error "conflicting options: $plasmoid_option and $arg"
            fi
            plasmoid=$value
            plasmoid_option=$arg
            ;;
        --restart-plasma | --no-restart-plasma)
            value=true
            [[ $arg == --no-restart-plasma ]] && value=false
            if [[ -n $restart_plasma && $restart_plasma != "$value" ]]; then
                usage_error "conflicting options: $restart_plasma_option and $arg"
            fi
            restart_plasma=$value
            restart_plasma_option=$arg
            ;;
        *) usage_error "unknown option \"$arg\"" ;;
    esac
done

if [[ $action == uninstall && -n $autostart ]]; then
    usage_error "$autostart_option only applies to installation (uninstalling always removes this installation's autostart entry)"
fi
if [[ $action == uninstall && -n $build_dir ]]; then
    usage_error "--build-dir only applies to installation"
fi
if [[ $action == uninstall && $no_build == 1 ]]; then
    usage_error "--no-build only applies to installation"
fi
if [[ $action == uninstall && -n $plasmoid ]]; then
    usage_error "$plasmoid_option only applies to installation (uninstalling removes this installation's widget, if any)"
fi
if [[ $action == uninstall && -n $restart_plasma ]]; then
    usage_error "$restart_plasma_option only applies to installation"
fi
if [[ $restart_plasma == true && $plasmoid == false ]]; then
    usage_error "--restart-plasma reloads the widget: it cannot be used with --no-plasmoid"
fi
if [[ $scope == local && $prefix_given == 1 ]]; then
    usage_error "--prefix is for system-wide installation; --local uses the XDG directories"
fi
[[ -n $autostart ]] || autostart=true

# --- distribution ---------------------------------------------------------

os_release=${AQTRUNCAT_OS_RELEASE:-/etc/os-release}
os_id=""
os_like=""
os_name="unknown Linux"
os_version=""

read_os_release() {
    [[ -r $os_release ]] || return 0
    local key value
    while IFS='=' read -r key value; do
        value=${value%\"}; value=${value#\"}; value=${value%\'}; value=${value#\'}
        case $key in
            ID) os_id=$value ;;
            ID_LIKE) os_like=$value ;;
            PRETTY_NAME) os_name=$value ;;
            VERSION_ID) os_version=$value ;;
        esac
    done < "$os_release"
}

# arch | fedora | debian | unknown, from ID first, then ID_LIKE.
distro_family() {
    local token
    # shellcheck disable=SC2086 # ID_LIKE is a space-separated list
    for token in $os_id $os_like; do
        case $token in
            arch) echo arch; return ;;
            fedora | rhel | centos) echo fedora; return ;;
            debian | ubuntu) echo debian; return ;;
        esac
    done
    echo unknown
}

read_os_release
family=$(distro_family)

deps_command() {
    case $family in
        arch) echo "sudo pacman -S --needed cmake ninja gcc qt6-base qt6-wayland libarchive" ;;
        fedora) echo "sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtwayland libarchive-devel" ;;
        debian) echo "sudo apt install cmake ninja-build g++ qt6-base-dev qt6-wayland libarchive-dev libgl-dev" ;;
        *) echo "install CMake >= 3.21, a C++20 compiler, Qt >= 6.5 development files (Widgets) and libarchive >= 3.0 development files with your package manager" ;;
    esac
}

# --- paths ----------------------------------------------------------------

# XDG base directory variables must be absolute; others are ignored (spec).
xdg_dir() {
    local value=$1 fallback=$2
    if [[ $value == /* ]]; then printf '%s\n' "$value"; else printf '%s\n' "$fallback"; fi
}

clean_path() { # strip trailing slashes, keep "/"
    local p=$1
    while [[ $p == */ && $p != / ]]; do p=${p%/}; done
    printf '%s\n' "$p"
}

if [[ $scope == local && $EUID -eq 0 && -n ${SUDO_USER:-} && ${SUDO_USER} != root ]]; then
    usage_error "--local installs for the current user; run it without sudo"
fi
if [[ $scope == local ]]; then
    [[ ${HOME:-} == /* ]] || die "HOME is not set to an absolute path; cannot install with --local"
    bindir=$(clean_path "$(xdg_dir "${XDG_BIN_HOME:-}" "$HOME/.local/bin")")
    datadir=$(clean_path "$(xdg_dir "${XDG_DATA_HOME:-}" "$HOME/.local/share")")
    manifest=$(clean_path "$(xdg_dir "${XDG_STATE_HOME:-}" "$HOME/.local/state")")/aqtruncat/install-manifest.txt
else
    prefix=$(clean_path "$prefix")
    [[ $prefix == /* ]] || usage_error "--prefix must be an absolute path"
    [[ $prefix != / ]] || usage_error "--prefix=/ is not allowed; use /usr/local (default) or /usr"
    [[ $prefix != *$'\n'* ]] || usage_error "--prefix must not contain a newline"
    bindir=$prefix/bin
    datadir=$prefix/share
    manifest=$datadir/aqtruncat/install-manifest.txt
fi
readonly bindir datadir manifest
installed_bin=$bindir/aqtruncat
installed_backend=$bindir/aqtruncatd
# The real user's directories: with "sudo ./install.sh" that is the user who
# ran sudo (autostart and personal data are per-user), not root.
if [[ $EUID -eq 0 && -n ${SUDO_USER:-} && ${SUDO_USER} != root ]]; then
    user_home=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    config_home=$user_home/.config
    data_home=$user_home/.local/share
else
    user_home=${HOME:-}
    config_home=$(xdg_dir "${XDG_CONFIG_HOME:-}" "$user_home/.config")
    data_home=$(xdg_dir "${XDG_DATA_HOME:-}" "$user_home/.local/share")
fi

# A destination is allowed only under bindir or datadir (or the manifest), as
# an absolute path without "." or ".." components.
safe_target() {
    local path=$1
    [[ $path == /* && $path != *$'\n'* ]] || return 1
    case "/$path/" in */../* | */./*) return 1 ;; esac
    [[ $path == "$manifest" || $path == "$bindir"/?* || $path == "$datadir"/?* ]]
}

# Nearest existing ancestor of a path is writable by us.
writable() {
    local p=$1
    while [[ ! -e $p ]]; do p=$(dirname -- "$p"); done
    [[ -w $p ]]
}

SUDO=()
need_privileges() {
    [[ $scope == system ]] || return 1
    local dir
    for dir in "$bindir" "$datadir" "$(dirname -- "$manifest")"; do
        writable "$dir" || return 0
    done
    return 1
}

choose_privileges() {
    if need_privileges; then
        if [[ $EUID -eq 0 ]]; then
            die "cannot write to $prefix even as root"
        fi
        command -v sudo > /dev/null || die "writing to $prefix needs root privileges and sudo is not available; run as root or use --local"
        SUDO=(sudo)
        say "Root privileges are needed to write to $prefix; sudo will be used for copying files only."
    fi
}

# Validate sudo before anything is changed. A refused password (wrong, or
# expired) is not fatal at once on a terminal: the user can fix it, e.g. with
# "passwd" in another terminal, and retry without rebuilding. Without a
# terminal, or on "N", nothing is changed and the hint says how to go on.
authenticate_sudo() { # authenticate_sudo HINT
    ((${#SUDO[@]})) || return 0
    local answer
    while ! sudo -v; do
        warn "sudo authentication failed (wrong or expired password?)"
        if [[ ! -t 0 ]]; then
            die "nothing was changed; $1"
        fi
        say "If your password has expired, change it in another terminal (passwd), then retry."
        answer=""
        read -r -p "$( [[ $action == install ]] && echo "Retry installing the build?" || echo "Retry the uninstallation?") [y/N] " answer || answer=""
        [[ $answer == [yY] || $answer == [yY][eE][sS] ]] || die "nothing was changed; $1"
    done
}

# Ask the real user's session bus to re-read its service files, so that the
# backend can be D-Bus activated without logging out (dbus-broker does not
# watch the directories). Best effort: without a session bus the clients
# start the backend themselves.
session_bus_address() {
    local address=${DBUS_SESSION_BUS_ADDRESS:-}
    if [[ -z $address && -S /run/user/$real_uid/bus ]]; then
        address=unix:path=/run/user/$real_uid/bus
    fi
    printf '%s\n' "$address"
}

reload_session_bus() {
    local address
    address=$(session_bus_address)
    [[ -n $address ]] || return 0
    if command -v busctl > /dev/null; then
        as_user env DBUS_SESSION_BUS_ADDRESS="$address" busctl --user call org.freedesktop.DBus /org/freedesktop/DBus \
            org.freedesktop.DBus ReloadConfig > /dev/null 2>&1 || true
    elif command -v dbus-send > /dev/null; then
        as_user env DBUS_SESSION_BUS_ADDRESS="$address" dbus-send --session --type=method_call --dest=org.freedesktop.DBus \
            /org/freedesktop/DBus org.freedesktop.DBus.ReloadConfig > /dev/null 2>&1 || true
    fi
}

# Run a command as the real user (the one who invoked sudo, if any).
as_user() {
    if [[ $EUID -eq 0 && -n ${SUDO_USER:-} && ${SUDO_USER} != root ]]; then
        sudo -u "$SUDO_USER" -H -- "$@"
    else
        "$@"
    fi
}

desktop_exec_value() { # quote for the Exec key when needed (XDG desktop entry spec)
    local path=$1
    if [[ $path =~ [[:space:]\"\`\$\\] ]]; then
        path=${path//\\/\\\\}; path=${path//\"/\\\"}; path=${path//\`/\\\`}; path=${path//\$/\\\$}
        printf '"%s"\n' "$path"
    else
        printf '%s\n' "$path"
    fi
}

# The autostart entry of this user points at the given binary.
autostart_points_to() {
    local entry=$config_home/autostart/aqtruncat.desktop bin=$1
    [[ -f $entry ]] || return 1
    grep -qxF "Exec=\"$bin\"" "$entry" || grep -qxF "Exec=$bin" "$entry"
}

# --- build directory ------------------------------------------------------

# Persistent, owned by whoever runs the build (never root's files in a user's
# cache: the build is never run through sudo). Only this exact directory is
# ever emptied.
if [[ $EUID -eq 0 ]]; then
    cache_home=$(getent passwd root | cut -d: -f6)/.cache
else
    cache_home=$(xdg_dir "${XDG_CACHE_HOME:-}" "${HOME:-/nonexistent}/.cache")
fi
default_build=$(clean_path "$cache_home")/aqtruncat/install-build
readonly BUILD_MARKER=.aqtruncat-install-build
if [[ -n $build_dir ]]; then
    build_dir=$(clean_path "$build_dir")
fi

cache_value() { # cache_value BUILD KEY -> value of KEY in BUILD/CMakeCache.txt
    sed -n "s/^$2:[A-Z]*=//p" "$1/CMakeCache.txt" | head -n 1
}

# The existing build may be installed without compiling: it belongs to this
# source tree (check_build_origin) and the build tool has nothing left to do
# (check_build_fresh).
check_build_origin() { # check_build_origin BUILD REQUIRE_RELEASE
    local build=$1 home
    [[ -f $build/CMakeCache.txt ]] ||
        die "there is no build to reuse in $build; run the installer without --no-build"
    home=$(cache_value "$build" CMAKE_HOME_DIRECTORY)
    [[ $(cd -- "$home" 2> /dev/null && pwd -P) == "$(cd -- "$SCRIPT_DIR" && pwd -P)" ]] ||
        die "$build was configured for another source tree ($home); run the installer without --no-build"
    if [[ $2 == release && $(cache_value "$build" CMAKE_BUILD_TYPE) != Release ]]; then
        die "$build is not a Release build; run the installer without --no-build"
    fi
}

check_build_fresh() { # check_build_fresh BUILD
    local build=$1 pending
    case $(cache_value "$build" CMAKE_GENERATOR) in
        Ninja*)
            pending=$(cmake --build "$build" -- -n 2> /dev/null) || pending=unknown
            [[ $pending == *"no work to do"* ]] || pending=stale
            ;;
        *)
            cmake --build "$build" -- -q > /dev/null 2>&1 && pending="no work to do" || pending=stale
            ;;
    esac
    [[ $pending == *"no work to do"* ]] ||
        die "the build in $build is not up to date with the sources; run the installer without --no-build"
}

# AQTRunCat version of these sources: project(VERSION) in CMakeLists.txt.
source_version() {
    sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$SCRIPT_DIR/CMakeLists.txt" | head -n 1
}

# Version of an existing build: what its aqtruncat reports, else the version
# CMake configured it with.
built_version() { # built_version BUILD
    local version=""
    if [[ -x $1/src/aqtruncat ]]; then
        version=$("$1/src/aqtruncat" --version 2> /dev/null | awk '{print $2}') || version=""
    fi
    [[ -n $version ]] || version=$(cache_value "$1" CMAKE_PROJECT_VERSION)
    printf '%s\n' "${version:-unknown}"
}

# --no-build with a build of another version than these sources: warn and
# ask which one to install. "y" installs the previous build as it is; "N"
# (the default, also without a terminal) builds the sources instead.
# Returns 0 to reuse the build, 1 to build.
choose_build_version() { # choose_build_version BUILD
    local built source answer=""
    built=$(built_version "$1")
    source=$(source_version)
    # Same version, or one that cannot be read (an incomplete build): the
    # up-to-date check decides, as before.
    [[ $built != "$source" && $built != unknown ]] || return 0
    warn "the build in $1 is AQTRunCat $built, these sources are AQTRunCat $source"
    if [[ -t 0 ]]; then
        read -r -p "Install the previous version $built (y) or build the new version $source (N)? [y/N] " answer || answer=""
    else
        say "No terminal to ask: building the new version $source."
    fi
    if [[ $answer == [yY] || $answer == [yY][eE][sS] ]]; then
        reuse_previous_version=1
        return 0
    fi
    return 1
}

# --- running instances ----------------------------------------------------

stop_timeout=${AQTRUNCAT_STOP_TIMEOUT:-10} # seconds; overridable for tests
[[ $stop_timeout =~ ^[1-9][0-9]?$ ]] || usage_error "AQTRUNCAT_STOP_TIMEOUT must be 1-99 seconds"
if [[ $EUID -eq 0 && -n ${SUDO_USER:-} && ${SUDO_USER} != root ]]; then
    real_uid=$(id -u -- "$SUDO_USER")
else
    real_uid=$EUID
fi
stopped_instances=0

# Start time (field 22 of /proc/PID/stat) tells a process apart from a later
# one that reuses its PID. A zombie has already exited: no start time.
process_start() {
    local stat
    { IFS= read -r stat < "/proc/$1/stat"; } 2> /dev/null || return 1
    stat=${stat##*) }
    local -a fields
    read -r -a fields <<< "$stat"
    [[ ${fields[0]} != Z ]] || return 1
    printf '%s\n' "${fields[19]}"
}

process_uid() {
    local key value _
    while read -r key value _; do
        [[ $key == Uid: ]] && { printf '%s\n' "$value"; return 0; }
    done < "/proc/$1/status" 2> /dev/null
    return 1
}

# "PID UID START" for every process running this installation's tray
# application or backend (F4.1). The name only preselects candidates
# cheaply; the decision is the exact executable path (" (deleted)" once a
# newer file replaced it). Processes of other users are only visible to root.
running_instances() {
    local dir pid comm exe
    for dir in /proc/[0-9]*; do
        pid=${dir#/proc/}
        { IFS= read -r comm < "$dir/comm"; } 2> /dev/null || continue
        [[ $comm == aqtruncat || $comm == aqtruncatd ]] || continue
        exe=$(readlink -- "$dir/exe" 2> /dev/null) || continue
        exe=${exe% (deleted)}
        [[ $exe == "$installed_bin" || $exe == "$installed_backend" ]] || continue
        printf '%s %s %s\n' "$pid" "$(process_uid "$pid")" "$(process_start "$pid")"
    done
}

describe_instances() {
    local pid uid start found=0 will="will be"
    ((dry_run)) && will="would be"
    while read -r pid uid start; do
        found=1
        if [[ $uid == "$real_uid" ]]; then
            option "running" "PID $pid, $will closed (SIGTERM, up to ${stop_timeout} s) before files change"
        else
            option "running" "PID $pid of user $uid, left running (not yours)"
        fi
    done < <(running_instances)
    ((found)) || option "running" "no instance of this installation"
}

# Close this user's running AQTRunCat of this installation, or fail without
# having changed anything. Never SIGKILL, never other users, never by name.
stop_running_instances() {
    local -a pids=() starts=() others=()
    local pid uid start
    while read -r pid uid start; do
        if [[ $uid == "$real_uid" ]]; then
            pids+=("$pid")
            starts+=("$start")
        else
            others+=("$pid")
        fi
    done < <(running_instances)
    if ((${#others[@]})); then
        warn "AQTRunCat is also running for other users (PID ${others[*]}); it is not stopped and keeps the version it started until it is restarted"
    fi
    ((${#pids[@]})) || return 0

    say "Closing the running AQTRunCat (PID ${pids[*]}) ..."
    local i
    for i in "${!pids[@]}"; do
        kill -TERM "${pids[$i]}" 2> /dev/null || true
    done
    local waited=0 alive
    while :; do
        alive=()
        for i in "${!pids[@]}"; do
            [[ $(process_start "${pids[$i]}" 2> /dev/null) == "${starts[$i]}" ]] && alive+=("${pids[$i]}")
        done
        ((${#alive[@]})) || break
        if ((waited >= stop_timeout * 10)); then
            die "AQTRunCat (PID ${alive[*]}) did not exit within ${stop_timeout} s after SIGTERM; nothing was changed. Quit it from its tray menu (or end that process yourself) and run the installer again."
        fi
        sleep 0.1
        waited=$((waited + 1))
    done
    stopped_instances=1
    say "AQTRunCat closed."
}

# --- dependency checks ----------------------------------------------------

version_at_least() { # version_at_least 6.4.2 6.5
    [[ $(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1) == "$2" ]]
}

# Qt version from qtpaths/qmake. qtpaths takes "--query", qmake "-query";
# each attempt is captured on its own and must look like a version (a tool
# rejecting the other spelling may print its usage to stdout).
qt_version_from() { # qt_version_from TOOL
    local query value
    for query in --query -query; do
        value=$("$1" "$query" QT_VERSION 2> /dev/null) || continue
        if [[ $value =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
            printf '%s\n' "$value"
            return 0
        fi
    done
    return 1
}

qt_version() {
    local tool
    for tool in qtpaths6 qmake6 /usr/lib/qt6/bin/qtpaths /usr/lib64/qt6/bin/qtpaths /usr/lib/qt6/bin/qmake /usr/lib64/qt6/bin/qmake; do
        command -v "$tool" > /dev/null 2>&1 || continue
        qt_version_from "$tool" && return 0
    done
    return 1
}

# --- Plasma widget (F4.2) -------------------------------------------------

readonly PLASMOID_ID=io.github.psykedady.aqtruncat
# Qt 6 QML modules the widget imports (contents/ui/*.qml).
readonly PLASMOID_QML_MODULES="org/kde/plasma/plasmoid org/kde/plasma/core org/kde/plasma/components org/kde/plasma/extras org/kde/plasma/workspace/dbus org/kde/kirigami org/kde/kcmutils QtQuick/Dialogs"

# Qt 6 QML import directories: AQTRUNCAT_QML_DIRS (colon separated; tests),
# else Qt's QT_INSTALL_QML and the usual distribution locations.
qml_dirs() {
    if [[ -n ${AQTRUNCAT_QML_DIRS+set} ]]; then
        tr ':' '\n' <<< "$AQTRUNCAT_QML_DIRS"
        return 0
    fi
    local tool dir
    for tool in qtpaths6 /usr/lib/qt6/bin/qtpaths /usr/lib64/qt6/bin/qtpaths; do
        command -v "$tool" > /dev/null 2>&1 || continue
        if dir=$("$tool" --query QT_INSTALL_QML 2> /dev/null) && [[ $dir == /* ]]; then
            printf '%s\n' "$dir"
        fi
        break
    done
    printf '%s\n' /usr/lib/qt6/qml /usr/lib64/qt6/qml /usr/lib/x86_64-linux-gnu/qt6/qml /usr/lib/aarch64-linux-gnu/qt6/qml
}

# Plasma 6 is "found" when every module of PLASMOID_QML_MODULES is: the
# widget needs nothing else (no build, no library). Fills plasma_missing
# (module names) and plasma_version (libplasma's, when it says).
plasma_missing=()
plasma_version=""
detect_plasma() {
    plasma_missing=()
    plasma_version=""
    local -a dirs=()
    local module dir found
    mapfile -t dirs < <(qml_dirs)
    for module in $PLASMOID_QML_MODULES; do
        found=""
        for dir in "${dirs[@]}"; do
            if [[ -n $dir && -f $dir/$module/qmldir ]]; then
                found=$dir
                break
            fi
        done
        if [[ -z $found ]]; then
            plasma_missing+=("${module//\//.}")
        elif [[ $module == org/kde/plasma/plasmoid && -r $found/$module/kde-qmlmodule.version ]]; then
            { IFS= read -r plasma_version < "$found/$module/kde-qmlmodule.version"; } 2> /dev/null || plasma_version=""
        fi
    done
    if ((${#plasma_missing[@]} == 0)) && [[ -n $plasma_version && ${plasma_version%%.*} != 6 ]]; then
        plasma_missing+=("Plasma 6 (found Plasma $plasma_version)")
    fi
}

plasma_deps_command() {
    case $family in
        arch) echo "sudo pacman -S --needed plasma-workspace libplasma kirigami kcmutils qt6-declarative" ;;
        fedora) echo "sudo dnf install plasma-workspace libplasma kf6-kirigami kf6-kcmutils qt6-qtdeclarative" ;;
        debian) echo "sudo apt install plasma-workspace libplasma6 qml6-module-org-kde-kirigami qml6-module-org-kde-kcmutils qml6-module-qtquick-dialogs" ;;
        *) echo "install KDE Plasma 6 (plasma-workspace) with its QML modules (Kirigami, KCMUtils) and Qt 6 Quick Dialogs" ;;
    esac
}

# install_plasmoid: true | false | missing (--plasmoid without Plasma 6),
# plasmoid_reason: why, for the printed options.
install_plasmoid=false
plasmoid_reason=""
decide_plasmoid() {
    detect_plasma
    local missing=""
    ((${#plasma_missing[@]} == 0)) || missing=$(printf '%s, ' "${plasma_missing[@]}")
    missing=${missing%, }
    if [[ $plasmoid == false ]]; then
        install_plasmoid=false
        plasmoid_reason="false (--no-plasmoid)"
    elif [[ -z $missing ]]; then
        install_plasmoid=true
        plasmoid_reason="true (${plasmoid_option:-default}: KDE Plasma ${plasma_version:-6} found)"
    elif [[ $plasmoid == true ]]; then
        install_plasmoid=missing
        plasmoid_reason="requested (--plasmoid) but KDE Plasma 6 is missing: $missing"
    else
        install_plasmoid=false
        plasmoid_reason="false (default: KDE Plasma 6 not found, missing $missing)"
    fi
}

# --- Plasma restart (opt-in, after a widget update) -------------------------

readonly PLASMA_UNIT=plasma-plasmashell.service
# Overridable for tests: the process name looked for, and how long (seconds)
# to wait for Plasma to come back.
plasma_shell_name=${AQTRUNCAT_PLASMASHELL_NAME:-plasmashell}
plasma_timeout=${AQTRUNCAT_PLASMA_TIMEOUT:-90}
[[ $plasma_timeout =~ ^[1-9][0-9]{0,2}$ ]] || usage_error "AQTRUNCAT_PLASMA_TIMEOUT must be 1-999 seconds"
readonly PLASMA_RESTART_HINT="systemctl --user restart plasma-plasmashell (if it stays down: systemctl --user start plasma-plasmashell; without systemd: kquitapp6 plasmashell; kstart plasmashell)"

# This user's plasmashell is running.
plasma_running() {
    local dir comm
    for dir in /proc/[0-9]*; do
        { IFS= read -r comm < "$dir/comm"; } 2> /dev/null || continue
        [[ $comm == "$plasma_shell_name" ]] || continue
        [[ $(process_uid "${dir#/proc/}") == "$real_uid" ]] && return 0
    done
    return 1
}

wait_for_plasma() {
    local waited=0
    until plasma_running; do
        ((waited < plasma_timeout * 10)) || return 1
        sleep 0.1
        waited=$((waited + 1))
    done
}

# A command in the real user's session (systemd user manager, session bus),
# also under sudo.
in_session() {
    local runtime=${XDG_RUNTIME_DIR:-/run/user/$real_uid}
    ((EUID != 0)) || runtime=/run/user/$real_uid
    as_user env XDG_RUNTIME_DIR="$runtime" DBUS_SESSION_BUS_ADDRESS="$(session_bus_address)" "$@"
}

restart_plasma_now() {
    say "Restarting Plasma: panels and the desktop disappear meanwhile, up to about a minute (open windows are not affected) ..."
    if in_session systemctl --user --quiet is-active "$PLASMA_UNIT" 2> /dev/null; then
        # Blocks until the new plasmashell owns its D-Bus name (the unit
        # allows 40 s to stop and 40 s to start).
        in_session systemctl --user restart "$PLASMA_UNIT" 2> /dev/null || true
        if ! plasma_running; then
            # A failed start or the unit's start limit: start it once more.
            in_session systemctl --user reset-failed "$PLASMA_UNIT" 2> /dev/null || true
            in_session systemctl --user start "$PLASMA_UNIT" 2> /dev/null || true
        fi
    elif ((EUID != 0)) && command -v kquitapp6 > /dev/null && command -v kstart > /dev/null; then
        # Plasma not started by systemd: quit it (kquitapp6 waits for the
        # exit), then start it detached from this terminal.
        kquitapp6 plasmashell > /dev/null 2>&1 || true
        kstart plasmashell > /dev/null 2>&1 || true
    else
        warn "Plasma was not restarted (no systemd unit $PLASMA_UNIT, and kquitapp6/kstart need your own session, not sudo); restart it yourself: $PLASMA_RESTART_HINT"
        return 0
    fi
    if wait_for_plasma; then
        say "Plasma is running again: the widget uses the new version."
    else
        warn "Plasma did not come back within ${plasma_timeout} s. From a terminal (or a text console, Ctrl+Alt+F3), run: $PLASMA_RESTART_HINT"
    fi
}

# After a widget update: the running Plasma still has the previous version.
offer_plasma_restart() {
    plasma_running || return 0 # no Plasma of this user: it loads the new one when it starts
    local answer=""
    case $restart_plasma in
        true) answer=y ;;
        false) ;;
        *)
            if [[ -t 0 ]]; then
                read -r -p "Restart Plasma now so that the widget already shown uses the new version? Panels and the desktop disappear meanwhile, up to about a minute; open windows are not affected. [y/N] " answer || answer=""
            fi
            ;;
    esac
    if [[ $answer == [yY] || $answer == [yY][eE][sS] ]]; then
        restart_plasma_now
    else
        say "  A widget already shown keeps the previous version until Plasma starts again (next login), or restart Plasma yourself: $PLASMA_RESTART_HINT"
    fi
}

# Plasma finds widgets in $XDG_DATA_HOME and $XDG_DATA_DIRS; the backend's
# D-Bus activation file is found the same way.
data_dir_searched() {
    local dir
    if [[ $scope == local ]]; then
        return 0
    fi
    local IFS=:
    for dir in ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
        [[ $(clean_path "$dir") == "$datadir" ]] && return 0
    done
    return 1
}

check_build_dependencies() {
    local missing=()
    command -v cmake > /dev/null || missing+=("cmake")
    if [[ -z $build_dir && $no_build == 0 ]]; then
        command -v c++ > /dev/null || command -v g++ > /dev/null || command -v clang++ > /dev/null || missing+=("a C++ compiler")
        command -v ninja > /dev/null || command -v make > /dev/null || missing+=("ninja (or make)")
        local qt
        if qt=$(qt_version); then
            if ! version_at_least "$qt" 6.5; then
                say "Found Qt $qt, but AQTRunCat requires Qt >= 6.5."
                [[ $os_id == ubuntu && $os_version == 24.04 ]] &&
                    say "Ubuntu 24.04 LTS ships Qt 6.4 and is not supported; use Ubuntu 26.04 LTS or newer (see README.md)."
                die "Qt is too old"
            fi
        else
            missing+=("Qt 6 development files (>= 6.5)")
        fi
        if command -v pkg-config > /dev/null && ! pkg-config --exists libarchive; then
            missing+=("libarchive development files")
        fi
    fi
    if ((${#missing[@]})); then
        local joined
        joined=$(printf '%s, ' "${missing[@]}")
        say "Missing build requirements: ${joined%, }"
        say "Detected: $os_name (family: $family). Install them with:"
        say "  $(deps_command)"
        die "missing build requirements"
    fi
}

# --- work directory -------------------------------------------------------

work=""
tmp_base=$(clean_path "${TMPDIR:-/tmp}")
cleanup() {
    local status=$?
    if [[ -n $work && -d $work && $work == "$tmp_base"/aqtruncat-install.* ]]; then
        rm -rf -- "$work"
    fi
    if ((status != 0 && stopped_instances)); then
        printf 'install.sh: AQTRunCat was closed before the failure; start it again with: %s\n' "$installed_bin" >&2
    fi
}
trap cleanup EXIT

# --- manifest -------------------------------------------------------------

# Validate in the main shell (a "die" inside a process substitution would only
# end the subshell), then read the recorded paths.
check_manifest() {
    local first=""
    IFS= read -r first < "$manifest" || true
    [[ $first == "$MANIFEST_HEADER" ]] || die "$manifest is not an AQTRunCat install manifest; nothing changed"
}

read_manifest() { # prints the recorded paths of a checked manifest
    grep -v '^#' "$manifest" | grep -v '^$' || true
}

remove_file() {
    local path=$1
    if ! safe_target "$path"; then
        warn "refusing to remove $path: outside $bindir and $datadir"
        return 0
    fi
    if [[ -d $path && ! -L $path ]]; then
        warn "refusing to remove directory $path"
        return 0
    fi
    if [[ -e $path || -L $path ]]; then
        "${SUDO[@]}" rm -f -- "$path"
    fi
}

remove_empty_dir() {
    local dir=$1
    [[ -d $dir ]] || return 0
    "${SUDO[@]}" rmdir -- "$dir" 2> /dev/null || true
}

# Empty directories that belong to AQTRunCat alone, deepest first, for the
# given removed files: its documentation, share/aqtruncat (bundled runners)
# and its Plasma widget package. Shared parents (share/plasma, …) are kept.
remove_owned_dirs() {
    local f dir root
    local -a dirs=()
    for f in "$@"; do
        local -a roots=("$datadir/aqtruncat" "$datadir/plasma/plasmoids/$PLASMOID_ID")
        if [[ $f == "$datadir"/doc/*/* ]]; then
            root=${f#"$datadir"/doc/}
            roots+=("$datadir/doc/${root%%/*}")
        fi
        for root in "${roots[@]}"; do
            [[ $f == "$root"/* ]] || continue
            dir=$(dirname -- "$f")
            while [[ $dir == "$root" || $dir == "$root"/* ]]; do
                dirs+=("$dir")
                dir=$(dirname -- "$dir")
            done
        done
    done
    ((${#dirs[@]})) || return 0
    while IFS= read -r dir; do
        remove_empty_dir "$dir"
    done < <(printf '%s\n' "${dirs[@]}" | sort -u | awk '{ print length, $0 }' | sort -rn | cut -d' ' -f2-)
}

# The desktop entry declares no MIME types, so no desktop database needs
# updating; only an existing hicolor icon cache is refreshed.
refresh_desktop_caches() {
    if command -v gtk-update-icon-cache > /dev/null && [[ -f $datadir/icons/hicolor/index.theme ]]; then
        "${SUDO[@]}" gtk-update-icon-cache -q -t -f "$datadir/icons/hicolor" 2> /dev/null || true
    fi
}

# --- plan -----------------------------------------------------------------

option() { printf '  %-17s = %s\n' "$1" "$2"; }

# Printed first by every run (also --dry-run): the effective settings and
# where each one comes from.
print_options() {
    say "AQTRunCat installer"
    option "action" "$action$( [[ $action == uninstall ]] && echo " (-U)" || echo " (default)")"
    if [[ $scope == system ]]; then
        option "installation type" "system-wide, all users (prefix $prefix$( ((prefix_given)) && echo ", --prefix" || echo ", default"))"
    else
        option "installation type" "local, current user only (--local)"
    fi
    if [[ $action == install ]]; then
        if ((no_build)); then
            option "rebuild" "false: reuse ${build_dir:-$default_build} (--no-build; must be up to date)"
            if [[ -f ${build_dir:-$default_build}/CMakeCache.txt ]]; then
                local built
                built=$(built_version "${build_dir:-$default_build}")
                if [[ $built == "$(source_version)" ]]; then
                    option "version" "$built (build and sources)"
                elif [[ $built == unknown ]]; then
                    option "version" "sources $(source_version), build unknown"
                else
                    option "version" "build $built, sources $(source_version): you will be asked which to install"
                fi
            fi
        elif [[ -n $build_dir ]]; then
            option "rebuild" "true: bring $build_dir up to date (--build-dir)"
        else
            option "rebuild" "true: from scratch in $default_build (default)"
        fi
        option "autostart" "$autostart (${autostart_option:-default})"
        option "plasmoid" "$plasmoid_reason"
        case $restart_plasma in
            true) option "restart Plasma" "true (--restart-plasma: after a widget update, if Plasma runs)" ;;
            false) option "restart Plasma" "false (--no-restart-plasma)" ;;
            *) option "restart Plasma" "ask (default: after a widget update, if Plasma runs, on a terminal; otherwise no)" ;;
        esac
    fi
    option "dry run" "$( ((dry_run)) && echo true || echo false)"
    option "binary" "$installed_bin"
    option "data" "$datadir"
    option "manifest" "$manifest"
    if need_privileges; then option "privileges" "sudo for file copies"; else option "privileges" "none"; fi
    option "stop timeout" "${stop_timeout} s"
    option "distribution" "$os_name (family: $family)"
    describe_instances
}

# --- install --------------------------------------------------------------

do_install() {
    check_build_dependencies
    choose_privileges
    work=$(mktemp -d "$tmp_base/aqtruncat-install.XXXXXX")

    local build reuse=0
    if ((no_build)); then
        build=${build_dir:-$default_build}
        check_build_origin "$build" "$( [[ -n $build_dir ]] && echo any || echo release)"
        if choose_build_version "$build"; then
            # The previous version on request is by definition behind the
            # sources: only a build of the same version must be up to date.
            ((reuse_previous_version)) || check_build_fresh "$build"
            say "Reusing the build in $build (--no-build: nothing is compiled)"
            reuse=1
        else
            say "Building the new version instead of reusing $build"
        fi
    fi
    if ((reuse)); then
        :
    elif [[ -n $build_dir ]]; then
        [[ -f $build_dir/CMakeCache.txt ]] || die "$build_dir is not a CMake build tree"
        build=$build_dir
        say "Using the existing build tree $build"
        cmake --build "$build" --parallel > "$work/build.log" 2>&1 ||
            { tail -n 30 "$work/build.log" >&2; die "build failed (log above)"; }
    else
        build=$default_build
        # From scratch every time, but only in a directory this installer
        # created (marker file) or an empty/missing one.
        if [[ -e $build && ! -f $build/$BUILD_MARKER && -n $(find "$build" -mindepth 1 -maxdepth 1 -print -quit 2> /dev/null) ]]; then
            die "$build exists but was not created by the AQTRunCat installer; remove it yourself"
        fi
        rm -rf -- "$build"
        mkdir -p -- "$build" || die "cannot create $build"
        : > "$build/$BUILD_MARKER"
        local -a generator=()
        command -v ninja > /dev/null && generator=(-G Ninja) # the suggested packages install ninja, not always make
        say "Configuring and building AQTRunCat (Release) ..."
        cmake -S "$SCRIPT_DIR" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF > "$work/configure.log" 2>&1 ||
            { tail -n 30 "$work/configure.log" >&2; die "CMake configuration failed (log above). Missing packages? $(deps_command)"; }
        cmake --build "$build" --parallel > "$work/build.log" 2>&1 ||
            { tail -n 30 "$work/build.log" >&2; die "build failed (log above)"; }
    fi

    # Stage with the install rules of CMakeLists.txt (single source of truth).
    local stage=$work/stage
    DESTDIR=$stage cmake --install "$build" --prefix / > "$work/stage.log" 2>&1 ||
        { tail -n 30 "$work/stage.log" >&2; die "staging failed"; }
    local top
    for top in "$stage"/*; do
        case ${top##*/} in
            bin | share) ;;
            *) die "unexpected staged directory ${top##*/}; only bin/ and share/ can be installed" ;;
        esac
    done
    [[ -x $stage/bin/aqtruncat ]] || die "the staged tree has no bin/aqtruncat"

    # Plasma widget (F4.2): part of every build (QML files only); kept or
    # left out here, so that --no-build installs exactly what was decided.
    local staged_plasmoid=$stage/share/plasma/plasmoids/$PLASMOID_ID
    if [[ $install_plasmoid == true && ! -f $staged_plasmoid/metadata.json ]]; then
        if [[ $plasmoid == true ]]; then
            die "--plasmoid: the build in $build has no Plasma widget (an older AQTRunCat version); nothing was changed"
        fi
        warn "the build in $build has no Plasma widget (an older AQTRunCat version); installing without it"
        install_plasmoid=false
    fi
    if [[ $install_plasmoid != true && -d $stage/share/plasma ]]; then
        rm -rf -- "$staged_plasmoid"
        rmdir -p -- "$stage/share/plasma/plasmoids" 2> /dev/null || true
    fi

    # Menu entries: start the binary by its full path (bin may not be in PATH).
    local desktop
    for desktop in "$stage/share/applications/aqtruncat.desktop" \
                   "$stage/share/applications/aqtruncat-settings.desktop"; do
      if [[ -f $desktop ]]; then
        local exec_value
        exec_value=$(desktop_exec_value "$installed_bin")
        local escaped=${exec_value//\\/\\\\}
        escaped=${escaped//|/\\|}
        escaped=${escaped//&/\\&}
        sed -i "s|^Exec=aqtruncat settings\$|Exec=$escaped settings|; s|^Exec=aqtruncat\$|Exec=$escaped|" "$desktop"
        if [[ $exec_value == "$installed_bin" ]]; then
            sed -i "s|^TryExec=aqtruncat\$|TryExec=$escaped|" "$desktop"
        else
            sed -i '/^TryExec=/d' "$desktop"
        fi
      fi
    done
    # D-Bus activation file: the backend of this installation, by full path.
    local service=$stage/share/dbus-1/services/io.github.psykedady.AQTRunCat.service
    if [[ -f $service ]]; then
        local backend_exec
        backend_exec=$(desktop_exec_value "$installed_backend")
        backend_exec=${backend_exec//\\/\\\\}
        backend_exec=${backend_exec//|/\\|}
        backend_exec=${backend_exec//&/\\&}
        sed -i "s|^Exec=.*\$|Exec=$backend_exec|" "$service"
    fi
    # Settings D-Bus activation runs the same aqtruncat executable in its
    # standalone mode; keep system and --local launch paths aligned.
    local settings_service=$stage/share/dbus-1/services/io.github.psykedady.AQTRunCat.Settings.service
    if [[ -f $settings_service ]]; then
        local settings_exec
        settings_exec=$(desktop_exec_value "$installed_bin")
        settings_exec+=" settings"
        settings_exec=${settings_exec//\\/\\\\}
        settings_exec=${settings_exec//|/\\|}
        settings_exec=${settings_exec//\&/\\\&}
        sed -i "s|^Exec=.*\$|Exec=$settings_exec|" "$settings_service"
    fi

    # Destination of every staged file.
    local -a sources=() targets=()
    local rel target
    while IFS= read -r -d '' rel; do
        rel=${rel#./}
        case $rel in
            bin/*) target=$bindir/${rel#bin/} ;;
            share/*) target=$datadir/${rel#share/} ;;
            *) die "unexpected staged file $rel" ;;
        esac
        safe_target "$target" || die "refusing to install outside the destination: $target"
        sources+=("$stage/$rel")
        targets+=("$target")
    done < <(cd "$stage" && find . \( -type f -o -type l \) -print0 | sort -z)

    # Files of a previous installation that are no longer installed.
    local -a stale=()
    local had_plasmoid=0
    if [[ -f $manifest ]]; then
        check_manifest
        local old
        while IFS= read -r old; do
            local keep=0 t
            for t in "${targets[@]}"; do [[ $t == "$old" ]] && { keep=1; break; }; done
            ((keep)) || stale+=("$old")
        done < <(read_manifest)
        grep -qxF "$datadir/plasma/plasmoids/$PLASMOID_ID/metadata.json" "$manifest" && had_plasmoid=1
        say "Updating the existing installation ($manifest)"
    fi

    # Last step that can still fail on its own: the sudo password, asked for
    # before the running application is closed.
    authenticate_sudo "the build in $build is kept: run the installer again with --no-build to install it without compiling"
    stop_running_instances

    say "Installing ${#targets[@]} files ..."
    local i
    for i in "${!targets[@]}"; do
        if [[ -L ${sources[$i]} ]]; then
            "${SUDO[@]}" mkdir -p -- "$(dirname -- "${targets[$i]}")"
            "${SUDO[@]}" cp -P -f -- "${sources[$i]}" "${targets[$i]}"
        else
            "${SUDO[@]}" install -D -m "$(stat -c %a -- "${sources[$i]}")" -- "${sources[$i]}" "${targets[$i]}"
        fi
    done
    for target in "${stale[@]}"; do remove_file "$target"; done
    remove_owned_dirs "${stale[@]}"

    local list=$work/manifest
    {
        printf '%s\n' "$MANIFEST_HEADER"
        printf '# version: %s\n' "$("$stage/bin/aqtruncat" --version 2> /dev/null | awk '{print $2}')"
        printf '# scope: %s\n' "$scope"
        printf '%s\n' "${targets[@]}"
    } > "$list"
    "${SUDO[@]}" install -D -m 0644 -- "$list" "$manifest"
    refresh_desktop_caches

    # Autostart is one per-user preference (also saved in AQTRunCat.ini, which
    # the application re-applies at start-up), so it is always set through
    # the application: enabled, or disabled with any existing entry removed.
    if [[ $autostart == true ]]; then
        as_user "$installed_bin" autostart enable || die "AQTRunCat is installed, but enabling autostart failed"
    else
        as_user "$installed_bin" autostart disable || die "AQTRunCat is installed, but disabling autostart failed"
    fi

    # An enabled integration written by an earlier version is rewritten by
    # its own adapter, with its options; an adapter never creates one, never
    # touches a user-managed package, and this never deletes anything in the
    # user's data directory. Each integration is independent.
    local adapter
    for adapter in "$bindir/aqtruncat-claude-metric" "$bindir/aqtruncat-codex-metric"; do
        if [[ -x $adapter ]] && ! as_user "$adapter" upgrade; then
            warn "the ${adapter##*/aqtruncat-} integration could not be upgraded; open Settings > Integrations or run: $adapter upgrade"
        fi
    done

    reload_session_bus

    if [[ -x $bindir/aqtruncatctl ]]; then
        say "Checking dependencies for enabled AQTRunCat features (read-only) ..."
        if ! as_user "$bindir/aqtruncatctl" --missing-deps; then
            warn "the optional dependency audit could not be completed; run: $bindir/aqtruncatctl --missing-deps"
        fi
    fi

    say "AQTRunCat is installed: $installed_bin"
    if [[ $install_plasmoid == true ]]; then
        say "Plasma widget installed: $datadir/plasma/plasmoids/$PLASMOID_ID"
        say "  Add it yourself: right-click a panel or the desktop > Add or Manage Widgets > AQTRunCat."
        if ((had_plasmoid)); then
            offer_plasma_restart
        fi
        if ! data_dir_searched; then
            warn "Plasma looks for widgets (and D-Bus for the backend) in XDG_DATA_DIRS=${XDG_DATA_DIRS:-/usr/local/share:/usr/share}, which does not contain $datadir: add it to XDG_DATA_DIRS of your session, or install with the default prefix or --local"
        fi
    elif ((had_plasmoid)); then
        say "The Plasma widget of this installation was removed (a widget still on a panel shows as unavailable until you remove it)."
    fi
    if [[ $scope == local && ":$PATH:" != *":$bindir:"* ]]; then
        warn "$bindir is not in your PATH. The menu entry and autostart work anyway; to run \"aqtruncat\" from a terminal, add it to PATH yourself."
    fi
    if ((stopped_instances)); then
        say "AQTRunCat was closed for the update and is not restarted automatically."
    fi
    say "Start it from the application menu (AQTRunCat) or with: $installed_bin"
}

# --- uninstall ------------------------------------------------------------

do_uninstall() {
    local -a files=()
    local line
    if [[ ! -f $manifest ]]; then
        if [[ -e $installed_bin ]]; then
            die "$installed_bin exists but there is no install manifest at $manifest (installed by a package or by hand?); nothing removed"
        fi
        die "no AQTRunCat $scope installation found (no $manifest)"
    fi
    check_manifest
    while IFS= read -r line; do files+=("$line"); done < <(read_manifest)
    choose_privileges
    authenticate_sudo "run the uninstallation again when sudo works"
    stop_running_instances

    if autostart_points_to "$installed_bin"; then
        if [[ -x $installed_bin ]] && as_user "$installed_bin" autostart disable; then
            :
        else
            rm -f -- "$config_home/autostart/aqtruncat.desktop"
            say "Removed the autostart entry"
        fi
    elif [[ -f $config_home/autostart/aqtruncat.desktop ]]; then
        say "Keeping $config_home/autostart/aqtruncat.desktop: it starts another AQTRunCat installation"
    fi

    say "Removing ${#files[@]} files ..."
    local f
    for f in "${files[@]}"; do remove_file "$f"; done
    "${SUDO[@]}" rm -f -- "$manifest"
    remove_owned_dirs "${files[@]}"
    remove_empty_dir "$(dirname -- "$manifest")"
    refresh_desktop_caches
    reload_session_bus

    say "AQTRunCat ($scope) is uninstalled."
    if printf '%s\n' "${files[@]}" | grep -qxF "$datadir/plasma/plasmoids/$PLASMOID_ID/metadata.json"; then
        say "The Plasma widget was removed too: an AQTRunCat widget still on a panel or the desktop shows as unavailable; remove it there (Plasma's layout is not edited)."
    fi
    say "Kept (delete them yourself if you no longer want them):"
    say "  settings:                    $config_home/AQTRunCat"
    say "  user runners, Custom Metrics: $data_home/AQTRunCat"
}

# --- main -----------------------------------------------------------------

[[ $action != install ]] || decide_plasmoid
print_options
if [[ $install_plasmoid == missing ]]; then
    say "The Plasma widget needs the Qt 6 QML modules of KDE Plasma 6. Detected: $os_name (family: $family). Install them with:"
    say "  $(plasma_deps_command)"
    die "--plasmoid: KDE Plasma 6 is not available (missing ${plasmoid_reason##*missing: }); run without --plasmoid to install AQTRunCat without the widget"
fi
if ((dry_run)); then
    if [[ $action == uninstall && -f $manifest ]]; then
        check_manifest
        say "Would remove:"
        read_manifest | sed 's/^/  /'
    fi
    exit 0
fi

case $action in
    install) do_install ;;
    uninstall) do_uninstall ;;
esac
