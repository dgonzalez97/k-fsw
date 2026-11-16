#!/usr/bin/env python3
"""Measure a loaded CAN node and reset interrupted secondary-slot updates."""

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import importlib.util
import hashlib
import os
import json
import re
from pathlib import Path
import subprocess
import sys
import time

import serial

spec = importlib.util.spec_from_file_location("can_update", Path(__file__).with_name("can-update.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

BEACON_INTERVAL_MS = 5000


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


def hk_counters(text):
    result = {}
    for field in ("collections", "failed collections", "absent values", "beacons sent"):
        match = re.search(rf"^{field}: (\d+)$", text, re.MULTILINE)
        if match is None:
            raise RuntimeError(f"Missing housekeeping counter: {field}")
        result[field] = int(match[1])
    return result


def soak(board, ground, seconds, output, profile, probes=False):
    if seconds <= 0 or profile not in ("minimal", "obc") or (probes and profile != "obc"):
        raise ValueError("Load needs positive seconds and an explicit minimal/obc profile")
    started = time.monotonic()
    result = {"result": "FAIL", "profile": profile, "probes": probes,
              "duration_requested_s": seconds, "duration_active_s": 0,
              "transfer_cycles": 0, "verified_roundtrip_bytes": 0,
              "reads": 0, "control_reads": 0, "transfer_cycle_max_s": 0,
              "operations": {}, "errors": [],
              "latency_method": "Host monotonic command round-trip, interleaved with FTP; "
                                "includes ground console/scheduling, not target WCET"}
    active = None
    completed = False
    interrupted = None
    hk_before = None
    hk_changed = False
    probes_started = False
    write_json(output / "soak.json", result)

    def operation(name, console, command, expected=None, timeout=30, pattern=None):
        stats = result["operations"].setdefault(name, {"attempted": 0, "succeeded": 0,
                                                      "failed": 0, "max_s": 0})
        stats["attempted"] += 1
        begin = time.monotonic()
        status = "FAIL"
        try:
            text = console.run(command, expected, timeout=timeout)
            if pattern is not None and re.search(pattern, text, re.MULTILINE) is None:
                raise RuntimeError(f"Unexpected reply to {command}: {text[:200]}")
            stats["succeeded"] += 1
            status = "PASS"
            return text
        except BaseException:
            stats["failed"] += 1
            raise
        finally:
            elapsed = time.monotonic() - begin
            stats["max_s"] = max(stats["max_s"], elapsed)
            with (output / "operations.jsonl").open("a") as log:
                log.write(json.dumps({"operation": name, "command": command,
                                      "duration_s": elapsed, "result": status}) + "\n")

    try:
        ground.run(f"param listall {bench.FLIGHT}", "node_id")
        ground.run("ftp generate /soak-source.bin 4096", ": PASS")
        if profile == "obc":
            before = board.run("hk show", "collections:")
            (output / "hk-before.txt").write_text(before)
            hk_before = hk_counters(before)
            hk_changed = True
            board.run("hk define 0 1:0 5:0 5:4 5:8", "defines 4 values")
            board.run(f"hk define 1 {bench.GROUND}:1:0 {bench.GROUND}:5:0", "defines 2 values")
            board.run("hk period 0 1000", "every 1000 ms")
            board.run("hk period 1 1000", "every 1000 ms")
            board.run(f"hk beacon 0 {bench.GROUND} {BEACON_INTERVAL_MS}",
                      f"beacons to node {bench.GROUND}")
        if probes:
            board.run("hil start", "Timing started")
            probes_started = True
        active = time.monotonic()
        deadline = active + seconds
        while time.monotonic() < deadline:
            cycle_started = time.monotonic()
            operation("ftp_put", ground,
                      f"ftp put {bench.FLIGHT} /soak-source.bin /soak.bin", ": PASS", 60)
            operation("ftp_get", ground,
                      f"ftp get {bench.FLIGHT} /soak.bin /soak-copy.bin", ": PASS", 60)
            operation("ftp_verify", ground,
                      "ftp verify /soak-source.bin /soak-copy.bin", ": PASS", 60)
            result["verified_roundtrip_bytes"] += 4096
            operation("slot_read", ground,
                      f"ftp get {bench.FLIGHT} /boot/firmware_1.bin /readback.bin", ": PASS", 600)
            result["transfer_cycles"] += 1
            result["transfer_cycle_max_s"] = max(result["transfer_cycle_max_s"],
                                                   time.monotonic() - cycle_started)
            operation("param", ground, f"param get {bench.FLIGHT} node_id", "node_id = ",
                      pattern=rf"^{bench.FLIGHT}:node_id = {bench.FLIGHT}$")
            result["reads"] += 1
            operation("control", ground, f"status {bench.FLIGHT}", "storage: ready",
                      pattern=r"^  k-fsw: \S+$")
            result["control_reads"] += 1
            write_json(output / "soak.json", result)
            print(f"Soak: {result['transfer_cycles']} verified transfer cycles", flush=True)
        if not result["transfer_cycles"] or not result["reads"] or not result["control_reads"]:
            raise RuntimeError("No complete transfer/PARAM/control work measured")
        completed = True
    except BaseException as error:
        result["errors"].append(str(error) or type(error).__name__)
        if not isinstance(error, Exception):
            raise
    finally:
        if active is not None:
            result["duration_active_s"] = time.monotonic() - active
        result["duration_overshoot_s"] = max(0, result["duration_active_s"] - seconds)
        write_json(output / "soak.json", result)
        cleanup = []
        if probes_started:
            cleanup.extend((("timing", "hil report", "untracked=0"),
                            ("stacks", "kernel thread stacks", "unused")))
        if hk_changed:
            cleanup.extend((("hk-after", "hk show", "collections:"),
                            (None, "hk period 0 0", "every 0 ms"),
                            (None, "hk period 1 0", "every 0 ms"),
                            (None, f"hk beacon 0 {bench.GROUND} 0", "stops beaconing")))
        for name, command, expected in cleanup:
            try:
                text = board.run(command, expected)
                if name is not None:
                    (output / f"{name}.txt").write_text(text)
                if name == "timing":
                    match = re.search(r"hk_collect count=(\d+)", text)
                    if match is None or int(match[1]) == 0:
                        raise RuntimeError("No housekeeping collections measured by probes")
                if name == "hk-after":
                    after = hk_counters(text)
                    result["hk_collections_delta"] = after["collections"] - hk_before["collections"]
                    result["hk_beacons_delta"] = after["beacons sent"] - hk_before["beacons sent"]
                    if (result["duration_active_s"] >= BEACON_INTERVAL_MS / 1000 and
                            result["hk_beacons_delta"] <= 0):
                        raise RuntimeError("No housekeeping beacon progress measured")
                    if result["hk_collections_delta"] <= 0:
                        raise RuntimeError("No housekeeping collection progress measured")
                    if (after["failed collections"] != hk_before["failed collections"] or
                            after["absent values"] != hk_before["absent values"]):
                        raise RuntimeError("Housekeeping reported failed collections or absent values")
            except BaseException as error:
                result["errors"].append(f"cleanup {command}: {str(error) or type(error).__name__}")
                if not isinstance(error, Exception) and interrupted is None:
                    interrupted = error
        result["duration_measured_s"] = time.monotonic() - started
        result["remote_read_max_s"] = result["operations"].get("param", {}).get("max_s", 0)
        result["control_read_max_s"] = result["operations"].get("control", {}).get("max_s", 0)
        if completed and not result["errors"]:
            result["result"] = "PASS"
        write_json(output / "soak.json", result)
    if interrupted is not None:
        raise interrupted
    if result["result"] != "PASS":
        raise RuntimeError(str(result["errors"] or ["Load interrupted before completion"]))
    return result


def boot_attempts_max(board):
    """The limit a cut reset spends one of. Eight cuts outlast a budget of three."""
    reply = board.run("golden attempts")
    match = re.search(r"attempts left: \d+ of (\d+)", reply)
    if match is None:
        raise RuntimeError(f"No boot attempt budget on this image: {reply[:120]}")
    return int(match[1])


def interruptions(board, ground, image, output):
    revision = bench.remote_revision(ground)
    bench.read_slot(ground, 1, "/original.bin")
    limit = boot_attempts_max(board)
    results = []
    for protocol in ("ftp", "lite"):
        for operation, count in (("erase", 1), ("erase", 80), ("write", 512), ("write", 65536)):
            board.run("mcuboot", "confirmed: 1")
            board.run(f"golden attempts {limit}", f"attempts left: {limit} of {limit}")
            board.run(f"hil cut {operation} {count}", "Reset armed:")
            command = (f"ftp put {bench.FLIGHT} /candidate.bin /firmware.bin" if protocol == "ftp"
                       else f"fwu_lite send {bench.FLIGHT} {image}")
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
            spent = board.run("golden attempts")
            if f"attempts left: {limit - 1} of {limit}" not in spent:
                raise RuntimeError(f"Cut reset did not spend one attempt: {spent[:120]}")
            bench.read_slot(ground, 1, "/readback.bin")
            bench.compare(ground, "/original.bin", "/readback.bin")
            results.append({"protocol": protocol, "operation": operation, "after": count,
                            "sender_recovered": ": PASS" in reply or "Upload complete" in reply,
                            "result": "PASS", "reset_marker": boot.splitlines()[0]})
            (output / "interruptions.json").write_text(json.dumps(results, indent=2))
            print(f"Reset test: {protocol} {operation} {count}: PASS", flush=True)
    ground.run(f"fwu_lite send {bench.FLIGHT} {image}", "Upload complete", timeout=600)
    ground.run(f"fwu_lite abort {bench.FLIGHT}", "Owned transfer aborted", timeout=60)
    board.run("mcuboot", "confirmed: 1")
    bench.read_slot(ground, 1, "/readback.bin")
    bench.compare(ground, "/original.bin", "/readback.bin")


def helper(name, path):
    module_spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def prepare_inputs(args):
    if not args.serial.startswith("/dev/serial/by-id/") or args.seconds < 0:
        raise ValueError("Use a stable serial identity and nonnegative duration")
    if not args.seconds and not args.interruptions:
        raise ValueError("Zero seconds without interruptions performs no work")
    if args.seconds and args.profile is None:
        raise ValueError("Load needs --profile minimal or obc")
    if args.probes and args.profile != "obc":
        raise ValueError("Timing probes require an explicitly instrumented obc profile")
    if args.seconds and args.interruptions and not args.probes:
        raise ValueError("Combined load/interruptions requires --probes and an instrumented obc image")
    if not os.access(args.ground, os.X_OK):
        raise ValueError("Ground executable is missing or not executable")
    repo = Path(__file__).resolve().parents[3]
    config_tool = helper("profile_config", repo / "tools/ci/profile-config.py")
    ground_config = args.ground.resolve().parent / ".config"
    ground_bytes = ground_config.read_bytes()
    ground = config_tool.configuration(ground_config)
    if ground_config.read_bytes() != ground_bytes:
        raise ValueError("Ground configuration changed during validation")
    for name, expected in {"CONFIG_KFSW_CSP_ADDRESS": str(bench.GROUND),
                           "CONFIG_KFSW_CSP_CAN": "y",
                           "CONFIG_KFSW_CSP_VERSION_2": "y",
                           "CONFIG_KFSW_CSP_CAN_BITRATE": "500000",
                           "CONFIG_KFSW_CSP_ROUTE_TABLE": '\"0/0 CAN\"'}.items():
        if ground.get(name) != expected:
            raise ValueError(f"Ground {name}: expected {expected}")
    paths = {"ground.exe": args.ground, "ground.config": ground_config,
             "ground-flash.bin": args.flash}
    context = None
    selection = None
    expected = None
    if args.seconds:
        if not all((args.flight_image, args.flight_config, args.context)):
            raise ValueError("Load needs --flight-image, --flight-config and --context")
        requirements_path = repo / "tests/requirements/profiles/reference.json"
        requirements_bytes = requirements_path.read_bytes()
        expected = config_tool.requirements(requirements_path, args.profile + "-l496")
        if requirements_path.read_bytes() != requirements_bytes:
            raise ValueError("Reference requirements changed during validation")
        paths["reference-requirements.json"] = requirements_path
        selection = args.profile + "-l496"
        if args.probes:
            expected["CONFIG_KFSW_HIL_PROBES"] = "y"
            selection += "-probes"
        flight_bytes = args.flight_config.read_bytes()
        actual = config_tool.configuration(args.flight_config)
        if args.flight_config.read_bytes() != flight_bytes:
            raise ValueError("Flight configuration changed during validation")
        wrong = [name for name, value in expected.items() if actual.get(name, "n") != value]
        if wrong:
            raise ValueError("Flight configuration differs from selected requirements: " + ", ".join(wrong))
        binding = helper("image_binding", repo / "tools/ci/robot-counts.py")
        context_bytes = args.context.read_bytes()
        context = binding.read_json(args.context)
        if args.context.read_bytes() != context_bytes:
            raise ValueError("Context changed during validation")
        binding.check_context(context)
        if context["profile"] != selection or context["evidence_kind"] != "physical":
            raise ValueError("Context profile/evidence_kind differs from selected physical load")
        digest = hashlib.sha256(flight_bytes).hexdigest()
        if context["config_sha256"] != digest:
            raise ValueError("Flight configuration SHA-256 differs from context")
        paths.update({"flight-image.bin": args.flight_image, "flight.config": args.flight_config,
                      "context.json": args.context})
    if args.interruptions:
        if args.image is None:
            raise ValueError("Interruptions need --image and a ground flash containing /candidate.bin")
        paths["candidate.bin"] = args.image
    for label, config in (("ground", ground_config), ("flight", args.flight_config if args.seconds else None)):
        if config is not None and (config.parent / "zephyr.dts").is_file():
            paths[f"{label}.dts"] = config.parent / "zephyr.dts"
    data = {name: path.read_bytes() for name, path in paths.items()}
    if data["ground.config"] != ground_bytes:
        raise ValueError("Ground configuration changed before capture")
    if args.seconds and (data["flight.config"] != flight_bytes or data["context.json"] != context_bytes
                         or data["reference-requirements.json"] != requirements_bytes):
        raise ValueError("Flight configuration/context changed before capture")
    metadata = {"context": context, "selection": selection, "expected_config": expected,
                "derived_instrumented_selection": bool(args.seconds and args.probes),
                "identity_limit": "Input consistency only; hashes and CSP revision do not attest installed bytes",
                "fixture_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in (Path(__file__), Path(__file__).with_name("can-update.py"),
                                                repo / "tools/ci/profile-config.py",
                                                repo / "tools/ci/robot-counts.py")},
                "inputs": {name: {"path": str(paths[name].resolve()),
                                  "sha256": hashlib.sha256(value).hexdigest()}
                           for name, value in data.items()}}
    return data, metadata


