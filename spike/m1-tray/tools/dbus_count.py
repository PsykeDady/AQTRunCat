#!/usr/bin/env python3
"""Count SNI D-Bus traffic for one spike run: dbus_count.py FPS RUNNER SECONDS [extra spike args...]"""
import collections, json, os, pathlib, subprocess, sys, time

# Spike binary: build with -DAQTRUNCAT_BUILD_SPIKES=ON, or point AQTRUNCAT_SPIKE_BIN at it.
BIN = os.environ.get("AQTRUNCAT_SPIKE_BIN",
                     str(pathlib.Path(__file__).resolve().parents[3] / "build/spike/m1-tray/aqtruncat-tray-spike"))
fps, runner, secs = sys.argv[1], sys.argv[2], int(sys.argv[3])
env = dict(os.environ, LC_ALL="C")

def sh(*a):
    return subprocess.run(a, capture_output=True, text=True, env=env).stdout

spike = subprocess.Popen([BIN, "--fps", fps, "--runner", runner, "--duration", str(secs + 8), "--stats", "0"] + sys.argv[4:],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
time.sleep(3)
names = {}
for line in sh("busctl", "--user", "list", "--no-legend").splitlines():
    f = line.split()
    if f and f[0].startswith(":") and len(f) > 2 and f[1].isdigit():
        names[f[0]] = (int(f[1]), f[2])
mine = [u for u, (pid, _) in names.items() if pid == spike.pid]
sni = next(u for u in mine if sh("busctl", "--user", "get-property", u, "/StatusNotifierItem",
                                 "org.kde.StatusNotifierItem", "Id").strip())
shell = next(u for u, (_, comm) in names.items() if comm == "plasmashell")

mon = subprocess.run(["timeout", str(secs), "dbus-monitor", "--session", "--profile",
                      f"sender='{sni}'", f"destination='{sni}'"], capture_output=True, text=True, env=env).stdout
signals, calls = collections.Counter(), collections.Counter()
for line in mon.splitlines():
    f = line.split("\t")
    if len(f) < 8:
        continue
    if f[0] == "sig" and f[3] == sni:
        signals[f[7]] += 1
    elif f[0] == "mc" and f[4] == sni:
        calls[(("plasmashell" if f[3] == shell else f[3]), f[7])] += 1

reply = json.loads(sh("busctl", "--user", "--json=short", "call", sni, "/StatusNotifierItem",
                      "org.freedesktop.DBus.Properties", "GetAll", "s", "org.kde.StatusNotifierItem"))["data"][0]
pix = reply["IconPixmap"]["data"]
tt = reply["ToolTip"]["data"]
raw = sum(len(d) for _, _, d in pix) + sum(len(d) for _, _, d in tt[1])
spike.wait()
print(f"fps={fps:>3} runner={runner:<6} sni={sni} " +
      " ".join(f"{k}/s={v/secs:.1f}" for k, v in sorted(signals.items())) + " | calls/s: " +
      " ".join(f"{who}:{m}={v/secs:.1f}" for (who, m), v in sorted(calls.items())) +
      f" | IconPixmap={[(w, h) for w, h, _ in pix]} ToolTipIcon={[(w, h) for w, h, _ in tt[1]]} "
      f"pixel_bytes_per_GetAll={raw}")
