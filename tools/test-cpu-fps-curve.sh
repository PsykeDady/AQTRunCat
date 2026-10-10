#!/usr/bin/env bash
# Visual check of the AQTRunCat CPU → FPS curve, without generating real load.
#
# Walks the simulated CPU levels 0 → 100 → 0 % in steps of 10, computes
#   fps = round(10 + 30 * (cpu / 100)^2)
# and runs the real application with the diagnostic `run --fps N` mode for a few
# seconds per level, so the runner can be watched speeding up and slowing
# down in the tray. It simulates the curve only, not the CPU smoothing.
#
# Usage: tools/test-cpu-fps-curve.sh [--duration SECONDS] [--binary PATH]
#   The binary defaults to $AQTRUNCAT_BIN, then <repo>/build/src/aqtruncat.

set -euo pipefail

usage() {
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=${AQTRUNCAT_BIN:-$repo_root/build/src/aqtruncat}
duration=5

while [[ $# -gt 0 ]]; do
    case $1 in
        --duration) duration=${2:?--duration needs a value}; shift 2 ;;
        --binary) binary=${2:?--binary needs a value}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done

if ! [[ $duration =~ ^[1-9][0-9]*$ ]]; then
    echo "--duration must be a positive integer number of seconds" >&2
    exit 2
fi
if [[ ! -x $binary ]]; then
    echo "AQTRunCat binary not found or not executable: $binary" >&2
    echo "Build it first (cmake -S . -B build && cmake --build build) or pass --binary PATH." >&2
    exit 1
fi

# round(10 + 30 * (cpu/100)^2) in integer arithmetic: (100000 + 30*cpu^2 + 5000) / 10000
fps_for() {
    local cpu=$1
    echo $(( (100000 + 30 * cpu * cpu + 5000) / 10000 ))
}

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

levels=(0 10 20 30 40 50 60 70 80 90 100 90 80 70 60 50 40 30 20 10 0)

echo "AQTRunCat CPU → FPS curve: fps = round(10 + 30 * (cpu / 100)^2)"
for cpu in 0 10 20 30 40 50 60 70 80 90 100; do
    printf 'CPU %3d%% -> %2d FPS\n' "$cpu" "$(fps_for "$cpu")"
done
echo
echo "Running ${#levels[@]} steps of ${duration}s (0 → 100 → 0 %) with $binary"

step=0
for cpu in "${levels[@]}"; do
    step=$((step + 1))
    fps=$(fps_for "$cpu")
    printf '[%2d/%d] CPU %3d%% -> %2d FPS\n' "$step" "${#levels[@]}" "$cpu" "$fps"
    "$binary" run --fps "$fps" &
    app_pid=$!
    # `sleep & wait` keeps Ctrl+C responsive (traps run as soon as wait returns).
    # A terminal Ctrl+C also reaches sleep and AQTRunCat, which then quit on
    # their own: treat a killed sleep as an interruption, not as a crash.
    sleep_status=0
    sleep "$duration" & wait $! || sleep_status=$?
    if (( sleep_status > 128 )); then
        echo; echo "interrupted"
        exit 130
    fi
    if ! kill -0 "$app_pid" 2>/dev/null; then
        wait "$app_pid" && status=0 || status=$?
        echo "AQTRunCat exited unexpectedly (status $status) at $fps FPS" >&2
        app_pid=""
        exit 1
    fi
    stop_app
done

echo "done"
