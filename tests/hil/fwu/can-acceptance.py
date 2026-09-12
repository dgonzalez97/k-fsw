#!/usr/bin/env python3
"""Measure a loaded CAN node and reset interrupted secondary-slot updates."""

import argparse
from contextlib import ExitStack
import importlib.util
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

import serial

spec = importlib.util.spec_from_file_location("can_update", Path(__file__).with_name("can-update.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def soak(board, ground, seconds, output):
    board.run(f"csp clock set {int(time.time())}", "UTC")
    board.run("hk show")
    board.run("hk define 0 1:0 5:0 5:4 5:8 51:0 51:4 51:8 51:16", "defines 8 values")
    board.run("hk define 1 16:1:0 16:5:0", "defines 2 values")
    board.run("hk period 0 1000", "every 1000 ms")
    board.run("hk period 1 1000", "every 1000 ms")
    board.run("hk beacon 0 16 5000")
    ground.run("param list 2", "temp_valid")
    ground.run("ftp generate /soak-source.bin 4096", ": PASS")
    board.run("hil start", "Timing started")
    deadline = time.monotonic() + seconds
    transfers = []
    errors = []
    reads = 0
    latencies = []
    started = time.monotonic()
    try:
        # Flash operations can stall the MCU long enough to lose console UART
        # bytes. Keep console writes outside the load; HK supplies concurrent
        # flight-to-ground PARAM traffic while this client transfers files.
        while time.monotonic() < deadline:
            begin = time.monotonic()
            ground.run("ftp put 2 /soak-source.bin /soak.bin", ": PASS", timeout=60)
            ground.run("ftp get 2 /soak.bin /soak-copy.bin", ": PASS", timeout=60)
            bench.compare(ground, "/soak-source.bin", "/soak-copy.bin")
            ground.run("ftp get 2 /boot/firmware_1.bin /readback.bin", ": PASS", timeout=600)
            transfers.append(time.monotonic() - begin)
            begin = time.monotonic()
            ground.run("param get 2 temp_valid", "temp_valid = 1")
            latencies.append(time.monotonic() - begin)
            reads += 1
            print(f"Soak: {len(transfers)} verified transfer cycles", flush=True)
    except Exception as error:
        errors.append(str(error))
    finally:
        timing = board.run("hil report", "untracked=0")
        (output / "timing.txt").write_text(timing)
        match = re.search(r"hk_collect count=(\d+)", timing)
        if match is None or int(match[1]) == 0:
            errors.append("No housekeeping collections measured")
        (output / "hk-after.txt").write_text(board.run("hk show"))
        (output / "stacks.txt").write_text(board.run("kernel thread stacks", "unused"))
        board.run("hk period 0 0")
        board.run("hk period 1 0")
        board.run("hk beacon 0 16 0")
    result = {"duration_requested_s": seconds, "duration_measured_s": time.monotonic() - started,
              "transfer_cycles": len(transfers),
              "reads": reads, "remote_read_max_s": max(latencies, default=0),
              "transfer_cycle_max_s": max(transfers, default=0), "errors": errors}
    (output / "soak.json").write_text(json.dumps(result, indent=2))
    if errors:
        raise RuntimeError(str(errors))


def interruptions(board, ground, image, output):
    revision = bench.remote_revision(ground)
    bench.read_slot(ground, 1, "/original.bin")
    results = []
    for protocol in ("ftp", "lite"):
        for operation, count in (("erase", 1), ("erase", 80), ("write", 512), ("write", 65536)):
            board.run("mcuboot", "confirmed: 1")
            board.run(f"hil cut {operation} {count}", "Reset armed:")
            command = ("ftp put 2 /candidate.bin /firmware.bin" if protocol == "ftp"
                       else f"fwu send 2 {image}")
            reply = ground.run(command, timeout=600)
            board.wait_for("@HIL_CUT ", timeout=30)
            boot = board.wait_for("@READY ", timeout=60)
            if not boot.startswith(operation):
                raise RuntimeError(f"Wrong reset boundary: {boot[:120]}")
            board.run("")
            bench.check_link(ground, board)
            board.run("mcuboot", "confirmed: 1")
            if bench.remote_revision(ground) != revision:
                raise RuntimeError("Interrupted upload changed the running image")
            bench.read_slot(ground, 1, "/readback.bin")
            bench.compare(ground, "/original.bin", "/readback.bin")
            results.append({"protocol": protocol, "operation": operation, "after": count,
                            "sender_recovered": ": PASS" in reply or "Image accepted and verified" in reply,
                            "result": "PASS", "reset_marker": boot.splitlines()[0]})
            (output / "interruptions.json").write_text(json.dumps(results, indent=2))
            print(f"Reset test: {protocol} {operation} {count}: PASS", flush=True)
    ground.run(f"fwu send 2 {image}", "Image accepted and verified", timeout=600)
    board.run("fwu abort", timeout=60)
    board.run("mcuboot", "confirmed: 1")
    bench.read_slot(ground, 1, "/readback.bin")
    bench.compare(ground, "/original.bin", "/readback.bin")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--flash", type=Path, required=True, help="Existing ground flash containing /candidate.bin")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--interface", default="can0")
    parser.add_argument("--seconds", type=int, default=3600)
    parser.add_argument("--interruptions", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.serial.startswith("/dev/serial/by-id/") or args.seconds < 0:
        parser.error("Use a stable serial identity and nonnegative duration")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(args.flash, output / "ground-flash.bin")
    (output / "can-before.json").write_text(json.dumps(bench.can_state(args.interface), indent=2))
    try:
        with ExitStack() as stack:
            port = stack.enter_context(serial.Serial(args.serial, 115200, timeout=1, exclusive=True))
            board_log = stack.enter_context((output / "board.log").open("w"))
            board = bench.Console(port.fileno(), port.write, "kfsw:~$ ", board_log)
            board.run("")
            board.run("mcuboot", "confirmed: 1")
            process = subprocess.Popen([str(args.ground.resolve()), "--uart_stdinout",
                "--device_id=16", "--no-color", f"--can-if={args.interface}",
                f"-flash={output / 'ground-flash.bin'}"], cwd=output,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            stack.callback(bench.stop_process, process)
            ground_log = stack.enter_context((output / "ground.log").open("w"))
            ground = bench.Console(process.stdout.fileno(), process.stdin.write, "kfsw-gnd-can# ", ground_log)
            ground.ready()
            bench.check_link(ground, board)
            if args.seconds:
                soak(board, ground, args.seconds, output)
            if args.interruptions:
                interruptions(board, ground, args.image.resolve(), output)
            board.run("csp interfaces")
            ground.run("csp interfaces")
        (output / "can-after.json").write_text(json.dumps(bench.can_state(args.interface), indent=2))
        print(f"CAN acceptance: PASS logs={output}")
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"CAN acceptance: FAIL {error} logs={output}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
