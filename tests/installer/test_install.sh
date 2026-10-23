#!/usr/bin/env bash
# Tests for install.sh: command line, distribution detection, XDG paths and a
# real install/reinstall/uninstall cycle into temporary directories (never
# /usr, never the real home). Usage: test_install.sh SOURCE_DIR BUILD_DIR

set -uo pipefail

source_dir=$(cd -- "$1" && pwd)
build_dir=$(cd -- "$2" && pwd)
installer=$source_dir/install.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/aqtruncat-installer-test.XXXXXX")
started=() # exact PIDs this test started; only these are ever signalled here
cleanup_test() {
    local pid
    for pid in "${started[@]}"; do kill -KILL "$pid" 2> /dev/null; done
    wait 2> /dev/null
    chmod -R u+w -- "$work" 2> /dev/null
    rm -rf -- "$work"
}
trap cleanup_test EXIT

failures=0
checks=0
pass() { checks=$((checks + 1)); }
fail() {
    checks=$((checks + 1))
    failures=$((failures + 1))
    printf 'FAIL: %s\n' "$*"
}
lacks() { ! grep -qF -- "$1" <<< "$2"; } # lacks NEEDLE TEXT
check() { # check DESCRIPTION COMMAND...
    local description=$1
    shift
    if "$@"; then pass; else fail "$description"; fi
}

# A fake sudo that records any call: --local and writable prefixes must never use it.
mkdir -p "$work/fakebin"
cat > "$work/fakebin/sudo" <<EOF
#!/bin/sh
echo "\$@" >> "$work/sudo-calls"
exit 1
EOF
chmod +x "$work/fakebin/sudo"
# Plasma tools: the installer never edits panels, and restarts plasmashell
# only when asked to (opt-in, after a widget update). Every run below looks
# for a fake plasmashell (AQTRUNCAT_PLASMASHELL_NAME) and finds these fakes
# first in PATH: the real Plasma session is never seen nor touched.
for tool in plasmashell kquitapp6 kstart kpackagetool6 qdbus6 qdbus; do
    printf '#!/bin/sh\necho "%s $*" >> "%s/plasma-calls"\nexit 0\n' "$tool" "$work" > "$work/fakebin/$tool"
    chmod +x "$work/fakebin/$tool"
done
# systemctl: is-active fails with "plasma-no-unit"; restart kills the fake
# plasmashell (and does not start it) with "plasma-dies".
cat > "$work/fakebin/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$work/systemctl-calls"
case "\$*" in
    *is-active*) [ -e "$work/plasma-no-unit" ] && exit 3 ;;
    *restart*) [ -e "$work/plasma-dies" ] && kill "\$(cat "$work/fakeplasma.pid")" ;;
esac
exit 0
EOF
chmod +x "$work/fakebin/systemctl"

# Fake Qt 6 QML trees: KDE Plasma 6 (every module the widget imports), none.
plasma_modules="org/kde/plasma/plasmoid org/kde/plasma/core org/kde/plasma/components org/kde/plasma/extras org/kde/plasma/workspace/dbus org/kde/kirigami org/kde/kcmutils QtQuick/Dialogs"
fake_qml() { # fake_qml DIR VERSION [MODULE-TO-LEAVE-OUT]
    local module
    for module in $plasma_modules; do
        [[ $module == "${3:-}" ]] && continue
        mkdir -p "$1/$module"
        echo "module ${module//\//.}" > "$1/$module/qmldir"
    done
    echo "$2" > "$1/org/kde/plasma/plasmoid/kde-qmlmodule.version"
}
fake_qml "$work/qml-plasma6" 6.8.0
mkdir -p "$work/qml-none"

# Run the installer with a clean, temporary user environment.
run() {
    env -i PATH="$work/fakebin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 \
        LANG=C.UTF-8 AQTRUNCAT_QML_DIRS="${QML_DIRS:-$work/qml-none}" ${XDG_TEST_ENV:+$XDG_TEST_ENV} bash "$installer" "$@"
}
out=""
rc=0
capture() { out=$(run "$@" 2>&1); rc=$?; }

# --- help and usage errors -------------------------------------------------

capture --help
check "--help exits 0" test "$rc" -eq 0
for needle in "--local" "-A, --autostart=true" "-na, --autostart=false" "-U, --uninstall" \
    "ON by default" "Never removed" "Ubuntu 24.04" "sudo is used only" "Examples:" "--no-build" \
    "aqtruncat/install-build" "--plasmoid" "--no-plasmoid" "Add or Manage Widgets" "--restart-plasma" "--no-restart-plasma" \
    "systemctl --user restart plasma-plasmashell" "systemctl --user start plasma-plasmashell"; do
    check "--help mentions $needle" grep -qF -- "$needle" <<< "$out"
done
help_text=$out
capture -h
check "-h equals --help" test "$out" = "$help_text"

for bad in "--bogus" "-x" "--autostart=maybe" "-A -na" "--autostart=true --autostart=false" \
    "-na --autostart=true" "-U -A" "--uninstall -na" "--local --prefix=/opt/x" "--prefix=relative" \
    "--prefix=/" "-U --build-dir=/tmp" "-U --no-build" "--plasmoid --no-plasmoid" "--no-plasmoid --plasmoid" \
    "-U --plasmoid" "--uninstall --no-plasmoid" "--restart-plasma --no-restart-plasma" "-U --restart-plasma" \
    "--restart-plasma --no-plasmoid"; do
    # shellcheck disable=SC2086 # split the option list on purpose
    capture $bad
    check "\"$bad\" is a usage error (2)" test "$rc" -eq 2
    check "\"$bad\" explains the error" grep -qF 'Run "./install.sh --help"' <<< "$out"
done
capture --bogus
check "unknown option is named" grep -qF 'unknown option "--bogus"' <<< "$out"

# --- option semantics (dry run: nothing is changed) --------------------------

plan() { capture --dry-run "$@"; }
plan;                     check "default: system install" grep -qF "  action            = install (default)" <<< "$out"; check "default: system install (type)" grep -qF "  installation type = system-wide, all users" <<< "$out"
                          check "default: autostart on" grep -qF "  autostart         = true" <<< "$out"
