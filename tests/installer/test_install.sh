#!/usr/bin/env bash
# Tests for install.sh: command line, distribution detection, XDG paths and a
# real install/reinstall/uninstall cycle into temporary directories (never
# /usr, never the real home). Usage: test_install.sh SOURCE_DIR BUILD_DIR

set -uo pipefail

source_dir=$(cd -- "$1" && pwd)
build_dir=$(cd -- "$2" && pwd)
installer=$source_dir/install.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/aqtruncat-installer-test.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

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
    "ON by default" "Never removed" "Ubuntu 24.04" "sudo is used only" "Examples:"; do
    check "--help mentions $needle" grep -qF -- "$needle" <<< "$out"
done
help_text=$out
capture -h
check "-h equals --help" test "$out" = "$help_text"

for bad in "--bogus" "-x" "--autostart=maybe" "-A -na" "--autostart=true --autostart=false" \
    "-na --autostart=true" "-U -A" "--uninstall -na" "--local --prefix=/opt/x" "--prefix=relative" \
    "--prefix=/" "-U --build-dir=/tmp"; do
    # shellcheck disable=SC2086 # split the option list on purpose
    capture $bad
    check "\"$bad\" is a usage error (2)" test "$rc" -eq 2
    check "\"$bad\" explains the error" grep -qF 'Run "./install.sh --help"' <<< "$out"
done
capture --bogus
check "unknown option is named" grep -qF 'unknown option "--bogus"' <<< "$out"

# --- option semantics (dry run: nothing is changed) --------------------------

plan() { capture --dry-run "$@"; }
plan;                     check "default: system install" grep -qF "Action:     install (system)" <<< "$out"
                          check "default: autostart on" grep -qF "Autostart:  true" <<< "$out"
plan -A;                  check "-A enables autostart" grep -qF "Autostart:  true" <<< "$out"
plan --autostart=true;    check "--autostart=true enables autostart" grep -qF "Autostart:  true" <<< "$out"
plan -na;                 check "-na disables autostart" grep -qF "Autostart:  false" <<< "$out"
plan --autostart=false;   check "--autostart=false disables autostart" grep -qF "Autostart:  false" <<< "$out"
plan -A -A;               check "repeating the same option is fine" test "$rc" -eq 0
plan --local;             check "--local installs locally" grep -qF "Action:     install (local)" <<< "$out"
                          check "--local needs no privileges" grep -qF "Privileges: none" <<< "$out"
plan -U;                  check "-U uninstalls system-wide" grep -qF "Action:     uninstall (system)" <<< "$out"
plan --uninstall;         check "--uninstall equals -U" grep -qF "Action:     uninstall (system)" <<< "$out"
plan --local -U;          check "--local -U uninstalls locally" grep -qF "Action:     uninstall (local)" <<< "$out"
plan -na --local;         check "option order does not matter" grep -qF "Autostart:  false" <<< "$out"
plan --prefix=/opt/aqt/;  check "--prefix sets the binary path" grep -qF "Binary:     /opt/aqt/bin/aqtruncat" <<< "$out"

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
check "pretty name is reported" grep -qF "Distribution: Gentoo Linux (family: unknown)" <<< "$out"
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
check "XDG_BIN_HOME is honoured" grep -qF "Binary:     $work/x/bin/aqtruncat" <<< "$out"
check "XDG_DATA_HOME is honoured" grep -qF "Data:       $work/x/data" <<< "$out"
check "XDG_STATE_HOME holds the manifest" grep -qF "Manifest:   $work/x/state/aqtruncat/install-manifest.txt" <<< "$out"
XDG_TEST_ENV="XDG_DATA_HOME=relative/data" capture --dry-run --local
check "relative XDG values are ignored" grep -qF "Data:       $work/home/.local/share" <<< "$out"

# --- real local install / reinstall / uninstall ----------------------------

home=$work/home
mkdir -p "$home/.config/AQTRunCat" "$home/.local/share/AQTRunCat/runners/mine" "$home/.local/share/AQTRunCat/metrics"
printf '[runner]\nid=coffee\n' > "$home/.config/AQTRunCat/AQTRunCat.ini"
echo frame > "$home/.local/share/AQTRunCat/runners/mine/frame-0.png"
echo '{}' > "$home/.local/share/AQTRunCat/metrics/producer.json"
entry=$home/.config/autostart/aqtruncat.desktop
bin=$home/.local/bin/aqtruncat
manifest=$home/.local/state/aqtruncat/install-manifest.txt

capture --local --build-dir="$build_dir"
check "local install succeeds" test "$rc" -eq 0
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

check "sudo was never used" test ! -e "$work/sudo-calls"

printf '%d checks, %d failures\n' "$checks" "$failures"
((failures == 0))
