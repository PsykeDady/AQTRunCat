#!/usr/bin/env bash
# Repeatable runtime check of "aqtruncat metrics": prints one metrics line per
# second while going through
#   baseline → CPU load → [network download] → disk write → recovery
# with a marker line at the start of each phase, so each phase can be seen in
# the CPU, Net and Disk columns.
#
# - CPU load: one `yes > /dev/null` per thread, each bounded by `timeout`.
# - Network: only with --download-url URL (nothing is downloaded otherwise);
#   fetched with curl into /dev/null.
# - Disk: writes a temporary file (default 256 MiB, flushed with fsync) in
#   --disk-dir (default ${XDG_CACHE_HOME:-~/.cache}), then deletes it. Use a
#   disk-backed directory: on tmpfs the writes never reach a disk.
#
# Usage: tools/test-metrics-runtime.sh [--download-url URL] [--disk-dir DIR] [--disk-mib N] [--binary PATH]
#   The binary defaults to $AQTRUNCAT_BIN, then <repo>/build/src/aqtruncat.

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
binary=${AQTRUNCAT_BIN:-$repo_root/build/src/aqtruncat}
download_url=""
disk_dir=${XDG_CACHE_HOME:-$HOME/.cache}
disk_mib=256

while [[ $# -gt 0 ]]; do
    case $1 in
        --download-url) download_url=${2:?--download-url needs a value}; shift 2 ;;
        --disk-dir) disk_dir=${2:?--disk-dir needs a value}; shift 2 ;;
        --disk-mib) disk_mib=${2:?--disk-mib needs a value}; shift 2 ;;
        --binary) binary=${2:?--binary needs a value}; shift 2 ;;
        -h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
    esac
done
if [[ ! -x $binary ]]; then
    echo "AQTRunCat binary not found or not executable: $binary" >&2
    exit 1
fi
if ! [[ $disk_mib =~ ^[1-9][0-9]*$ ]]; then
    echo "--disk-mib must be a positive integer" >&2
    exit 2
fi
if [[ -n $download_url ]] && ! command -v curl > /dev/null; then
    echo "--download-url needs curl" >&2
    exit 1
fi
mkdir -p "$disk_dir"
disk_file=$(mktemp "$disk_dir/.aqtruncat-metrics-test.XXXXXX")

watch_pid=""
helper_pids=()
cleanup() {
    for pid in "${helper_pids[@]}" $watch_pid; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -f "$disk_file"
}
trap cleanup EXIT
trap 'echo "interrupted" >&2; exit 130' INT TERM

mark() { echo "=== $(date +%T) $*"; }

"$binary" metrics --watch &
watch_pid=$!

mark "baseline (5 s; rates appear after the first second)"
sleep 6

threads=$(nproc)
mark "CPU load: $threads x yes for 6 s"
for _ in $(seq "$threads"); do
    timeout 6 yes > /dev/null &
    helper_pids+=($!)
done
sleep 6
mark "CPU recovery (3 s)"
sleep 3

if [[ -n $download_url ]]; then
    mark "network: downloading $download_url"
    curl -s -o /dev/null --max-time 60 "$download_url" &
    helper_pids+=($!)
    wait $! || echo "(download failed)"
    sleep 3
else
    mark "network phase skipped (no --download-url)"
fi

mark "disk: writing $disk_mib MiB to $disk_dir"
dd if=/dev/zero of="$disk_file" bs=1M count="$disk_mib" conv=fsync status=none &
helper_pids+=($!)
wait $! || echo "(disk write failed)"
rm -f "$disk_file"
sleep 2

mark "recovery (5 s)"
sleep 5
mark "done"