plan -A;                  check "-A enables autostart" grep -qF "  autostart         = true" <<< "$out"
plan --autostart=true;    check "--autostart=true enables autostart" grep -qF "  autostart         = true" <<< "$out"
plan -na;                 check "-na disables autostart" grep -qF "  autostart         = false" <<< "$out"
plan --autostart=false;   check "--autostart=false disables autostart" grep -qF "  autostart         = false" <<< "$out"
plan -A -A;               check "repeating the same option is fine" test "$rc" -eq 0
plan --local;             check "--local installs locally" grep -qF "  installation type = local, current user only (--local)" <<< "$out"
                          check "--local needs no privileges" grep -qF "  privileges        = none" <<< "$out"
plan -U;                  check "-U uninstalls system-wide" grep -qF "  action            = uninstall (-U)" <<< "$out"; check "-U uninstalls system-wide (type)" grep -qF "  installation type = system-wide" <<< "$out"
plan --uninstall;         check "--uninstall equals -U" grep -qF "  action            = uninstall (-U)" <<< "$out"; check "--uninstall equals -U (type)" grep -qF "  installation type = system-wide" <<< "$out"
plan --local -U;          check "--local -U uninstalls locally" grep -qF "  action            = uninstall (-U)" <<< "$out"; check "--local -U uninstalls locally (type)" grep -qF "  installation type = local" <<< "$out"
plan -na --local;         check "option order does not matter" grep -qF "  autostart         = false" <<< "$out"
plan --prefix=/opt/aqt/;  check "--prefix sets the binary path" grep -qF "  binary            = /opt/aqt/bin/aqtruncat" <<< "$out"
plan;                     check "default: build from scratch in the cache" grep -qF "  rebuild           = true: from scratch in $work/home/.cache/aqtruncat/install-build (default)" <<< "$out"
plan --no-build;          check "--no-build reuses that build" grep -qF "  rebuild           = false: reuse $work/home/.cache/aqtruncat/install-build (--no-build" <<< "$out"
XDG_TEST_ENV="XDG_CACHE_HOME=$work/xc" capture --dry-run
                          check "XDG_CACHE_HOME holds the build" grep -qF "  rebuild           = true: from scratch in $work/xc/aqtruncat/install-build" <<< "$out"
plan -U;                  check "uninstall has no build step" test "$(grep -c '^  rebuild ' <<< "$out")" -eq 0

# --- distribution detection ------------------------------------------------

family_of() { # family_of "OS-RELEASE CONTENT"
    printf '%s\n' "$1" > "$work/os-release"
    XDG_TEST_ENV="AQTRUNCAT_OS_RELEASE=$work/os-release" capture --dry-run
    sed -n 's/.*(family: \([a-z]*\)).*/\1/p' <<< "$out"
}
check "Arch" test "$(family_of 'ID=arch')" = arch
check "Manjaro (ID_LIKE=arch)" test "$(family_of $'ID=manjaro\nID_LIKE=arch')" = arch
check "EndeavourOS" test "$(family_of $'ID=endeavouros\nID_LIKE="arch"')" = arch
check "Fedora" test "$(family_of $'ID=fedora\nVERSION_ID=44')" = fedora
check "Rocky (rhel centos fedora)" test "$(family_of $'ID="rocky"\nID_LIKE="rhel centos fedora"')" = fedora
check "Debian" test "$(family_of 'ID=debian')" = debian
check "Ubuntu" test "$(family_of $'ID=ubuntu\nID_LIKE=debian')" = debian
check "Linux Mint (ubuntu debian)" test "$(family_of $'ID=linuxmint\nID_LIKE="ubuntu debian"')" = debian
check "unknown distribution" test "$(family_of 'ID=gentoo')" = unknown
printf 'ID=gentoo\nPRETTY_NAME="Gentoo Linux"\n' > "$work/os-release"
XDG_TEST_ENV="AQTRUNCAT_OS_RELEASE=$work/os-release" capture --dry-run
check "pretty name is reported" grep -qF "  distribution      = Gentoo Linux (family: unknown)" <<< "$out"
XDG_TEST_ENV="AQTRUNCAT_OS_RELEASE=$work/missing" capture --dry-run
check "missing os-release is unknown" grep -qF "(family: unknown)" <<< "$out"

# --- Plasma widget: detection and decision (F4.2) ----------------------------

plan
check "no Plasma 6: widget left out by default" grep -qF "  plasmoid          = false (default: KDE Plasma 6 not found, missing org.kde.plasma.plasmoid" <<< "$out"
QML_DIRS=$work/qml-plasma6 plan
check "Plasma 6 found: widget installed by default" grep -qF "  plasmoid          = true (default: KDE Plasma 6.8.0 found)" <<< "$out"
QML_DIRS="$work/qml-none:$work/qml-plasma6" plan
check "every QML directory is searched" grep -qF "  plasmoid          = true (default: KDE Plasma 6.8.0 found)" <<< "$out"
QML_DIRS=$work/qml-plasma6 plan --no-plasmoid
check "--no-plasmoid with Plasma 6: no widget" grep -qF "  plasmoid          = false (--no-plasmoid)" <<< "$out"
QML_DIRS=$work/qml-plasma6 plan --plasmoid
check "--plasmoid with Plasma 6" grep -qF "  plasmoid          = true (--plasmoid: KDE Plasma 6.8.0 found)" <<< "$out"
plan --plasmoid
check "--plasmoid without Plasma 6 fails" test "$rc" -eq 1
check "--plasmoid without Plasma 6 says why" grep -qF -- "--plasmoid: KDE Plasma 6 is not available" <<< "$out"
check "--plasmoid without Plasma 6 names the packages" grep -qF "Install them with:" <<< "$out"
check "--plasmoid without Plasma 6 suggests going without it" grep -qF "run without --plasmoid" <<< "$out"
fake_qml "$work/qml-nodbus" 6.8.0 org/kde/plasma/workspace/dbus
QML_DIRS=$work/qml-nodbus plan
check "a missing module is named" grep -qF "missing org.kde.plasma.workspace.dbus)" <<< "$out"
fake_qml "$work/qml-plasma5" 5.27.11
QML_DIRS=$work/qml-plasma5 plan
check "Plasma 5 is not Plasma 6" grep -qF "missing Plasma 6 (found Plasma 5.27.11))" <<< "$out"
printf 'ID=arch\n' > "$work/os-release"
XDG_TEST_ENV="AQTRUNCAT_OS_RELEASE=$work/os-release" capture --dry-run --plasmoid
check "Arch: Plasma packages for pacman" grep -qF "sudo pacman -S --needed plasma-workspace" <<< "$out"
plan -U
check "uninstall has no widget decision" test "$(grep -c '^  plasmoid ' <<< "$out")" -eq 0