def check_can(state):
    data = state.get("linkinfo", {}).get("info_data", {})
    if data.get("state") != "ERROR-ACTIVE" or data.get("bittiming", {}).get("bitrate") != 500000:
        raise RuntimeError("CAN must be ERROR-ACTIVE at 500000 bit/s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", type=Path, required=True)
    parser.add_argument("--image", type=Path, help="Signed candidate, required only for interruptions")
    parser.add_argument("--flight-image", type=Path, help="Actual selected baseline artifact for load")
    parser.add_argument("--flight-config", type=Path, help="Resolved baseline .config")
    parser.add_argument("--context", type=Path, help="Independently recorded image-binding build context")
    parser.add_argument("--profile", choices=("minimal", "obc"))
    parser.add_argument("--probes", action="store_true", help="Separate instrumented OBC load")
    parser.add_argument("--flash", type=Path, required=True, help="Existing ground filesystem snapshot")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--interface", default="can0")
    parser.add_argument("--seconds", type=int, default=3600)
    parser.add_argument("--interruptions", action="store_true")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        files, metadata = prepare_inputs(args)
    except (OSError, ValueError, UnicodeError) as error:
        parser.error(str(error))
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (output / name).write_bytes(data)
    (output / "ground.exe").chmod(0o700)
    record = {"result": "FAIL", "run_id": output.name,
              "started_utc": datetime.now(timezone.utc).isoformat(),
              "arguments": sys.argv[1:], "artifacts": metadata, "errors": []}
    write_json(output / "run.json", record)
    observed_can = False
    finished = False
    interrupted = None
    try:
        before = bench.can_state(args.interface)
        observed_can = True
        write_json(output / "can-before.json", before)
        check_can(before)
        with ExitStack() as stack:
            port = stack.enter_context(serial.Serial(args.serial, 115200, timeout=1, exclusive=True))
            board_log = stack.enter_context((output / "board.log").open("w"))
            board = bench.Console(port.fileno(), port.write, "kfsw:~$ ", board_log)
            board.run("")
            board.run("mcuboot", "confirmed: 1")
            routes = board.run("csp routes", f"{bench.GROUND}/14 -> CAN direct")
            if re.search(rf"(?m)^\s*{bench.GROUND}/14 -> CAN direct\s*$", routes) is None:
                raise RuntimeError("Target has no exact ground CAN route")
            board.run("csp interfaces", f"CAN addr={bench.FLIGHT}/")
            process = subprocess.Popen([str(output / "ground.exe"), "--uart_stdinout",
                f"--device_id={bench.GROUND}", "--no-color", f"--can-if={args.interface}",
                f"-flash={output / 'ground-flash.bin'}"], cwd=output,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            stack.callback(bench.stop_process, process)
            ground_log = stack.enter_context((output / "ground.log").open("w"))
            ground = bench.Console(process.stdout.fileno(), process.stdin.write, "kfsw-gnd-can# ", ground_log)
            ground.ready()
            ground.run("csp routes", "0/0 -> CAN direct")
            ground.run("csp interfaces", f"CAN addr={bench.GROUND}/")
            bench.check_link(ground, board)
            record["revision_before"] = bench.remote_revision(ground)
            if args.seconds:
                soak(board, ground, args.seconds, output, args.profile, args.probes)
                record["revision_after_load"] = bench.remote_revision(ground)
                if record["revision_after_load"] != record["revision_before"]:
                    raise RuntimeError("Running revision changed during load")
            if args.interruptions:
                interruptions(board, ground, output / "candidate.bin", output)
            board.run("csp interfaces")
            ground.run("csp interfaces")
        for name in files:
            if name != "ground-flash.bin" and hashlib.sha256((output / name).read_bytes()).hexdigest() != metadata["inputs"][name]["sha256"]:
                raise RuntimeError(f"Input changed during run: {name}")
        finished = True
    except BaseException as error:
        record["errors"].append(str(error) or type(error).__name__)
        if not isinstance(error, (OSError, RuntimeError, subprocess.CalledProcessError)):
            interrupted = error
    finally:
        if observed_can:
            try:
                after = bench.can_state(args.interface)
                write_json(output / "can-after.json", after)
                check_can(after)
            except BaseException as error:
                record["errors"].append(f"CAN after run: {str(error) or type(error).__name__}")
                if not isinstance(error, (OSError, RuntimeError, subprocess.CalledProcessError)) and interrupted is None:
                    interrupted = error
        if finished and not record["errors"]:
            record["result"] = "PASS"
        record["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_json(output / "run.json", record)
    if interrupted is not None:
        raise interrupted
    print(f"CAN acceptance: {record['result']} logs={output}")
    if record["errors"]:
        print(str(record["errors"]), file=sys.stderr)
    return 0 if record["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
