#!/usr/bin/env bash
# A CAN frame nobody acknowledges must be counted as a failure, not as a send.
#
# This asks the board itself what it thinks happened, with no node on the bus
# able to acknowledge, and the only acceptable answer is a failure.
#
# What it does not cover: with nobody on the bus the controller rejects the
# frame synchronously, so can_send() returns the error and the completion
# callback never carries it. The callback path, where a frame is accepted into
# a mailbox and fails afterwards, needs a bus that acknowledges and then goes
# wrong, which one board and one adapter cannot produce. Measured, not assumed:
# this case reports the same counters with and without that fix.
#
#   tests/hil/stm32/nucleo-l496zg/can-tx-accounting.sh
#
# The bus must be down or absent. With an adapter acknowledging, the frame does
# leave and the case cannot say anything, so it reports NOT RUN rather than a
# result it did not earn.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../../.." && pwd)"

# shellcheck source=/dev/null
set -a; . "$here/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
ground="${KFSW_CAN_GROUND_NODE:-16}"
console="${KFSW_NUCLEO_CONSOLE:-}"

fail() { echo "CAN TX ACCOUNTING RESULT: FAIL - $*"; exit 1; }
skip() { echo "CAN TX ACCOUNTING RESULT: NOT RUN - $*"; exit 0; }

if [[ -z "$console" ]]; then
	# Never a raw /dev/ttyACM path in a tracked file: it moves between boots.
	console="$(ls /dev/serial/by-id/*STLink* 2>/dev/null | head -1 || true)"
fi
[[ -n "$console" && -e "$console" ]] || skip "no NUCLEO console; set KFSW_NUCLEO_CONSOLE"

if ip link show "$interface" >/dev/null 2>&1; then
	state="$(ip -details link show "$interface" | awk '/can state/ {print $3; exit}')"
	[[ "$state" == "ERROR-ACTIVE" ]] &&
		skip "$interface is acknowledging; take it down for this case"
fi

cd "$root"
# shellcheck source=/dev/null
[[ -f .venv/bin/activate ]] && . .venv/bin/activate

python3 - "$console" "$ground" <<'PYTHON'
import re
import sys
import time

import serial

console, ground = sys.argv[1], sys.argv[2]
COUNTERS = re.compile(r"tx=(\d+) rx=(\d+) txerr=(\d+) rxerr=(\d+) drop=(\d+)")


def counters(link):
    link.reset_input_buffer()
    link.write(b"comms can info\n")
    time.sleep(2.0)
    text = link.read(link.in_waiting or 1).decode(errors="replace")
    found = COUNTERS.search(text)
    if not found:
        print(f"CAN TX ACCOUNTING RESULT: FAIL - the board did not report counters:\n{text}")
        raise SystemExit(1)
    return [int(value) for value in found.groups()]


with serial.Serial(console, 115200, timeout=1) as link:
    time.sleep(1.0)
    before = counters(link)
    link.reset_input_buffer()
    link.write(f"comms can test {ground}\n".encode())
    time.sleep(10.0)
    after = counters(link)

sent, _, failed = after[0] - before[0], 0, after[2] - before[2]
if failed <= 0:
    print(f"CAN TX ACCOUNTING RESULT: FAIL - nothing was counted as a failed send: "
          f"tx+{sent} txerr+{failed}")
    raise SystemExit(1)
if sent != 0:
    print(f"CAN TX ACCOUNTING RESULT: FAIL - a frame nobody acknowledged was counted as sent: "
          f"tx+{sent} txerr+{failed}")
    raise SystemExit(1)
print(f"CAN TX ACCOUNTING RESULT: PASS tx+{sent} txerr+{failed} console={console}")
PYTHON
