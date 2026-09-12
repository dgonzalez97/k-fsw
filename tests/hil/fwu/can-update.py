#!/usr/bin/env python3
"""Test FTP and FWU lite uploads, slot readback, revert and confirmation over CAN."""

import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import termios
import time

import serial


ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
FLIGHT = 2
GROUND = 16


def require(text, expected):
    if expected not in text:
        raise RuntimeError(f"Expected {expected!r}, received:\n{text}")
    return text


class Console:
    def __init__(self, fd, write, prompt, log):
        self.fd = fd
        self.write = write
        self.prompt = prompt
        self.log = log
        self.buffer = ""

    def wait_for(self, marker, timeout=30):
        deadline = time.monotonic() + timeout
        while True:
            position = self.buffer.find(marker)
            if position >= 0:
                result = self.buffer[:position]
                self.buffer = self.buffer[position + len(marker):]
                return ANSI.sub("", result).replace("\r", "")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise RuntimeError(f"Timed out waiting for {marker!r}:\n{self.buffer[-2000:]}")
            data = os.read(self.fd, 4096)
            if not data:
                raise RuntimeError("Console closed")
            text = data.decode("utf-8", errors="replace")
            self.log.write(text)
            self.log.flush()
            self.buffer += text

    def ready(self, timeout=30):
        return self.wait_for(self.prompt, timeout)

    def run(self, command, expected=None, timeout=30):
        self.log.write(f"\n> {command}\n")
        self.write((command + "\r").encode())
        text = self.ready(timeout)
        if expected is not None:
            require(text, expected)
        return text

    def reboot(self, pin):
        self.run(f"cmd reboot {pin}", "rebooting in")
        self.wait_for("@READY ", timeout=60)
        self.run("")


