#!/usr/bin/env python3
"""Stand where the rotator and radio daemons stand, so Gpredict can be watched.

Gpredict speaks only as a client: it connects to a rotator daemon and a radio
daemon and sends bearings and a Doppler-corrected frequency. It has no interface
that accepts orders, so nothing here tells Gpredict what to track. This listens
where those daemons listen instead:

    Gpredict --az/el--> this --az/el--> rotctld --> motors
             --freq---> this --freq---> rigctld --> radio

Every decision stays Gpredict's. This sees each one, applies the selected
profile's rules, and forwards it or refuses it.

The profile is chosen by hand because the protocol carries angles and a
frequency and never says which spacecraft they are for. Working it out from the
frequency breaks the day two share a band.

    tools/ground/gpredict-proxy.py --profile "LUR-1" \
        --rotator-upstream 127.0.0.1:4533 --radio-upstream 127.0.0.1:4532

With no upstream the command is accepted, checked and logged but driven nowhere,
which is how to watch Gpredict without a rotator attached.
"""

import argparse
import json
import pathlib
import selectors
import socket
import sys
import threading
import time

ROTATOR_PORT = 4533
RADIO_PORT = 4532
# hamlib's own: zero is success, and this is what Gpredict reads.
RPRT_OK = "RPRT 0\n"
RPRT_REFUSED = "RPRT -1\n"


class Profile:
    def __init__(self, entry):
        self.name = entry["name"]
        self.catalogue = int(entry["catalogue"])
        self.enabled = bool(entry.get("enabled", True))
        self.azimuth = (float(entry["azimuth_min_deg"]), float(entry["azimuth_max_deg"]))
        self.elevation = (float(entry["elevation_min_deg"]), float(entry["elevation_max_deg"]))
        self.policy = entry.get("frequency_policy", "follow")
        self.frequency_hz = int(entry.get("frequency_hz", 0))
        if self.policy not in ("follow", "hold", "ignore"):
            raise ValueError(f"{self.name}: unknown frequency policy {self.policy}")
        if self.policy == "hold" and self.frequency_hz <= 0:
            raise ValueError(f"{self.name}: hold needs a frequency to hold")

    def allows(self, azimuth, elevation):
        return (
            self.azimuth[0] <= azimuth <= self.azimuth[1]
            and self.elevation[0] <= elevation <= self.elevation[1]
        )


class Counters:
    def __init__(self):
        self.lock = threading.Lock()
        self.bearings_in = 0
        self.frequencies_in = 0
        self.bearings_out = 0
        self.frequencies_out = 0
        self.refused = 0
        self.upstream_failures = 0

    def add(self, field):
        with self.lock:
            setattr(self, field, getattr(self, field) + 1)

    def line(self):
        with self.lock:
            return (
                f"in az/el {self.bearings_in} freq {self.frequencies_in}; "
                f"out az/el {self.bearings_out} freq {self.frequencies_out}; "
                f"refused {self.refused}; upstream failures {self.upstream_failures}"
            )


class Upstream:
    """The real daemon, if there is one. Absent is a supported configuration."""

    def __init__(self, endpoint, counters):
        self.endpoint = endpoint
        self.counters = counters
        self.socket = None

    def connect(self):
        if self.endpoint is None or self.socket is not None:
            return self.socket is not None
        host, _, port = self.endpoint.rpartition(":")
        try:
            self.socket = socket.create_connection((host, int(port)), timeout=5)
        except OSError as error:
            self.counters.add("upstream_failures")
            print(f"upstream {self.endpoint}: {error}", file=sys.stderr, flush=True)
            self.socket = None
        return self.socket is not None

    def send(self, text):
        """Return the daemon's reply, or None when there is no daemon."""
        if not self.connect():
            return None
        try:
            self.socket.sendall(text.encode())
            return self.socket.recv(256).decode(errors="replace")
        except OSError as error:
            # A daemon that went away is reconnected on the next command rather
            # than bringing the proxy down in the middle of a pass.
            self.counters.add("upstream_failures")
            print(f"upstream {self.endpoint}: {error}", file=sys.stderr, flush=True)
            self.socket.close()
            self.socket = None
            return None

    def close(self):
        if self.socket is not None:
            self.socket.close()
            self.socket = None


