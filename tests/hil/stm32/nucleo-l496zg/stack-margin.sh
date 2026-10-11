#!/usr/bin/env bash
# How much stack every thread on the board has left, read from the board.
#
# The host build measures pthread stacks and cannot qualify these, so this is
# the only place the number means anything. It is a published figure, so it
# fails rather than reports when the margin is thin or when a stack could not
# be read: a margin for part of the system is not a margin for the system.
#
#   tests/hil/stm32/nucleo-l496zg/stack-margin.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../../.." && pwd)"
console="${KFSW_NUCLEO_CONSOLE:-}"
# Bytes every thread must keep free. A deep error path or an interrupt taken on
# a thread's own stack is what this room is for.
floor="${KFSW_STACK_FLOOR_BYTES:-128}"

fail() { echo "STACK MARGIN RESULT: FAIL - $*"; exit 1; }
skip() { echo "STACK MARGIN RESULT: NOT RUN - $*"; exit 0; }

if [[ -z "$console" ]]; then
	console="$(ls /dev/serial/by-id/*STLink* 2>/dev/null | head -1 || true)"
fi
[[ -n "$console" && -e "$console" ]] || skip "no NUCLEO console; set KFSW_NUCLEO_CONSOLE"

cd "$root"
# shellcheck source=/dev/null
[[ -f .venv/bin/activate ]] && . .venv/bin/activate

python3 - "$console" "$floor" <<'PYTHON'
import re
import sys
import time

import serial

console, floor = sys.argv[1], int(sys.argv[2])


def ask(link, command, wait=3.0):
    link.reset_input_buffer()
    link.write((command + "\n").encode())
    time.sleep(wait)
    return link.read(link.in_waiting or 1).decode(errors="replace")


with serial.Serial(console, 115200, timeout=1) as link:
    time.sleep(1.0)
    if "command not found" in ask(link, "resmon show", 2.0):
        print("STACK MARGIN RESULT: NOT RUN - this image has no resource monitor")
        raise SystemExit(0)
    # Sweep now rather than trusting whatever the periodic one last saw.
    ask(link, "resmon sample", 2.0)
    report = ask(link, "resmon show")

numbers = {}
for key, pattern in (
    ("threads", r"threads: (\d+) read, (\d+) unmeasured"),
    ("worst", r"worst_used: (\d+)% on (\S+)"),
    ("tightest", r"tightest: (\d+) of (\d+) bytes on (\S+)"),
):
    found = re.search(pattern, report)
    if not found:
        print(f"STACK MARGIN RESULT: FAIL - the board did not report {key}:\n{report}")
        raise SystemExit(1)
    numbers[key] = found.groups()

read, unmeasured = (int(value) for value in numbers["threads"])
used_percent, busiest = int(numbers["worst"][0]), numbers["worst"][1]
free, size, tightest = int(numbers["tightest"][0]), int(numbers["tightest"][1]), numbers["tightest"][2]

if read < 1:
    print("STACK MARGIN RESULT: FAIL - no thread was read")
    raise SystemExit(1)
if unmeasured > 0:
    print(f"STACK MARGIN RESULT: FAIL - {unmeasured} stacks could not be read, so this "
          f"margin covers part of the system only")
    raise SystemExit(1)
if free < floor:
    print(f"STACK MARGIN RESULT: FAIL - {tightest} has {free} bytes free, under the "
          f"{floor} byte floor")
    raise SystemExit(1)

print(f"STACK MARGIN: {read} threads, 0 unmeasured")
print(f"STACK MARGIN: busiest {busiest} at {used_percent}%")
print(f"STACK MARGIN: tightest {tightest} with {free} of {size} bytes free")
print(f"STACK MARGIN RESULT: PASS floor={floor} tightest={free}")
PYTHON
