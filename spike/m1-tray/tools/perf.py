#!/usr/bin/env python3
"""Per-process CPU/RSS/wakeup benchmark of the tray spike, read straight from /proc.

perf.py WARMUP MEASURE COND...   where COND is 'base' or '<runner>:<fps>[:generic]'
"""
import os, pathlib, subprocess, sys, time

# Spike binary: build with -DAQTRUNCAT_BUILD_SPIKES=ON, or point AQTRUNCAT_SPIKE_BIN at it.
BIN = os.environ.get("AQTRUNCAT_SPIKE_BIN",
                     str(pathlib.Path(__file__).resolve().parents[3] / "build/spike/m1-tray/aqtruncat-tray-spike"))
TCK = os.sysconf("SC_CLK_TCK")

def pid_of(name, pick=lambda p: True):
    for p in os.listdir("/proc"):
        if p.isdigit():
            try:
                if open(f"/proc/{p}/comm").read().strip() == name and pick(p):
                    return int(p)
            except OSError:
                pass

def session_bus_pid():
    return pid_of("dbus-broker", lambda p: "--controller 10" in open(f"/proc/{p}/cmdline").read().replace("\0", " "))

def sample(pid):
    stat = open(f"/proc/{pid}/stat").read().rsplit(")", 1)[1].split()
    ticks = int(stat[11]) + int(stat[12])  # utime + stime (all threads)
    status = dict(l.split(":", 1) for l in open(f"/proc/{pid}/status") if ":" in l)
    rss = int(status["VmRSS"].split()[0])
    # context switches summed over threads ~ wakeups
    cs = 0
    for t in os.listdir(f"/proc/{pid}/task"):
        try:
            st = dict(l.split(":", 1) for l in open(f"/proc/{pid}/task/{t}/status") if ":" in l)
            cs += int(st["voluntary_ctxt_switches"]) + int(st["nonvoluntary_ctxt_switches"])
        except OSError:
            pass
    return ticks, rss, cs

warmup, measure = int(sys.argv[1]), int(sys.argv[2])
watched = {"plasmashell": pid_of("plasmashell"), "kwin_wayland": pid_of("kwin_wayland"), "bus": session_bus_pid()}
print(f"# warmup={warmup}s measure={measure}s pids={watched}", flush=True)
print("cond\tspike_cpu%\tspike_rss_kB\tspike_cs/s\tplasmashell_cpu%\tplasmashell_rss_kB(start->end)\tplasmashell_cs/s\tkwin_cpu%\tbus_cpu%", flush=True)

for cond in sys.argv[3:]:
    procs = dict(watched)
    spike = None
    if cond != "base":
        runner, fps, *rest = cond.split(":")
        env = dict(os.environ)
        if rest and rest[0] == "generic":
            env["QT_QPA_PLATFORMTHEME"] = "generic"
        spike = subprocess.Popen([BIN, "--runner", runner, "--fps", fps, "--stats", "0"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        procs["spike"] = spike.pid
    time.sleep(warmup)
    t0 = time.monotonic()
    a = {k: sample(p) for k, p in procs.items()}
    time.sleep(measure)
    b = {k: sample(p) for k, p in procs.items()}
    dt = time.monotonic() - t0
    if spike:
        spike.terminate()
        spike.wait()
    cpu = {k: 100.0 * (b[k][0] - a[k][0]) / TCK / dt for k in procs}
    cs = {k: (b[k][2] - a[k][2]) / dt for k in procs}
    print("\t".join([
        cond,
        f"{cpu['spike']:.2f}" if spike else "-",
        str(b["spike"][1]) if spike else "-",
        f"{cs['spike']:.0f}" if spike else "-",
        f"{cpu['plasmashell']:.2f}",
        f"{a['plasmashell'][1]}->{b['plasmashell'][1]}",
        f"{cs['plasmashell']:.0f}",
        f"{cpu['kwin_wayland']:.2f}",
        f"{cpu['bus']:.2f}",
    ]), flush=True)
    time.sleep(3)