# --- dependency checks (fake Qt tools) ---------------------------------------

# A qmake6 that, like real qmake, prints its usage to stdout for "--query".
mkdir -p "$work/qtbin"
cat > "$work/qtbin/qmake6" <<'EOF'
#!/bin/sh
if [ "$1" = "-query" ] && [ "$2" = "QT_VERSION" ]; then echo "${FAKE_QT_VERSION:-6.12.0}"; exit 0; fi
echo "Usage: qmake [mode] [options] [files]"; exit 0
EOF
chmod +x "$work/qtbin/qmake6"
qt_check() { # qt_check VERSION -> the installer's version logic applied to the fake qmake only
    # shellcheck disable=SC2016 # evaluated by the inner bash
    env -i PATH="/usr/bin:/bin" FAKE_QT_VERSION="$1" bash -c '
            source <(sed -n "/^version_at_least()/,/^}/p; /^qt_version_from()/,/^}/p" "$0")
            v=$(qt_version_from "$1") && echo "qt=$v" && version_at_least "$v" 6.5 && echo ok || echo too-old' \
        "$installer" "$work/qtbin/qmake6"
}
check "qmake usage text is not taken for a version" grep -qx "qt=6.12.0" <<< "$(qt_check 6.12.0)"
check "Qt 6.12 is new enough" grep -qx "ok" <<< "$(qt_check 6.12.0)"
check "Qt 6.5.0 is new enough" grep -qx "ok" <<< "$(qt_check 6.5.0)"
check "Qt 6.4.2 is too old" grep -qx "too-old" <<< "$(qt_check 6.4.2)"
check "Qt 6.10 is new enough (numeric, not lexical)" grep -qx "ok" <<< "$(qt_check 6.10.2)"

# --- XDG paths -------------------------------------------------------------

XDG_TEST_ENV="XDG_BIN_HOME=$work/x/bin XDG_DATA_HOME=$work/x/data XDG_STATE_HOME=$work/x/state" capture --dry-run --local
check "XDG_BIN_HOME is honoured" grep -qF "  binary            = $work/x/bin/aqtruncat" <<< "$out"
check "XDG_DATA_HOME is honoured" grep -qF "  data              = $work/x/data" <<< "$out"
check "XDG_STATE_HOME holds the manifest" grep -qF "  manifest          = $work/x/state/aqtruncat/install-manifest.txt" <<< "$out"
XDG_TEST_ENV="XDG_DATA_HOME=relative/data" capture --dry-run --local
check "relative XDG values are ignored" grep -qF "  data              = $work/home/.local/share" <<< "$out"

# --- --no-build: reuse only a matching, up-to-date build ---------------------

cache_build=$work/home/.cache/aqtruncat/install-build
capture --local --no-build
check "--no-build without a build fails" test "$rc" -eq 1
check "--no-build without a build says what to do" grep -qF "there is no build to reuse" <<< "$out"
check "--no-build without a build installs nothing" test ! -e "$work/home/.local/bin/aqtruncat"

mkdir -p "$cache_build"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=/somewhere/else\nCMAKE_BUILD_TYPE:STRING=Release\n' > "$cache_build/CMakeCache.txt"
capture --local --no-build
check "a build of another source tree is refused" grep -qF "was configured for another source tree" <<< "$out"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\nCMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_GENERATOR:INTERNAL=Ninja\n' "$source_dir" > "$cache_build/CMakeCache.txt"
capture --local --no-build
check "a non-Release build is refused" grep -qF "is not a Release build" <<< "$out"
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\nCMAKE_BUILD_TYPE:STRING=Release\nCMAKE_GENERATOR:INTERNAL=Ninja\n' "$source_dir" > "$cache_build/CMakeCache.txt"
capture --local --no-build
check "an incomplete or stale build is refused" grep -qF "is not up to date with the sources" <<< "$out"
check "a refused build installs nothing" test ! -e "$work/home/.local/bin/aqtruncat"
rm -rf "$cache_build"

# A build of another version: warning and a question; N (default, and
# without a terminal) builds the sources, y installs the previous build.
source_version=$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$source_dir/CMakeLists.txt" | head -n 1)
old_build=$work/old-build
mkdir -p "$cache_build/src" "$old_build/src"
for dir in "$cache_build" "$old_build"; do
    printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\nCMAKE_BUILD_TYPE:STRING=Release\nCMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_PROJECT_VERSION:STATIC=0.9.0\n' "$source_dir" > "$dir/CMakeCache.txt"
    printf '#!/bin/sh\necho "AQTRunCat 0.9.0"\n' > "$dir/src/aqtruncat"
    chmod +x "$dir/src/aqtruncat"
