#!/usr/bin/env python3
"""Minimal third-party client of the AQTRunCat backend (API 1), without Qt.

It uses only PyGObject (Gio), which most Linux desktops already ship; it is an
example, not a dependency of AQTRunCat. It prints the current metrics, then
follows MetricsChanged deltas, and takes the whole state again when it misses
a generation or when the backend restarts.

    python3 aqtruncat-client.py            # follow updates (Ctrl+C to stop)
    python3 aqtruncat-client.py --once     # print once and exit
"""

import json
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

NAME = "io.github.psykedady.AQTRunCat"
PATH = "/io/github/psykedady/AQTRunCat"
IFACE = "io.github.psykedady.AQTRunCat1"


class Client:
    def __init__(self, bus):
        self.bus = bus
        self.generation = 0
        self.metrics = {}  # id -> metric object
        self.order = []
        self.owner = None  # unique bus name of the backend in use

    def call(self, method, args=None):
        # Autostart is allowed: D-Bus activation starts aqtruncatd if needed.
        reply = self.bus.call_sync(NAME, PATH, IFACE, method, args, GLib.VariantType("(s)"),
                                   Gio.DBusCallFlags.NONE, 15000, None)
        return json.loads(reply.unpack()[0])

    def load_state(self):
        state = self.call("GetState")
        if state["api"] != 1:
            raise SystemExit("unsupported AQTRunCat API %s" % state["api"])
        self.generation = state["generation"]
        self.metrics = {m["id"]: m for m in state["metrics"]}
        self.order = [m["id"] for m in state["metrics"]]
        self.show()

    def show(self):
        for metric_id in self.order:
            m = self.metrics[metric_id]
            value = m["formattedValue"] if m["state"] == "ok" else m["state"]
            print("%-12s %-28s %s" % (m["category"], m["title"][:28], value))
            for item in m["items"]:
                print("%-12s   %-26s %s" % ("", item["title"][:26], item["formattedValue"]))
        print(flush=True)

    def on_metrics_changed(self, _bus, _sender, _path, _iface, _signal, params):
        generation, text = params.unpack()
        if generation <= self.generation:
            return
        if generation != self.generation + 1:
            self.load_state()  # missed an update: never guess
            return
        delta = json.loads(text)
        for m in delta["metrics"]:
            self.metrics[m["id"]] = m
        for metric_id in delta["removed"]:
            self.metrics.pop(metric_id, None)
        self.order = delta["order"]
        self.generation = generation
        self.show()


def main():
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    client = Client(bus)
    try:
        client.load_state()
    except GLib.Error as error:
        print("AQTRunCat backend unavailable: %s" % error.message, file=sys.stderr)
        return 3
    if "--once" in sys.argv[1:]:
        return 0
    bus.signal_subscribe(NAME, IFACE, "MetricsChanged", PATH, None,
                         Gio.DBusSignalFlags.NONE, client.on_metrics_changed)
    # A new backend (restart, upgrade): take its whole state.
    def appeared(_bus, _name, owner):
        if client.owner is not None and owner != client.owner:
            client.load_state()
        client.owner = owner

    Gio.bus_watch_name_on_connection(bus, NAME, Gio.BusNameWatcherFlags.NONE, appeared, None)
    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
