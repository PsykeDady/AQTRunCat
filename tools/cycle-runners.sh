#!/usr/bin/env bash
# Visual check of the available runners: shows every valid runner (bundled and
# user, as listed by `aqtruncat runner list`) in the tray at a fixed frame
# rate, one after the other, using `aqtruncat run --runner ID --fps N`.
# User runners come from $XDG_DATA_HOME/AQTRunCat/runners, so a test set can
# be used with: XDG_DATA_HOME=/some/dir tools/cycle-runners.sh
#
# Usage: tools/cycle-runners.sh [--fps N] [--duration SECONDS] [--binary PATH] [ID...]
#   Without IDs, all valid runners are shown. --fps defaults to 20,
#   --duration to 5 s per runner. The binary defaults to $AQTRUNCAT_BIN,
#   then <repo>/build/src/aqtruncat.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=${AQTRUNCAT_BIN:-$repo_root/build/src/aqtruncat}
fps=20
duration=5
runners=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --fps) fps=${2:?--fps needs a value}; shift 2 ;;
        --duration) duration=${2:?--duration needs a value}; shift 2 ;;
        --binary) binary=${2:?--binary needs a value}; shift 2 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
        *) runners+=("$1"); shift ;;
    esac
done

if ! [[ $fps =~ ^[1-9][0-9]*$ && $duration =~ ^[1-9][0-9]*$ ]]; then
    echo "--fps and --duration must be positive integers" >&2
    exit 2
fi
if [[ ! -x $binary ]]; then
    echo "AQTRunCat binary not found or not executable: $binary" >&2
    echo "Build it first (cmake -S . -B build && cmake --build build) or pass --binary PATH." >&2
    exit 1
fi

listing=$("$binary" runner list)
if [[ ${#runners[@]} -eq 0 ]]; then
    # Table rows start with the id; skip the header, warnings (indented),
    # invalid runners and the footer (which starts with "*").
    mapfile -t runners < <(printf '%s\n' "$listing" | awk 'NR > 1 && /^[^ *]/ && !/INVALID:/ {print $1}')
fi
if [[ ${#runners[@]} -eq 0 ]]; then
    echo "no valid runners found" >&2
    exit 1
fi
printf '%s\n\n' "$listing"

# Only ever stop the instance this script started.
app_pid=""
stop_app() {
    if [[ -n $app_pid ]] && kill -0 "$app_pid" 2>/dev/null; then
        kill -TERM "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    app_pid=""
}
trap 'stop_app' EXIT
trap 'echo; echo "interrupted"; exit 130' INT TERM

step=0
for runner in "${runners[@]}"; do
    step=$((step + 1))
    printf '[%d/%d] %s @ %d FPS for %ds\n' "$step" "${#runners[@]}" "$runner" "$fps" "$duration"
    "$binary" run --runner "$runner" --fps "$fps" &
    app_pid=$!
    # A terminal Ctrl+C also reaches sleep and AQTRunCat: a killed sleep means
    # the user interrupted, not that AQTRunCat crashed.
    sleep_status=0
    sleep "$duration" & wait $! || sleep_status=$?
    if (( sleep_status > 128 )); then
        echo; echo "interrupted"
        exit 130
    fi
    if ! kill -0 "$app_pid" 2>/dev/null; then
        wait "$app_pid" && status=0 || status=$?
        echo "AQTRunCat exited (status $status) while showing \"$runner\"" >&2
        app_pid=""
        exit 1
    fi
    stop_app
done

echo "done"
