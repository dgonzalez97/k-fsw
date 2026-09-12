#!/usr/bin/env python3
"""Exercise encrypted radio traffic with native peers or the Holybro bench."""

import argparse
from contextlib import ExitStack
import importlib.util
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import threading
import time

import serial

spec = importlib.util.spec_from_file_location("can_update", Path(__file__).parents[2] / "fwu/can-update.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class RedactedLog:
    def __init__(self, stream):
        self.stream = stream
        self.pending = ""

    def write(self, text):
        self.pending += text
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            self.stream.write(re.sub(r"[0-9a-fA-F]{64}", "<key>", line) + "\n")

    def flush(self):
        self.stream.flush()


class Bridge:
    def __init__(self, left, right, output):
        self.ports = (left, right)
        self.frames = [[], []]
        self.partial = [bytearray(), bytearray()]
        self.stop = threading.Event()
        self.error = None
        self.output = output
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        try:
            while not self.stop.is_set():
                ready, _, _ = select.select(self.ports, [], [], 0.1)
                for port in ready:
                    index = self.ports.index(port)
                    data = os.read(port.fileno(), 4096)
                    if not data:
                        continue
                    self.ports[1-index].write(data)
                    for byte in data:
                        frame = self.partial[index]
                        if byte == 0xc0:
                            if len(frame) > 2:
                                frame.append(byte)
                                self.frames[index].append(bytes(frame))
                            self.partial[index] = bytearray([byte])
                        elif frame:
                            frame.append(byte)
                            if len(frame) > 4096:
                                self.partial[index].clear()
        except Exception as error:
            self.error = error

    def close(self):
        self.stop.set()
        self.thread.join(timeout=3)
        for index, frames in enumerate(self.frames):
            (self.output / f"wire-{index}.bin").write_bytes(b"".join(frames))


def native(stack, executable, output, label, prompt):
    process = subprocess.Popen([str(executable.resolve()), "--uart_stdinout", "--no-color",
        f"-flash={output / (label + '-flash.bin')}"], cwd=output,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    stack.callback(bench.stop_process, process)
    log = RedactedLog(stack.enter_context((output / (label + '.log')).open('w')))
    console = bench.Console(process.stdout.fileno(), process.stdin.write, prompt, log)
    boot = console.ready(60)
    match = re.search(r"uart_1 connected to pseudotty: (\S+)", boot)
    if match is None:
        raise RuntimeError("Ground UART PTY missing")
    port = stack.enter_context(serial.Serial(match[1], 57600, timeout=0.1, write_timeout=2, exclusive=True))
    return console, port


def sessions(ground, flight):
    for attempt in range(6):
        ground.run("uhf connect")
        time.sleep(0.7)
        gs = ground.run("uhf status")
        fs = flight.run("uhf status")
        if "sessions TX/RX: 1/1" in gs and "sessions TX/RX: 1/1" in fs:
            return
        time.sleep(1)
    raise RuntimeError("Radio sessions did not establish")


def tests(ground, flight, bridge, key, reboot):
    wrong = ("00" if key[:2] != "00" else "ff") + key[2:]
    for console in (ground, flight):
        console.run(f"param set uhf_key_hex {key}")
        console.run("param get uhf_key_hex", 'uhf_key_hex = ""')
        console.run("param get uhf_crypto_error", "uhf_crypto_error = 0")
    sessions(ground, flight)
    for repeat in range(3):
        ground.run("csp ping 2", "CSP ping 2: success")
        flight.run("csp ping 17", "CSP ping 17: success")
    ground.run("param list 2", "uhf_replays", timeout=60)
    ground.run("param get 2 uhf_key_hex", 'uhf_key_hex = ""')
    result = ground.run("param set 2 uhf_encrypt_enable 0", timeout=30)
    if "failed" not in result.lower() and "error" not in result.lower():
        raise RuntimeError("Remote radio-disable write was not refused")
    flight.run("param get uhf_encrypt_enable", "uhf_encrypt_enable = 1")
    start = len(bridge.frames[0])
    ground.run("param set 2 log_level 3", "log_level = 3")
    captured = list(bridge.frames[0][start:])
    if not captured:
        raise RuntimeError("No encrypted request captured")
    flight.run("param set log_level 2", "log_level = 2")
    for frame in captured:
        bridge.ports[1].write(frame)
    time.sleep(0.5)
    ground.run("param get 2 log_level", "log_level = 2")
    replay_status = flight.run("param get uhf_replays")
    if not re.search(r"uhf_replays = [1-9][0-9]*", replay_status):
        raise RuntimeError("Replayed wire request was not counted")
    print("Encrypted traffic, remote-write rejection and wire replay: PASS", flush=True)

    ground.run(f"param set uhf_key_hex {wrong}")
    ground.run("uhf connect")
    time.sleep(1)
    if ": success" in ground.run("csp ping 2"):
        raise RuntimeError("Different radio keys communicated")
    ground.run(f"param set uhf_key_hex {key}")
    sessions(ground, flight)
    ground.run("param set uhf_encrypt_tx 0")
    if ": success" in ground.run("csp ping 2"):
        raise RuntimeError("Plaintext entered the protected flight receiver")
    ground.run("param set uhf_encrypt_tx 1")
    sessions(ground, flight)
    ground.run("csp ping 2", "CSP ping 2: success")
    print("Wrong key and plaintext rejection: PASS", flush=True)

    if reboot:
        flight.reboot("0000")
        flight.run("param get uhf_key_set", "uhf_key_set = 1")
        current = flight.run("param get log_level")
        match = re.search(r"log_level = (\d+)", current)
        if match is None:
            raise RuntimeError("Missing rebooted log level")
        # Startup may already have negotiated fresh sessions.
        for frame in captured:
            bridge.ports[1].write(frame)
        time.sleep(0.5)
        flight.run("param get log_level", f"log_level = {match[1]}")
        sessions(ground, flight)
        ground.run("csp ping 2", "CSP ping 2: success")
        print("Saved key and replay rejection after flight reset: PASS", flush=True)
    for label, console in (("flight", flight), ("ground", ground)):
        console.run("csp interfaces")
        console.run("kernel thread stacks")
        console.run("uhf status")
    if bridge.error:
        raise RuntimeError(str(bridge.error))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", type=Path, required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--flight", type=Path, help="Native flight peer")
    group.add_argument("--serial", help="Flight ST-LINK /dev/serial/by-id path")
    parser.add_argument("--radio", help="Holybro /dev/serial/by-id path")
    parser.add_argument("--key-file", type=Path, required=True, help="Private file containing 64 hex digits")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    key = args.key_file.read_text().strip()
    if re.fullmatch(r"[0-9a-fA-F]{64}", key) is None:
        parser.error("Key file must contain 64 hex digits")
    if args.serial and (not args.serial.startswith("/dev/serial/by-id/") or
                        not args.radio or not args.radio.startswith("/dev/serial/by-id/")):
        parser.error("Use stable serial identities for the flight console and radio")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    try:
        with ExitStack() as stack:
            ground, left = native(stack, args.ground, output, "ground", "kfsw-gnd-crypto# ")
            if args.flight:
                flight, right = native(stack, args.flight, output, "flight", "crypto-flight# ")
            else:
                port = stack.enter_context(serial.Serial(args.serial, 115200, timeout=1,
                    write_timeout=2, exclusive=True))
                log = RedactedLog(stack.enter_context((output / 'flight.log').open('w')))
                flight = bench.Console(port.fileno(), port.write, "kfsw:~$ ", log)
                flight.run("")
                right = stack.enter_context(serial.Serial(args.radio, 57600, timeout=0.1,
                    write_timeout=2, exclusive=True))
            bridge = Bridge(left, right, output)
            stack.callback(bridge.close)
            tests(ground, flight, bridge, key, args.serial is not None)
        print(f"Radio crypto: PASS logs={output}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Radio crypto: FAIL {re.sub(r'[0-9a-fA-F]{64}', '<key>', str(error))}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
