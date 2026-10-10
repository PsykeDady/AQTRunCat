#!/usr/bin/env bash
# End-to-end check of the CPU-driven runner with real load:
#   idle → half load → full load → recovery
# Load is generated with `yes > /dev/null` (one process per thread, each bounded
# by `timeout`). AQTRunCat runs with --verbose; at the end a per-phase summary
# of raw CPU, smoothed CPU and applied FPS is printed. If busctl and
# dbus-monitor are available, the NewIcon signals of the tray item are counted
# too, as an independent measure of the real animation speed.
#
# Usage: tools/test-cpu-runtime.sh [--binary PATH] [--out DIR]
#   The binary defaults to $AQTRUNCAT_BIN, then <repo>/build/src/aqtruncat.
#   Logs go to DIR (default: a new temporary directory).

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=${AQTRUNCAT_BIN:-$repo_root/build/src/aqtruncat}
out=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --binary) binary=${2:?--binary needs a value}; shift 2 ;;
        --out) out=${2:?--out needs a value}; shift 2 ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done
if [[ ! -x $binary ]]; then
    echo "AQTRunCat binary not found or not executable: $binary" >&2
    exit 1
fi
out=${out:-$(mktemp -d -t aqtruncat-cpu-runtime.XXXXXX)}
mkdir -p "$out"

threads=$(nproc)
half=$(( threads / 2 > 0 ? threads / 2 : 1 ))
idle_s=30 half_s=30 full_s=40 recovery_s=45
total_s=$(( idle_s + half_s + full_s + recovery_s ))

app_pid="" monitor_pid=""
load_pids=()
cleanup() {
    for pid in "${load_pids[@]}" $monitor_pid $app_pid; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT
trap 'echo "interrupted" >&2; exit 130' INT TERM

load() { # load <processes> <seconds>
    for _ in $(seq "$1"); do
        timeout "$2" yes > /dev/null &
        load_pids+=($!)
    done
}
mark() { echo "$(date +%T.%3N) $1" >> "$out/phases.txt"; echo "$(date +%T) $1"; }

QT_FORCE_STDERR_LOGGING=1 QT_MESSAGE_PATTERN='%{time hh:mm:ss.zzz} %{message}' \
    "$binary" run --verbose > "$out/app.log" 2>&1 &
app_pid=$!
sleep 2
kill -0 "$app_pid" 2>/dev/null || { echo "AQTRunCat exited at startup, see $out/app.log" >&2; exit 1; }

if command -v busctl > /dev/null && command -v dbus-monitor > /dev/null; then
    # With the Metrics Bar enabled the process owns several tray items: pick
    # the runner's (tooltip "AQTRunCat — <runner>"), else the first one found.
    sni=""
    for name in $(busctl --user list --no-legend | awk -v p="$app_pid" '$2 == p && $1 ~ /^:/ {print $1}'); do
        busctl --user get-property "$name" /StatusNotifierItem org.kde.StatusNotifierItem Id > /dev/null 2>&1 || continue
        if busctl --user get-property "$name" /StatusNotifierItem org.kde.StatusNotifierItem ToolTip 2>/dev/null \
                | grep -q 'AQTRunCat —'; then
            sni=$name
            break
        fi
        [[ -n $sni ]] || sni=$name
    done
    if [[ -n $sni ]]; then
        timeout $(( total_s + 2 )) dbus-monitor --session --profile \
            "type='signal',sender='$sni',member='NewIcon'" > "$out/newicon.tsv" 2>/dev/null &
        monitor_pid=$!
    fi
fi

: > "$out/phases.txt"
mark "idle";                                       sleep "$idle_s"
mark "half";     load "$half" "$half_s";           sleep "$half_s"
mark "full";     load "$threads" "$full_s";        sleep "$full_s"
mark "recovery";                                   sleep "$recovery_s"
mark "end"

kill -TERM "$app_pid"
wait "$app_pid" && app_status=0 || app_status=$?
app_pid=""
leftover=0
for pid in "${load_pids[@]}"; do kill -0 "$pid" 2>/dev/null && leftover=$((leftover + 1)); done

echo
echo "Per-phase summary (first 6 s after each transition excluded):"
awk '
    function secs(t,   p) { split(t, p, ":"); return p[1] * 3600 + p[2] * 60 + p[3] }
    FNR == NR { start[FNR] = secs($1); name[FNR] = $2; n = FNR; next }
    match($0, /raw= *[0-9.]+/) {
        t = secs($1)
        for (i = 1; i < n; i++) if (t >= start[i] + 6 && t < start[i + 1]) break
        if (i == n) next
        split(substr($0, RSTART), r, "="); raw = r[2] + 0
        match($0, /applied= *[0-9]+/); split(substr($0, RSTART, RLENGTH), a, "="); fps = a[2] + 0
        sum[i] += raw; cnt[i]++
        if (!(i in lo) || fps < lo[i]) lo[i] = fps
        if (!(i in hi) || fps > hi[i]) hi[i] = fps
        if ((i in last) && last[i] != fps) changes[i]++
        last[i] = fps
    }
    END {
        for (i = 1; i < n; i++) {
            printf "  %-9s raw CPU %5.1f%%  applied FPS %2d-%-2d  changes %d\n",
                   name[i], cnt[i] ? sum[i] / cnt[i] : 0, lo[i], hi[i], changes[i] + 0
        }
    }' "$out/phases.txt" "$out/app.log"
if [[ -s $out/newicon.tsv ]]; then
    echo "  NewIcon signals: $(grep -c '^sig' "$out/newicon.tsv") in ${total_s}s (raw data in $out/newicon.tsv)"
fi
echo "AQTRunCat exit status: $app_status, leftover load processes: $leftover"
echo "Logs: $out"
