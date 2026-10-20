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

# Run the installer with a clean, temporary user environment.
run() {
    env -i PATH="$work/fakebin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" \
        LANG=C.UTF-8 ${XDG_TEST_ENV:+$XDG_TEST_ENV} bash "$installer" "$@"
}
out=""
rc=0
capture() { out=$(run "$@" 2>&1); rc=$?; }

# --- help and usage errors -------------------------------------------------

capture --help
check "--help exits 0" test "$rc" -eq 0
for needle in "--local" "-A, --autostart=true" "-na, --autostart=false" "-U, --uninstall" \
    "ON by default" "Never removed" "Ubuntu 24.04" "sudo is used only" "Examples:" "--no-build" \
    "aqtruncat/install-build"; do
    check "--help mentions $needle" grep -qF -- "$needle" <<< "$out"
done
help_text=$out
capture -h
check "-h equals --help" test "$out" = "$help_text"

for bad in "--bogus" "-x" "--autostart=maybe" "-A -na" "--autostart=true --autostart=false" \
    "-na --autostart=true" "-U -A" "--uninstall -na" "--local --prefix=/opt/x" "--prefix=relative" \
    "--prefix=/" "-U --build-dir=/tmp" "-U --no-build"; do
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
    local command="env -i PATH='$work/sudobin:/usr/local/bin:/usr/bin:/bin' HOME='$work/home' TMPDIR='$work' LANG=C.UTF-8 bash '$installer' --prefix='$locked' --build-dir='$build_dir' -na"
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
env -i PATH="$work/sudobin:/usr/local/bin:/usr/bin:/bin" HOME="$work/home" TMPDIR="$work" LANG=C.UTF-8 \
    bash "$installer" --prefix="$locked" -U < /dev/null > /dev/null 2>&1
check "cleanup uninstall through sudo" test ! -e "$locked/bin/aqtruncat"

check "sudo was never used" test ! -e "$work/sudo-calls"

printf '%d checks, %d failures\n' "$checks" "$failures"
((failures == 0))