done
capture --dry-run --local --no-build
check "--dry-run shows both versions" grep -qF "build 0.9.0, sources $source_version: you will be asked" <<< "$out"
capture --local --no-build -na
check "another version is reported" grep -qF "is AQTRunCat 0.9.0, these sources are AQTRunCat $source_version" <<< "$out"
check "without a terminal the new version is built" grep -qF "building the new version $source_version" <<< "$out"
check "building anew does not reuse the old build" grep -qF "Building the new version instead of reusing" <<< "$out"
check "the old build is never installed without asking" test ! -e "$work/home/.local/bin/aqtruncat"
if command -v script > /dev/null; then
    tty_out=$(printf 'y\n' | env -i PATH="$work/fakebin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 LANG=C.UTF-8 \
        script -qec "bash '$installer' --local --no-build --build-dir='$old_build' -na" /dev/null 2>&1)
    check "the question names both versions" grep -qF "Install the previous version 0.9.0 (y) or build the new version $source_version (N)? [y/N]" <<< "$tty_out"
    check "y reuses the previous build without compiling" grep -qF "nothing is compiled" <<< "$tty_out"
    check "y skips the up-to-date check" lacks "is not up to date" "$tty_out"
    tty_out=$(printf '\n' | env -i PATH="$work/fakebin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 LANG=C.UTF-8 \
        script -qec "bash '$installer' --local --no-build --build-dir='$old_build' -na" /dev/null 2>&1)
    check "Enter (N) builds the new version" grep -qF "Building the new version instead of reusing" <<< "$tty_out"
fi
rm -rf "$cache_build" "$old_build"

# The default build directory is only emptied if the installer created it.
mkdir -p "$cache_build"
echo precious > "$cache_build/not-ours.txt"
capture --local -na
check "a foreign build directory is not emptied" test "$rc" -eq 1
check "the refusal is explained" grep -qF "was not created by the AQTRunCat installer" <<< "$out"
check "its content is kept" test -f "$cache_build/not-ours.txt"
rm -rf "$cache_build"

# An up-to-date build tree is installed without compiling. Bring it to "no
# work to do" first: right after an incremental rebuild ninja (1.13 here)
# can need more than one pass before its dry run is empty.
capture --local --build-dir="$build_dir" -na
check "--build-dir brings the build up to date" test "$rc" -eq 0
for _ in 1 2 3 4 5 6 7 8; do
    [[ $(cmake --build "$build_dir" -- -n 2> /dev/null) == *"no work to do"* ]] && break
    cmake --build "$build_dir" > /dev/null 2>&1
done
capture --local --no-build --build-dir="$build_dir" -na
check "--no-build with an up-to-date build succeeds" test "$rc" -eq 0
check "--no-build compiles nothing" grep -qF "nothing is compiled" <<< "$out"
check "--no-build installed the binary" test -x "$work/home/.local/bin/aqtruncat"
capture --local -U
check "uninstall after --no-build" test "$rc" -eq 0

# --- real local install / reinstall / uninstall ----------------------------

home=$work/home
mkdir -p "$home/.config/AQTRunCat" "$home/.local/share/AQTRunCat/runners/mine" "$home/.local/share/AQTRunCat/metrics"
printf '[runner]\nid=coffee\n' > "$home/.config/AQTRunCat/AQTRunCat.ini"
echo frame > "$home/.local/share/AQTRunCat/runners/mine/frame-0.png"
echo '{}' > "$home/.local/share/AQTRunCat/metrics/producer.json"
entry=$home/.config/autostart/aqtruncat.desktop
bin=$home/.local/bin/aqtruncat
manifest=$home/.local/state/aqtruncat/install-manifest.txt

# A Claude Code integration enabled by an earlier version (no provenance
# field, adapter at an old path, context option on) is upgraded in place.
old_package=$home/.local/share/AQTRunCat/metrics/claude-code
mkdir -p "$old_package"
printf '%s' '{"id":"claude-code","title":"Claude Code","source":{"type":"command","executable":"/old/bin/aqtruncat-claude-metric","arguments":["snapshot","--context"],"output":"snapshot"}}' > "$old_package/metric.json"
echo mine > "$old_package/notes.txt"
# The same for Codex (live updates on), independently.
old_codex=$home/.local/share/AQTRunCat/metrics/codex
mkdir -p "$old_codex"
printf '%s' '{"id":"codex","title":"Codex","source":{"type":"command","executable":"/old/bin/aqtruncat-codex-metric","arguments":["snapshot","--live"],"output":"snapshot"}}' > "$old_codex/metric.json"

capture --local --build-dir="$build_dir"
check "local install succeeds" test "$rc" -eq 0
check "a real run starts with the options summary" test "$(head -n 1 <<< "$out")" = "AQTRunCat installer"
check "the summary says it is not a dry run" grep -qF "  dry run           = false" <<< "$out"
check "old Claude Code package gets its provenance" grep -qF '"integration": "claude-code"' "$old_package/metric.json"
check "old Claude Code package runs the installed adapter" grep -qF "\"executable\": \"$home/.local/bin/aqtruncat-claude-metric\"" "$old_package/metric.json"
check "old Claude Code package keeps its options" grep -qF '"--context"' "$old_package/metric.json"
check "nothing else in the package is removed" test -f "$old_package/notes.txt"
check "Codex adapter installed" test -x "$home/.local/bin/aqtruncat-codex-metric"
check "backend installed" test -x "$home/.local/bin/aqtruncatd"
check "command-line client installed" test -x "$home/.local/bin/aqtruncatctl"
service_file=$home/.local/share/dbus-1/services/io.github.psykedady.AQTRunCat.service
check "D-Bus activation file installed" test -f "$service_file"
check "activation starts this installation's backend" grep -qxF "Exec=$home/.local/bin/aqtruncatd" "$service_file"
check "activation file names the service" grep -qxF "Name=io.github.psykedady.AQTRunCat" "$service_file"
check "old Codex package gets its provenance" grep -qF '"integration": "codex"' "$old_codex/metric.json"
check "old Codex package runs the installed adapter" grep -qF "\"executable\": \"$home/.local/bin/aqtruncat-codex-metric\"" "$old_codex/metric.json"
check "old Codex package keeps live updates" grep -qF '"--live"' "$old_codex/metric.json"
check "other Custom Metrics untouched by the upgrade" test "$(cat "$home/.local/share/AQTRunCat/metrics/producer.json")" = "{}"
check "binary installed" test -x "$bin"
check "desktop entry installed" test -f "$home/.local/share/applications/aqtruncat.desktop"
check "scalable icon installed" test -f "$home/.local/share/icons/hicolor/scalable/apps/aqtruncat.svg"
check "manifest written" test -f "$manifest"
check "default install enables autostart" test -f "$entry"
check "autostart starts the installed binary" grep -qxF "Exec=\"$bin\"" "$entry"
check "menu entry starts the installed binary" grep -qxF "Exec=$bin" "$home/.local/share/applications/aqtruncat.desktop"
check "PATH warning for ~/.local/bin" grep -qF "is not in your PATH" <<< "$out"
if command -v desktop-file-validate > /dev/null; then
    check "installed desktop entry is valid" desktop-file-validate "$home/.local/share/applications/aqtruncat.desktop"
    check "autostart entry is valid" desktop-file-validate "$entry"
fi
check "installed binary runs" "$bin" --version

capture --local --build-dir="$build_dir" -na
check "reinstall -na succeeds" test "$rc" -eq 0
check "reinstall is detected" grep -qF "Updating the existing installation" <<< "$out"
check "-na removes the autostart entry" test ! -e "$entry"
check "-na saves the preference" grep -qx "autostart=false" "$home/.config/AQTRunCat/AQTRunCat.ini"

# A file from an older version, recorded in the manifest, disappears on update.
mkdir -p "$home/.local/share/aqtruncat-old"
echo old > "$home/.local/share/aqtruncat-old/obsolete.txt"
echo "$home/.local/share/aqtruncat-old/obsolete.txt" >> "$manifest"
capture --local --build-dir="$build_dir" -A
check "reinstall -A succeeds" test "$rc" -eq 0
check "-A creates the autostart entry again" test -f "$entry"
check "exactly one autostart entry" test "$(find "$home/.config/autostart" -type f | wc -l)" -eq 1
check "obsolete file removed on update" test ! -e "$home/.local/share/aqtruncat-old/obsolete.txt"
check "user settings kept across reinstalls" grep -qx "id=coffee" "$home/.config/AQTRunCat/AQTRunCat.ini"

capture --local --uninstall
check "local uninstall succeeds" test "$rc" -eq 0
check "binary removed" test ! -e "$bin"
check "desktop entry removed" test ! -e "$home/.local/share/applications/aqtruncat.desktop"
check "icons removed" test ! -e "$home/.local/share/icons/hicolor/scalable/apps/aqtruncat.svg"
check "manifest removed" test ! -e "$manifest"
check "autostart entry removed" test ! -e "$entry"
check "documentation directory removed" test -z "$(find "$home/.local/share/doc" -mindepth 1 2> /dev/null)"
check "settings kept" grep -qx "id=coffee" "$home/.config/AQTRunCat/AQTRunCat.ini"
check "user runner kept" test -f "$home/.local/share/AQTRunCat/runners/mine/frame-0.png"
check "Custom Metrics kept" test -f "$home/.local/share/AQTRunCat/metrics/producer.json"
check "kept data is reported" grep -qF "user runners, Custom Metrics" <<< "$out"
capture --local -U
check "uninstalling twice fails cleanly" test "$rc" -eq 1
check "nothing to uninstall is explained" grep -qF "no AQTRunCat local installation found" <<< "$out"

# --- system mode into a writable prefix (no sudo needed) -------------------

prefix=$work/prefix
capture --prefix="$prefix" --build-dir="$build_dir" -na
check "system install into a writable prefix" test "$rc" -eq 0
check "system binary installed" test -x "$prefix/bin/aqtruncat"
check "system manifest under the prefix" test -f "$prefix/share/aqtruncat/install-manifest.txt"
check "system -na leaves no autostart entry" test ! -e "$entry"
capture --prefix="$prefix" --build-dir="$build_dir"
check "system reinstall enables per-user autostart" grep -qxF "Exec=\"$prefix/bin/aqtruncat\"" "$entry"
capture --prefix="$prefix" -U
check "system uninstall succeeds" test "$rc" -eq 0
check "system files removed" test -z "$(find "$prefix" -type f)"
check "system uninstall removes its autostart entry" test ! -e "$entry"

# --- Plasma widget: real local and system installs (F4.2) -------------------

widget=$home/.local/share/plasma/plasmoids/io.github.psykedady.aqtruncat
# An unrelated widget and a Plasma panel layout of the user: never touched.
other_widget=$home/.local/share/plasma/plasmoids/org.example.other
mkdir -p "$other_widget"
echo '{"KPlugin": {"Id": "org.example.other"}}' > "$other_widget/metadata.json"
layout=$home/.config/plasma-org.kde.plasma.desktop-appletsrc
printf '[Containments][1][Applets][7]\nplugin=io.github.psykedady.aqtruncat\n' > "$layout"
cp "$layout" "$work/layout-before"

QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na
check "local install with Plasma 6 succeeds" test "$rc" -eq 0
check "widget metadata installed" test -f "$widget/metadata.json"
check "widget QML installed" test -f "$widget/contents/ui/main.qml"
check "widget configuration schema installed" test -f "$widget/contents/config/main.xml"
check "widget files are in the manifest" grep -qxF "$widget/contents/ui/main.qml" "$manifest"
check "bundled runner files installed" test -f "$home/.local/share/aqtruncat/runners/cat/cat-frame-0.png"
check "bundled runner metadata installed" test -f "$home/.local/share/aqtruncat/runners/coffee/metadata.json"
check "Walking Goose frames installed" test -f "$home/.local/share/aqtruncat/runners/walkinggoose/goose-frame-5.png"
check "Walking Goose metadata installed" test -f "$home/.local/share/aqtruncat/runners/walkinggoose/metadata.json"
check "Party Parrot frames installed" test -f "$home/.local/share/aqtruncat/runners/partyparrot/parrot-frame-9.png"
check "Party Parrot metadata installed" test -f "$home/.local/share/aqtruncat/runners/partyparrot/metadata.json"
check "Engine frames installed" test -f "$home/.local/share/aqtruncat/runners/engine/engine-frame-9.png"
check "Engine metadata installed" test -f "$home/.local/share/aqtruncat/runners/engine/metadata.json"
check "Rickroll frames installed" test -f "$home/.local/share/aqtruncat/runners/rickroll/rickroll-frame-30.png"
check "Rickroll metadata installed" test -f "$home/.local/share/aqtruncat/runners/rickroll/metadata.json"
check "Rickroll files are in the manifest" grep -qxF "$home/.local/share/aqtruncat/runners/rickroll/rickroll-frame-0.png" "$manifest"
check "new runner files are in the manifest" grep -qxF "$home/.local/share/aqtruncat/runners/partyparrot/parrot-frame-0.png" "$manifest"
check "the user is told how to add the widget" grep -qF "Add or Manage Widgets > AQTRunCat" <<< "$out"
check "the widget id matches its directory" grep -qF '"Id": "io.github.psykedady.aqtruncat"' "$widget/metadata.json"
check "the widget version is the AQTRunCat version" grep -qF "\"Version\": \"$source_version\"" "$widget/metadata.json"
check "--local: no XDG_DATA_DIRS warning" lacks "XDG_DATA_DIRS" "$out"

QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na
check "updating the widget, no Plasma running: nothing to reload" lacks "Restart Plasma" "$out"

QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --no-plasmoid
check "--no-plasmoid update succeeds" test "$rc" -eq 0
check "--no-plasmoid removes this installation's widget" test ! -e "$widget"
check "the removal is reported" grep -qF "Plasma widget of this installation was removed" <<< "$out"
check "the tray application stays installed" test -x "$bin"
check "bundled runner files stay (the backend uses them)" test -f "$home/.local/share/aqtruncat/runners/cat/cat-frame-0.png"

capture --local --build-dir="$build_dir" -na --plasmoid
check "--plasmoid without Plasma 6: nothing changed" test "$rc" -eq 1
check "--plasmoid failure installs no widget" test ! -e "$widget"

QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --plasmoid
check "--plasmoid installs the widget again" test -f "$widget/metadata.json"
capture --local -U
check "uninstall with the widget succeeds" test "$rc" -eq 0
check "uninstall removes the widget package" test ! -e "$widget"
check "uninstall removes the bundled runner files" test ! -e "$home/.local/share/aqtruncat"
check "uninstall reports the widget left on panels" grep -qF "shows as unavailable" <<< "$out"
check "an unrelated widget is kept" test -f "$other_widget/metadata.json"
check "the Plasma layout is never edited" cmp -s "$layout" "$work/layout-before"
check "plasmashell and Plasma tools were never called" test ! -e "$work/plasma-calls"
check "systemctl was never called" test ! -e "$work/systemctl-calls"
check "user runners kept with the widget" test -f "$home/.local/share/AQTRunCat/runners/mine/frame-0.png"
rm -rf "$other_widget" "$layout"

# --- Plasma restart after a widget update (opt-in) --------------------------

# A fake plasmashell of this user: a copy of sleep, so that its process name
# is the one the installer is told to look for.
mkdir -p "$work/fakeplasma"
cp -- "$(command -v sleep)" "$work/fakeplasma/fakeplasmashell"
start_fake_plasma() {
    "$work/fakeplasma/fakeplasmashell" 600 &
    started+=("$!")
    echo "$!" > "$work/fakeplasma.pid"
    local i
    for i in $(seq 50); do
        [[ $(cat "/proc/$!/comm" 2> /dev/null) == fakeplasmashell ]] && return 0
        sleep 0.1
    done
}
# The installer on a pseudo-terminal fed with INPUT.
plasma_tty() { # plasma_tty INPUT [OPTION...]
    local input=$1
    shift
    out=$(printf '%s' "$input" | env -i PATH="$work/fakebin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" \
        AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 LANG=C.UTF-8 AQTRUNCAT_QML_DIRS="$work/qml-plasma6" \
        script -qec "bash '$installer' --local --build-dir='$build_dir' -na $*" /dev/null 2>&1)
    rc=$?
}
restart_call="systemctl --user restart plasma-plasmashell.service"

QML_DIRS=$work/qml-plasma6 capture --dry-run --local
check "--dry-run: Plasma restart asked by default" grep -qF "  restart Plasma    = ask (default" <<< "$out"
QML_DIRS=$work/qml-plasma6 capture --dry-run --local --restart-plasma
check "--dry-run: --restart-plasma" grep -qF "  restart Plasma    = true (--restart-plasma" <<< "$out"

QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na
check "first widget install: nothing to reload" lacks "Restart Plasma" "$out"
start_fake_plasma
QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na
check "update without a terminal succeeds" test "$rc" -eq 0
check "without a terminal Plasma is not restarted" test ! -e "$work/systemctl-calls"
check "without a terminal there is no Plasma question" lacks "Restart Plasma now" "$out"
check "the old widget version is explained" grep -qF "keeps the previous version until Plasma starts again" <<< "$out"
check "the restart command is suggested" grep -qF "systemctl --user restart plasma-plasmashell" <<< "$out"
if command -v script > /dev/null; then
    plasma_tty $'\n'
    check "on a terminal the user is asked" grep -qF "Restart Plasma now so that the widget already shown uses the new version?" <<< "$out"
    check "the question says how long Plasma disappears" grep -qF "up to about a minute" <<< "$out"
    check "Enter (N): Plasma is not restarted" test ! -e "$work/systemctl-calls"
    plasma_tty $'y\n'
    check "y: plasmashell restarted through its systemd unit" grep -qxF "$restart_call" "$work/systemctl-calls"
    check "y: the user session is used" grep -qF "systemctl --user --quiet is-active plasma-plasmashell.service" "$work/systemctl-calls"
    check "y: Plasma is back" grep -qF "Plasma is running again" <<< "$out"
    check "y: the installation succeeded" test "$rc" -eq 0
    rm -f "$work/systemctl-calls"
    plasma_tty "" --no-restart-plasma
    check "--no-restart-plasma: no question" lacks "Restart Plasma now" "$out"
    check "--no-restart-plasma: Plasma is not restarted" test ! -e "$work/systemctl-calls"
fi
QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --restart-plasma
check "--restart-plasma: restarted without a terminal" grep -qxF "$restart_call" "$work/systemctl-calls"
check "--restart-plasma: no question" lacks "Restart Plasma now" "$out"
check "--restart-plasma: no second start when Plasma is back" test "$(grep -cxF 'systemctl --user start plasma-plasmashell.service' "$work/systemctl-calls")" -eq 0
rm -f "$work/systemctl-calls"

# Plasma not started by systemd: kquitapp6, then kstart.
touch "$work/plasma-no-unit"
QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --restart-plasma
check "without the systemd unit: kquitapp6 then kstart" test "$(cat "$work/plasma-calls" 2> /dev/null)" = $'kquitapp6 plasmashell\nkstart plasmashell'
check "without the systemd unit: no restart through systemd" lacks "restart" "$(cat "$work/systemctl-calls")"
rm -f "$work/plasma-no-unit" "$work/plasma-calls" "$work/systemctl-calls"

# Plasma does not come back: one more start, then the commands to run.
touch "$work/plasma-dies"
QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --restart-plasma
check "Plasma down: the installation still succeeds" test "$rc" -eq 0
check "Plasma down: reset-failed first" grep -qxF "systemctl --user reset-failed plasma-plasmashell.service" "$work/systemctl-calls"
check "Plasma down: start once more" grep -qxF "systemctl --user start plasma-plasmashell.service" "$work/systemctl-calls"
check "Plasma down: reported after the timeout" grep -qF "Plasma did not come back within 3 s" <<< "$out"
check "Plasma down: how to start it again" grep -qF "systemctl --user start plasma-plasmashell" <<< "$out"
rm -f "$work/plasma-dies" "$work/systemctl-calls"

# No Plasma of this user running (the fake one was stopped above): nothing to do.
QML_DIRS=$work/qml-plasma6 capture --local --build-dir="$build_dir" -na --restart-plasma
check "no Plasma running: nothing restarted" test ! -e "$work/systemctl-calls"
capture --local -U
check "uninstall after the Plasma restart cases" test "$rc" -eq 0
rm -f "$work/plasma-calls"

QML_DIRS=$work/qml-plasma6 capture --prefix="$prefix" --build-dir="$build_dir" -na
check "system install with the widget" test -f "$prefix/share/plasma/plasmoids/io.github.psykedady.aqtruncat/metadata.json"
check "a prefix Plasma does not search is reported" grep -qF "which does not contain $prefix/share" <<< "$out"
QML_DIRS=$work/qml-plasma6 XDG_TEST_ENV="XDG_DATA_DIRS=$prefix/share:/usr/share" capture --prefix="$prefix" --build-dir="$build_dir" -na
check "a prefix in XDG_DATA_DIRS: no warning" lacks "does not contain" "$out"
capture --prefix="$prefix" -U
check "system uninstall removes the widget" test -z "$(find "$prefix" -type f)"
check "no AQTRunCat directory is left in the prefix" test ! -e "$prefix/share/plasma/plasmoids/io.github.psykedady.aqtruncat" -a ! -e "$prefix/share/aqtruncat"

# --- running instances: closed before files change, nothing else touched ---

alive() { # a running (not zombie) process with this PID
    local stat
    { IFS= read -r stat < "/proc/$1/stat"; } 2> /dev/null || return 1
    stat=${stat##*) }
    [[ ${stat%% *} != Z ]]
}
running_from() { # running_from PID EXE: wait (≤ 5 s, also on a loaded machine) until PID runs EXE
    local i
    for ((i = 0; i < 50; i++)); do
        [[ $(readlink "/proc/$1/exe" 2> /dev/null) == "$2" ]] && return 0
        sleep 0.1
    done
    return 1
}
catches_term() { # catches_term PID: wait (≤ 5 s) until PID handles SIGTERM (its clean-exit handler is set)
    local i key mask
    for ((i = 0; i < 50; i++)); do
        while read -r key mask; do
            [[ $key == SigCgt: ]] && (((16#$mask >> 14) & 1)) && return 0
        done < "/proc/$1/status" 2> /dev/null
        sleep 0.1
    done
    return 1
}
start() { # start "$@" in the background and record its PID in $last
    "$@" > /dev/null 2>&1 &
    last=$!
    started+=("$last")
}

capture --local --build-dir="$build_dir" -na
check "install for the running-instance checks" test "$rc" -eq 0
# Same process name, other program; and "aqtruncat" only in a command line.
mkdir -p "$work/decoy"
cp /bin/sleep "$work/decoy/aqtruncat"
start "$work/decoy/aqtruncat" 300; decoy=$last
start bash -c 'exec -a aqtruncat sleep 300'; decoy_args=$last
# A real AQTRunCat of this installation (the metrics monitor needs no display).
start env HOME="$home" "$bin" metrics --watch; app=$last
check "the test instance runs" running_from "$app" "$bin"
check "the test instance is ready for SIGTERM" catches_term "$app"

capture --dry-run --local
check "dry run names the instance it would close" grep -qF "  running           = PID $app, would be closed" <<< "$out"
check "dry run lists nothing else" test "$(grep -c '^  running ' <<< "$out")" -eq 1
check "dry run closes nothing" alive "$app"

capture --local --build-dir="$build_dir" -na
check "update with a running instance succeeds" test "$rc" -eq 0
check "the instance is closed first" grep -qF "Closing the running AQTRunCat (PID $app)" <<< "$out"
check "closing comes before copying" test "$(grep -n 'Closing the running' <<< "$out" | cut -d: -f1)" -lt "$(grep -n '^Installing ' <<< "$out" | cut -d: -f1)"
check "the instance has exited" bash -c "! test -d /proc/$app || ! grep -q . /proc/$app/cmdline"
wait "$app"; app_status=$?
check "it exited cleanly on SIGTERM (status 0)" test "$app_status" -eq 0
check "it is not restarted" grep -qF "is not restarted automatically" <<< "$out"
check "same-name program untouched" alive "$decoy"
check "program with aqtruncat in its arguments untouched" alive "$decoy_args"

start env HOME="$home" "$bin" metrics --watch; app=$last
check "the second test instance runs" running_from "$app" "$bin"
check "the second test instance is ready for SIGTERM" catches_term "$app"
capture --local -U
check "uninstall with a running instance succeeds" test "$rc" -eq 0
check "uninstall closes it first" grep -qF "Closing the running AQTRunCat (PID $app)" <<< "$out"
wait "$app"
check "decoys still untouched after uninstall" alive "$decoy"

# The backend (F4.1) of this installation is closed too: it runs on its own
# private session bus here.
if command -v dbus-run-session > /dev/null; then
    capture --local --build-dir="$build_dir" -na
    backend_bin=$home/.local/bin/aqtruncatd
    start dbus-run-session -- env HOME="$home" XDG_CONFIG_HOME="$home/.config" XDG_DATA_HOME="$home/.local/share" "$backend_bin"
    session=$last
    backend_pid=""
    for _ in $(seq 50); do
        for dir in /proc/[0-9]*; do
            [[ $(readlink "$dir/exe" 2> /dev/null) == "$backend_bin" ]] && backend_pid=${dir#/proc/} && break
        done
        [[ -n $backend_pid ]] && break
        sleep 0.1
    done
    check "the test backend runs" test -n "$backend_pid"
    check "the test backend is ready for SIGTERM" catches_term "$backend_pid"
    capture --dry-run --local
    check "dry run names the backend it would close" grep -qF "PID $backend_pid, would be closed" <<< "$out"
    capture --local --build-dir="$build_dir" -na
    check "update closes the running backend first" grep -qF "Closing the running AQTRunCat (PID $backend_pid)" <<< "$out"
    check "the backend has exited" bash -c "! test -d /proc/$backend_pid || ! grep -q . /proc/$backend_pid/cmdline"
    wait "$session" 2> /dev/null
fi

# An instance that ignores SIGTERM: the installer gives up, changes nothing
# and never kills it.
capture --local --build-dir="$build_dir" -na
cp /bin/bash "$bin"
start "$bin" -c 'trap "" TERM; while :; do sleep 0.2; done'; stubborn=$last
check "the stuck instance runs" running_from "$stubborn" "$bin"
XDG_TEST_ENV="AQTRUNCAT_STOP_TIMEOUT=1" capture --local --build-dir="$build_dir" -na
check "a stuck instance fails the installation" test "$rc" -eq 1
check "the failure is explained" grep -qF "did not exit within 1 s after SIGTERM; nothing was changed" <<< "$out"
check "nothing was replaced" cmp -s "$bin" /bin/bash
check "the stuck instance was not killed" alive "$stubborn"
XDG_TEST_ENV="AQTRUNCAT_STOP_TIMEOUT=0" capture --dry-run
check "invalid stop timeout is a usage error" test "$rc" -eq 2
kill -KILL "$stubborn" "$decoy" "$decoy_args"
wait "$stubborn" "$decoy" "$decoy_args" 2> /dev/null
started=()
capture --local -U
check "cleanup uninstall succeeds" test "$rc" -eq 0

# --- uninstall safety ------------------------------------------------------

capture --local --build-dir="$build_dir" -na
mkdir -p "$work/outside"
echo precious > "$work/outside/precious.txt"
echo "$work/outside/precious.txt" >> "$manifest"
echo "$home/.local/share/../../outside/precious.txt" >> "$manifest"
capture --local -U
check "tampered manifest: uninstall still completes" test "$rc" -eq 0
check "file outside the destination is never removed" test -f "$work/outside/precious.txt"
check "refusal is reported" grep -qF "refusing to remove" <<< "$out"

mkdir -p "$(dirname "$manifest")"
printf '/etc/hostname\n' > "$manifest"
capture --local -U
check "foreign manifest is rejected" test "$rc" -eq 1
check "foreign manifest is explained" grep -qF "is not an AQTRunCat install manifest" <<< "$out"
check "foreign manifest: nothing removed" test -f /etc/hostname
rm -f "$manifest"

# --- sudo refused after the build (e.g. expired password) -------------------

# A prefix only "root" can write to, and a sudo that refuses -v a given number
# of times, then runs commands (making the prefix writable for them).
locked=$work/locked-prefix
mkdir -p "$work/sudobin" "$locked"
chmod 555 "$locked"
cat > "$work/sudobin/sudo" <<EOF
#!/bin/sh
if [ "\$1" = -v ]; then
    n=\$(cat "$work/sudo-v-count" 2> /dev/null || echo 0)
    echo \$((n + 1)) > "$work/sudo-v-count"
    [ "\$n" -ge "\$(cat "$work/sudo-v-fails")" ]
    exit \$?
fi
chmod -R u+w "$locked"
exec "\$@"
EOF
chmod +x "$work/sudobin/sudo"
# The installer with that sudo, on a pseudo-terminal fed with INPUT (or,
# without INPUT, with stdin from /dev/null: no terminal).
sudo_install() { # sudo_install [INPUT]
    local command="env -i PATH='$work/sudobin:/usr/local/bin:/usr/bin:/bin' HOME='$work/home' TMPDIR='$work' AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 LANG=C.UTF-8 bash '$installer' --prefix='$locked' --build-dir='$build_dir' -na"
    if (($#)); then
        out=$(printf '%s' "$1" | script -qec "$command" /dev/null 2>&1); rc=$?
    else
        out=$(bash -c "$command" < /dev/null 2>&1); rc=$?
    fi
}

echo 99 > "$work/sudo-v-fails"; rm -f "$work/sudo-v-count"
sudo_install
check "refused sudo without a terminal fails" test "$rc" -eq 1
check "the refusal mentions an expired password" grep -qF "wrong or expired password" <<< "$out"
check "without a terminal there is no question" test "$(grep -c 'y/N' <<< "$out")" -eq 0
check "the kept build and --no-build are suggested" grep -qF "run the installer again with --no-build" <<< "$out"
check "nothing installed after refused sudo" test ! -e "$locked/bin/aqtruncat"

rm -f "$work/sudo-v-count"
sudo_install $'n\n'
check "on a terminal the user is asked" grep -qF "Retry installing the build? [y/N]" <<< "$out"
check "answering N stops" test "$rc" -eq 1
check "answering N changes nothing" test ! -e "$locked/bin/aqtruncat"
check "N: sudo was tried once" test "$(cat "$work/sudo-v-count")" -eq 1

echo 1 > "$work/sudo-v-fails"; rm -f "$work/sudo-v-count"
sudo_install $'y\n'
check "answering y retries and installs" test "$rc" -eq 0
check "y: sudo was tried twice" test "$(cat "$work/sudo-v-count")" -eq 2
check "y: asked exactly once" test "$(grep -c 'Retry installing the build' <<< "$out")" -eq 1
check "y: the binary is installed" test -x "$locked/bin/aqtruncat"
check "y: nothing was rebuilt" test "$(grep -c 'Configuring and building' <<< "$out")" -eq 0
echo 0 > "$work/sudo-v-fails"
env -i PATH="$work/sudobin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" AQTRUNCAT_PLASMASHELL_NAME=fakeplasmashell AQTRUNCAT_PLASMA_TIMEOUT=3 LANG=C.UTF-8 \
    bash "$installer" --prefix="$locked" -U < /dev/null > /dev/null 2>&1
check "cleanup uninstall through sudo" test ! -e "$locked/bin/aqtruncat"

check "sudo was never used" test ! -e "$work/sudo-calls"

printf '%d checks, %d failures\n' "$checks" "$failures"
((failures == 0))
