#!/usr/bin/env python3
"""Speak to the proxy the way Gpredict does, and check what it decides."""

import json
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
PROXY = REPO / "tools" / "ground" / "gpredict-proxy.py"


def profiles(path):
    document = {
        "profiles": [
            {
                "name": "LUR-1",
                "catalogue": 60506,
                "enabled": True,
                "azimuth_min_deg": 0,
                "azimuth_max_deg": 360,
                "elevation_min_deg": 5,
                "elevation_max_deg": 90,
                "frequency_policy": "follow",
                "frequency_hz": 0,
            },
            {
                "name": "ROADS 1",
                "catalogue": 64535,
                "enabled": True,
                "azimuth_min_deg": 0,
                "azimuth_max_deg": 360,
                "elevation_min_deg": 5,
                "elevation_max_deg": 90,
                "frequency_policy": "hold",
                "frequency_hz": 437125000,
            },
            {
                "name": "ROADS 2",
                "catalogue": 64549,
                "enabled": False,
                "azimuth_min_deg": 0,
                "azimuth_max_deg": 360,
                "elevation_min_deg": 5,
                "elevation_max_deg": 90,
                "frequency_policy": "ignore",
                "frequency_hz": 0,
            },
        ]
    }
    path.write_text(json.dumps(document), encoding="utf-8")


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def talk(port, lines):
    """One exchange per line, as rotctld and rigctld do."""
    replies = []
    with socket.create_connection(("127.0.0.1", port), timeout=10) as link:
        for line in lines:
            link.sendall((line + "\n").encode())
            replies.append(link.recv(256).decode(errors="replace"))
    return replies


def fail(message):
    print(f"GPREDICT PROXY RESULT: FAIL\n  {message}")
    raise SystemExit(1)


def start(path, profile, rotator, radio):
    process = subprocess.Popen(
        [sys.executable, str(PROXY), "--profiles", str(path), "--profile", profile,
         "--rotator-port", str(rotator), "--radio-port", str(radio), "--seconds", "25"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    for _ in range(100):
        try:
            with socket.create_connection(("127.0.0.1", rotator), timeout=0.5):
                return process
        except OSError:
            time.sleep(0.1)
    process.kill()
    fail("the proxy never started listening")


def main():
    with tempfile.TemporaryDirectory() as folder:
        path = pathlib.Path(folder) / "satellites.json"
        profiles(path)
        rotator, radio = free_port(), free_port()

        process = start(path, "LUR-1", rotator, radio)
        try:
            # A bearing inside the travel, then one below the horizon limit.
            replies = talk(rotator, ["P 120.0 35.0", "p", "P 120.0 2.0", "q"])
            if not replies[0].startswith("RPRT 0"):
                fail(f"a bearing inside the travel was answered {replies[0]!r}")
            if "120.00" not in replies[1] or "35.00" not in replies[1]:
                fail(f"the position read back as {replies[1]!r}")
            if not replies[2].startswith("RPRT -1"):
                fail(f"a bearing below the elevation limit was answered {replies[2]!r}")

            # follow passes the frequency through unchanged.
            replies = talk(radio, ["F 437505000", "f", "q"])
            if not replies[0].startswith("RPRT 0"):
                fail(f"a frequency was answered {replies[0]!r}")
            if "437505000" not in replies[1]:
                fail(f"the frequency read back as {replies[1]!r}")
        finally:
            process.wait(timeout=40)

        # hold overrides whatever Gpredict sends, every cycle.
        rotator, radio = free_port(), free_port()
        process = start(path, "ROADS 1", rotator, radio)
        try:
            replies = talk(radio, ["F 437505000", "f", "q"])
            if "437125000" not in replies[1]:
                fail(f"hold did not override the frequency: {replies[1]!r}")
        finally:
            process.wait(timeout=40)

        # A profile that is defined but not enabled cannot be selected.
        refused = subprocess.run(
            [sys.executable, str(PROXY), "--profiles", str(path), "--profile", "ROADS 2",
             "--seconds", "1"], capture_output=True, text=True, timeout=30)
        if refused.returncode == 0 or "not enabled" not in refused.stderr:
            fail(f"a disabled profile was selected: {refused.returncode} {refused.stderr!r}")

    print("GPREDICT PROXY RESULT: PASS travel refusal, follow, hold and the disabled profile")
    return 0


if __name__ == "__main__":
    sys.exit(main())
