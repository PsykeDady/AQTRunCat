#!/usr/bin/env bash
# Offscreen preview of the AQTRunCat Plasmoid (F4.2) with demonstration
# data: renders the popup and the runner at panel sizes into a PNG, in a
# light and in a dark colour scheme, without touching plasmashell, the
# user's panels or the user's data.
#
# Everything runs inside a private session bus (dbus-run-session) with
# temporary HOME/XDG directories: a build-tree aqtruncatd, demo Custom
# Metrics (one snapshot file, one command metric) and two fake official
# integrations (command metrics marked "claude-code" and "codex" that print
# fixed quota windows, one of them not computable). Nothing reads the real
# Claude Code or Codex data.
#
# Usage: tools/plasmoid-preview.sh [--runner cat|coffee|walkinggoose|partyparrot|engine] [--build DIR] [--out DIR]
#   --build defaults to <repo>/build, --out to the current directory.
# Writes plasmoid-light.png and plasmoid-dark.png. Needs qml6 (Qt
# Declarative), Kirigami, Plasma's QML modules and dbus-run-session.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=$repo_root/build
out=$PWD
runner="cat"

while [[ $# -gt 0 ]]; do
    case $1 in
        --runner) runner=${2:?--runner needs a value}; shift 2 ;;
        --build) build=${2:?--build needs a value}; shift 2 ;;
        --out) out=${2:?--out needs a value}; shift 2 ;;
        -h | --help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done
backend=$build/src/aqtruncatd
[[ -x $backend ]] || { echo "no backend at $backend (build first)" >&2; exit 1; }
for tool in qml6 dbus-run-session; do
    command -v "$tool" > /dev/null || { echo "$tool not found" >&2; exit 1; }
done
mkdir -p "$out"
out=$(cd "$out" && pwd)

# A private session bus WITHOUT service directories: nothing installed on
# this machine (portals, an installed AQTRunCat, …) can be activated in it,
# and no display: everything renders offscreen.
if [[ -z ${AQTRUNCAT_PREVIEW_INSIDE:-} ]]; then
    bus_config=$(mktemp "${TMPDIR:-/tmp}/aqtruncat-preview-bus.XXXXXX")
    trap 'rm -f -- "$bus_config"' EXIT
    cat > "$bus_config" <<'XML'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
XML
    status=0
    AQTRUNCAT_PREVIEW_INSIDE=1 dbus-run-session --config-file="$bus_config" -- \
        env -u WAYLAND_DISPLAY -u DISPLAY "$0" --runner "$runner" --build "$build" --out "$out" || status=$?
    exit "$status"
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/aqtruncat-plasmoid-preview.XXXXXX")
backend_pid=""
cleanup() {
    if [[ -n $backend_pid ]]; then
        kill -TERM "$backend_pid" 2> /dev/null || true
        wait "$backend_pid" 2> /dev/null || true
    fi
    rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

export HOME=$work/home XDG_CONFIG_HOME=$work/config XDG_DATA_HOME=$work/data
export XDG_CACHE_HOME=$work/cache XDG_STATE_HOME=$work/state
mkdir -p "$HOME" "$XDG_CONFIG_HOME/AQTRunCat" "$XDG_DATA_HOME/AQTRunCat/metrics"
metrics=$XDG_DATA_HOME/AQTRunCat/metrics
# No update checks of the host in previews (F5).
printf '[runner]\nid=%s\n[updates]\nintervalMinutes=0\n' "$runner" > "$XDG_CONFIG_HOME/AQTRunCat/AQTRunCat.ini"

# A command metric that prints a fixed snapshot (multi-value output).
demo_command() { # demo_command FILE ID TITLE INTEGRATION SNAPSHOT
    local script=$metrics/$2.sh
    printf '#!/bin/sh\ncat <<'\''EOF'\''\n%s\nEOF\n' "$5" > "$script"
    chmod +x "$script"
    local integration=""
    [[ -n $4 ]] && integration=$(printf '"integration": "%s",' "$4")
    cat > "$1" <<EOF
{"id": "$2", "title": "$3", $integration
 "source": {"type": "command", "executable": "$script", "output": "snapshot", "intervalSeconds": 60}}
EOF
}
demo_command "$metrics/demo-claude.json" demo-claude "Claude Code" claude-code '{"title": "Claude Code", "lastUpdatedDate": "2026-10-21T08:45:00Z", "metrics": [
 {"id": "five_hour", "title": "Current session", "formattedValue": "26% used", "normalizedValue": 0.26, "detail": "Resets Oct 23, 20:00 · as of 08:45"},
 {"id": "seven_day", "title": "Current week (all models)", "formattedValue": "61% used", "normalizedValue": 0.61, "detail": "Resets Oct 27, 09:00 · as of 08:45"}]}'
demo_command "$metrics/demo-codex.json" demo-codex "Codex" codex '{"title": "Codex", "lastUpdatedDate": "2026-10-21T08:45:00Z", "metrics": [
 {"id": "primary", "title": "5h limit", "formattedValue": "12% used", "normalizedValue": 0.12, "detail": "Resets 14:10"},
 {"id": "secondary", "title": "Weekly limit", "formattedValue": "—", "detail": "Cannot be calculated reliably: no recent session", "warning": true}]}'
demo_command "$metrics/demo-builds.json" demo-builds "CI builds" "" '{"title": "CI builds", "lastUpdatedDate": "2026-10-21T08:45:00Z", "metrics": [
 {"title": "Passing", "formattedValue": "18 / 20", "normalizedValue": 0.9}]}'
cat > "$metrics/demo-weather.json" <<'EOF'
{"title": "Outside", "metrics": [{"title": "Temperature", "formattedValue": "14 °C"}],
 "lastUpdatedDate": "2026-10-21T08:00:00Z"}
EOF

"$backend" > "$work/backend.log" 2>&1 &
backend_pid=$!

render() { # render NAME [COLOR_SCHEME_FILE]
    rm -f -- "$XDG_CONFIG_HOME/kdeglobals"
    [[ -z ${2:-} ]] || cp -- "$2" "$XDG_CONFIG_HOME/kdeglobals"
    rm -f -- "$out/plasmoid-$1.png"
    QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde QT_FORCE_STDERR_LOGGING=1 \
        qml6 "$repo_root/tools/plasmoid-preview.qml" -- "$out/plasmoid-$1.png" 2>&1 |
        grep -v -E 'Failed to register with host portal|kf.windowsystem|wl_display|installEventFilter' || true
    if [[ -f $out/plasmoid-$1.png ]]; then
        echo "wrote $out/plasmoid-$1.png"
    else
        echo "the $1 preview failed (messages above)" >&2
        return 1
    fi
}
render light
dark=""
for scheme in /usr/share/color-schemes/BreezeDark.colors /usr/local/share/color-schemes/BreezeDark.colors; do
    [[ -f $scheme ]] && { dark=$scheme; break; }
done
if [[ -n $dark ]]; then
    render dark "$dark"
else
    echo "BreezeDark colour scheme not found: no dark preview" >&2
fi