def stop_process(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    process.stdin.close()
    process.stdout.close()


def can_state(interface):
    result = subprocess.run(
        ["ip", "-j", "-details", "-statistics", "link", "show", interface],
        check=True, capture_output=True, text=True,
    )
    return json.loads(result.stdout)[0]


def remote_revision(ground):
    text = ground.run(f"csp ident {FLIGHT}", "revision:")
    match = re.search(r"^revision: ([^\n]+)", text, re.MULTILINE)
    if match is None:
        raise RuntimeError("Missing remote image revision")
    return match[1].strip()


def check_link(ground, board, attempts=3):
    """Ping both ways, retrying the ground's first attempt.

    A ground node that has just started has not yet put anything on the bus,
    and its first ping can be lost while the interface settles: the same ping
    succeeds a second later. Failing the whole acceptance on it reports a link
    fault that is not there, so the first direction is retried and only a
    repeated failure is believed.
    """
    for attempt in range(attempts):
        try:
            ground.run(f"csp ping {FLIGHT}", f"CSP ping {FLIGHT}: success")
            break
        except RuntimeError:
            if attempt == attempts - 1:
                raise
            time.sleep(2)
    board.run(f"csp ping {GROUND}", f"CSP ping {GROUND}: success")


def read_slot(ground, slot, destination):
    ground.run(f"ftp get {FLIGHT} /boot/firmware_{slot}.bin {destination}",
               ": PASS bytes=", timeout=600)


def compare(ground, first, second):
    ground.run(f"ftp verify {first} {second}", ": PASS", timeout=60)


def check_trial(ground, board, expected_revision):
    check_link(ground, board)
    if remote_revision(ground) != expected_revision:
        raise RuntimeError("The candidate did not boot")
    board.run("mcuboot", "confirmed: 0")
    board.run("fwu abort", "cleanup failed (-16)")
    read_slot(ground, 1, "/readback.bin")
    compare(ground, "/candidate.bin", "/readback.bin")
    read_slot(ground, 2, "/readback.bin")
    compare(ground, "/before.bin", "/readback.bin")


def run_test(arguments, output):
    repo = Path(__file__).resolve().parents[3]
    before = can_state(arguments.interface)
    data = before["linkinfo"]["info_data"]
    if data.get("state") != "ERROR-ACTIVE" or data.get("bittiming", {}).get("bitrate") != 500000:
        raise RuntimeError("CAN must be ERROR-ACTIVE at 500000 bit/s")
    (output / "can-before.json").write_text(json.dumps(before, indent=2))
    image = arguments.image.resolve()
    (output / "image.sha256").write_text(hashlib.sha256(image.read_bytes()).hexdigest() + "\n")
    sources = {}
    for name in ("k-fsw", "kfsw-platform", "kfsw-services", "kfsw-comms", "kfsw-modules"):
        source = repo.parent / name
        revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        diff = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--"])
        untracked = subprocess.check_output([
            "git", "-C", str(source), "ls-files", "--others", "--exclude-standard"], text=True).splitlines()
        sources[name] = {"commit": revision, "diff_sha256": hashlib.sha256(diff).hexdigest(),
                         "untracked": {path: hashlib.sha256((source / path).read_bytes()).hexdigest()
                                       for path in untracked if (source / path).is_file()}}
    (output / "sources.json").write_text(json.dumps(sources, indent=2) + "\n")
    for label, directory in (("flight", image.parent), ("ground", arguments.ground.resolve().parent)):
        for name in (".config", "zephyr.dts"):
            path = directory / name
            if path.is_file():
                (output / f"{label}-{name.lstrip('.')}").write_bytes(path.read_bytes())

    flash = output / "ground-flash.bin"
    subprocess.run([
        sys.executable, str(repo / "tools/ground/stage-file.py"),
        "--flash", str(flash), "--offset", "0xfc000", "--size", "0x200000",
        "--flash-size", "0x400000", str(image), "/ftp/candidate.bin",
    ], check=True)

    with ExitStack() as stack:
        saved_fd = os.open(arguments.serial, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        stack.callback(os.close, saved_fd)
        saved_attributes = termios.tcgetattr(saved_fd)
        stack.callback(termios.tcsetattr, saved_fd, termios.TCSANOW, saved_attributes)
        port = stack.enter_context(serial.Serial(
            arguments.serial, 115200, timeout=1, write_timeout=2, exclusive=True))
        board_log = stack.enter_context((output / "board.log").open("w"))
        board = Console(port.fileno(), port.write, arguments.board_prompt, board_log)
        board.run("")
        board.run("fwu status", "target: bound")
        board.run("csp routes", "16/14 -> CAN direct")
        board.run("mcuboot", "confirmed: 1")

        process = subprocess.Popen([str(arguments.ground.resolve()),
            "--uart_stdinout", "--device_id=16", "--no-color",
            f"--can-if={arguments.interface}", f"-flash={flash}",
        ], cwd=output, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0)
        stack.callback(stop_process, process)
        ground_log = stack.enter_context((output / "ground.log").open("w"))
        ground = Console(process.stdout.fileno(), process.stdin.write,
                         "kfsw-gnd-can# ", ground_log)
        ground.ready()
        ground.run("csp routes", "0/0 -> CAN direct")
        ground.run("csp interfaces", "CAN addr=16/")
        board.run("csp interfaces", "CAN addr=2/")
        check_link(ground, board)
        original_revision = remote_revision(ground)
        if original_revision == arguments.revision:
            raise RuntimeError("Candidate and running revisions must differ")
        ground.run(f"ftp list {FLIGHT} /boot", "firmware_1.bin")
        read_slot(ground, 1, "/before.bin")
        ground.run(f"ftp put {FLIGHT} /candidate.bin /boot/firmware_1.bin", "FAIL (-5)")
        read_slot(ground, 1, "/readback.bin")
        compare(ground, "/before.bin", "/readback.bin")

        print("CAN FWU: FTP upload and readback", flush=True)
        ground.run(f"ftp put {FLIGHT} /candidate.bin /firmware.bin", ": PASS bytes=",
                   timeout=600)
        board.run("fwu status", "swap_scheduled: yes")
        read_slot(ground, 2, "/readback.bin")
        compare(ground, "/candidate.bin", "/readback.bin")
        board.reboot(arguments.reboot_pin)
        check_trial(ground, board, arguments.revision)

        print("CAN FWU: revert without confirmation", flush=True)
        board.reboot(arguments.reboot_pin)
        check_link(ground, board)
        if remote_revision(ground) != original_revision:
            raise RuntimeError("The unconfirmed image did not revert")
        read_slot(ground, 1, "/readback.bin")
        compare(ground, "/before.bin", "/readback.bin")

        print("CAN FWU: FWU lite upload and readback", flush=True)
        ground.run(f"fwu send {FLIGHT} {image}", "Image accepted and verified", timeout=600)
        status = board.run("fwu status", "state: verified")
        require(status, "swap_scheduled: no")
        read_slot(ground, 2, "/readback.bin")
        compare(ground, "/candidate.bin", "/readback.bin")
        ground.run(f"fwu flash {FLIGHT}", f"Node {FLIGHT} scheduled a swap")
        board.reboot(arguments.reboot_pin)
        check_trial(ground, board, arguments.revision)

        print("CAN FWU: confirm and reboot", flush=True)
        board.run("mcuboot confirm")
        board.run("mcuboot", "confirmed: 1")
        board.reboot(arguments.reboot_pin)
        check_link(ground, board)
        if remote_revision(ground) != arguments.revision:
            raise RuntimeError("The confirmed image did not remain active")
        read_slot(ground, 2, "/readback.bin")
        compare(ground, "/before.bin", "/readback.bin")
        if arguments.stack_check:
            print("CAN FWU: PARAM read and stack margins", flush=True)
            ground.run(f"param list {FLIGHT}", "param_requests_dropped", timeout=30)
            ground.run(f"param get {FLIGHT} param_requests_dropped", f"{FLIGHT}:param_requests_dropped = ")
            stacks = board.run("kernel thread stacks", "unused")
            (output / "stacks.txt").write_text(stacks)
        ground.run("csp interfaces", "CAN addr=16/")
        board.run("csp interfaces", "CAN addr=2/")

    after = can_state(arguments.interface)
    (output / "can-after.json").write_text(json.dumps(after, indent=2))
    if after["linkinfo"]["info_data"]["state"] != "ERROR-ACTIVE":
        raise RuntimeError("CAN left ERROR-ACTIVE during the test")
    print(f"CAN FWU RESULT: PASS logs={output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ground", type=Path, required=True, help="Prebuilt CAN ground executable")
    parser.add_argument("--image", type=Path, required=True, help="Signed candidate image")
    parser.add_argument("--revision", required=True, help="Candidate CSP revision")
    parser.add_argument("--serial", required=True, help="ST-LINK /dev/serial/by-id path")
    parser.add_argument("--board-prompt", default="kfsw:~$ ")
    parser.add_argument("--reboot-pin", default="0000", help="Board reboot PIN (default: 0000)")
    parser.add_argument("--stack-check", action="store_true", help="Read PARAM and stack margins on the confirmed candidate")
    parser.add_argument("--interface", default="can0")
    parser.add_argument("--output", type=Path, required=True, help="New directory for logs and readbacks")
    arguments = parser.parse_args()
    if re.fullmatch(r"[0-9]{1,8}", arguments.reboot_pin) is None:
        parser.error("--reboot-pin must contain 1 to 8 digits")
    if not arguments.serial.startswith("/dev/serial/by-id/"):
        parser.error("--serial must use /dev/serial/by-id/")
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    try:
        run_test(arguments, output)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"CAN FWU RESULT: FAIL {error}\nlogs={output}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