class Proxy:
    def __init__(self, profile, rotator, radio, counters, quiet=False):
        self.profile = profile
        self.rotator = rotator
        self.radio = radio
        self.counters = counters
        self.quiet = quiet
        self.azimuth = 0.0
        self.elevation = 0.0
        self.frequency_hz = 0

    def say(self, text):
        if not self.quiet:
            print(text, flush=True)

    def rotator_command(self, line):
        fields = line.split()
        if not fields:
            return ""
        verb = fields[0]
        if verb == "P" and len(fields) >= 3:
            self.counters.add("bearings_in")
            try:
                azimuth, elevation = float(fields[1]), float(fields[2])
            except ValueError:
                return RPRT_REFUSED
            if not self.profile.enabled or not self.profile.allows(azimuth, elevation):
                self.counters.add("refused")
                self.say(f"refused az {azimuth:.1f} el {elevation:.1f} ({self.profile.name})")
                return RPRT_REFUSED
            self.azimuth, self.elevation = azimuth, elevation
            self.say(f"az {azimuth:7.2f}  el {elevation:6.2f}  {self.profile.name}")
            reply = self.rotator.send(f"P {azimuth} {elevation}\n")
            self.counters.add("bearings_out")
            return reply if reply is not None else RPRT_OK
        if verb == "p":
            reply = self.rotator.send("p\n")
            if reply is not None:
                return reply
            # No daemon: answer with what was last accepted, which is what a
            # rotator that reached its bearing would say.
            return f"{self.azimuth:.2f}\n{self.elevation:.2f}\n"
        if verb in ("S", "K"):
            reply = self.rotator.send(f"{verb}\n")
            self.say("stop" if verb == "S" else "park")
            return reply if reply is not None else RPRT_OK
        if verb in ("q", "Q"):
            return None
        return RPRT_REFUSED

    def radio_command(self, line):
        fields = line.split()
        if not fields:
            return ""
        verb = fields[0]
        if verb == "F" and len(fields) >= 2:
            self.counters.add("frequencies_in")
            try:
                frequency = int(float(fields[1]))
            except ValueError:
                return RPRT_REFUSED
            if self.profile.policy == "ignore" or not self.profile.enabled:
                self.counters.add("refused")
                return RPRT_OK
            if self.profile.policy == "hold":
                # Gpredict corrects every cycle, so holding is a standing
                # override and not a value anyone set once.
                frequency = self.profile.frequency_hz
            self.frequency_hz = frequency
            self.say(f"freq {frequency} Hz  {self.profile.name} ({self.profile.policy})")
            reply = self.radio.send(f"F {frequency}\n")
            self.counters.add("frequencies_out")
            return reply if reply is not None else RPRT_OK
        if verb == "f":
            reply = self.radio.send("f\n")
            return reply if reply is not None else f"{self.frequency_hz}\n"
        if verb in ("q", "Q"):
            return None
        return RPRT_REFUSED


def serve(proxy, rotator_port, radio_port, seconds=None):
    listeners = {}
    for port, handler in ((rotator_port, proxy.rotator_command), (radio_port, proxy.radio_command)):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", port))
        listener.listen(1)
        listeners[listener] = handler

    selector = selectors.DefaultSelector()
    for listener, handler in listeners.items():
        selector.register(listener, selectors.EVENT_READ, ("listen", handler, b""))
    deadline = (time.monotonic() + seconds) if seconds else None
    ports = ", ".join(str(port) for port in (rotator_port, radio_port))
    print(f"listening on {ports}; profile {proxy.profile.name} ({proxy.profile.catalogue})",
          flush=True)

    try:
        while deadline is None or time.monotonic() < deadline:
            timeout = None if deadline is None else max(0.0, deadline - time.monotonic())
            for key, _ in selector.select(timeout=timeout or 0.5):
                kind, handler, pending = key.data
                if kind == "listen":
                    client, _ = key.fileobj.accept()
                    selector.register(client, selectors.EVENT_READ, ("client", handler, b""))
                    continue
                data = key.fileobj.recv(256)
                if not data:
                    selector.unregister(key.fileobj)
                    key.fileobj.close()
                    continue
                pending += data
                while b"\n" in pending:
                    line, _, pending = pending.partition(b"\n")
                    reply = handler(line.decode(errors="replace").strip())
                    if reply is None:
                        selector.unregister(key.fileobj)
                        key.fileobj.close()
                        pending = b""
                        break
                    key.fileobj.sendall(reply.encode())
                else:
                    selector.modify(key.fileobj, selectors.EVENT_READ, ("client", handler, pending))
    finally:
        selector.close()
        for listener in listeners:
            listener.close()


def main():
    repository = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--profiles", type=pathlib.Path,
                        default=repository / "ground-station" / "satellites.json")
    parser.add_argument("--profile", help="profile name; the first enabled one by default")
    parser.add_argument("--list", action="store_true", help="show the profiles and stop")
    parser.add_argument("--rotator-port", type=int, default=ROTATOR_PORT)
    parser.add_argument("--radio-port", type=int, default=RADIO_PORT)
    parser.add_argument("--rotator-upstream", help="host:port of the real rotctld")
    parser.add_argument("--radio-upstream", help="host:port of the real rigctld")
    parser.add_argument("--seconds", type=float, help="stop after this long, for a test")
    parser.add_argument("--quiet", action="store_true")
    arguments = parser.parse_args()

    document = json.loads(arguments.profiles.read_text(encoding="utf-8"))
    profiles = [Profile(entry) for entry in document["profiles"]]
    if arguments.list:
        for index, profile in enumerate(profiles):
            state = "enabled" if profile.enabled else "disabled"
            print(f"{index} {profile.name} ({profile.catalogue}) {state} "
                  f"el {profile.elevation[0]:.0f}-{profile.elevation[1]:.0f} "
                  f"freq {profile.policy}")
        return 0

    if arguments.profile:
        chosen = [p for p in profiles if p.name == arguments.profile]
        if not chosen:
            parser.error(f"no profile named {arguments.profile}")
        profile = chosen[0]
        if not profile.enabled:
            parser.error(f"{profile.name} is defined but not enabled")
    else:
        enabled = [p for p in profiles if p.enabled]
        if not enabled:
            parser.error("no profile is enabled")
        profile = enabled[0]

    counters = Counters()
    rotator = Upstream(arguments.rotator_upstream, counters)
    radio = Upstream(arguments.radio_upstream, counters)
    proxy = Proxy(profile, rotator, radio, counters, arguments.quiet)
    try:
        serve(proxy, arguments.rotator_port, arguments.radio_port, arguments.seconds)
    finally:
        rotator.close()
        radio.close()
        print(counters.line(), flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(1)
