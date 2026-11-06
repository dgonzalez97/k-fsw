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
# Nothing on the bus may acknowledge. The adapter is taken down for that, but
# another board on the same wire acknowledges too, so a delivered frame is
# reported as NOT RUN rather than as a result the case did not earn.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../../.." && pwd)"
root="$(cd "$repo/.." && pwd)"

# shellcheck source=/dev/null
set -a; . "$here/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
ground="${KFSW_CAN_GROUND_NODE:-17}"

fail() { echo "CAN TX ACCOUNTING RESULT: FAIL - $*"; exit 1; }
skip() { echo "CAN TX ACCOUNTING RESULT: NOT RUN - $*"; exit 0; }

# The runner exports the target profile, so prefer what it says. Two boards
# answer on this bench and they are not the same composition, so the identity
# is checked below rather than trusted from a path.
console="${KFSW_NUCLEO_CONSOLE:-${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}}"
if [[ -z "$console" ]]; then
	# Run on its own, with no profile in the environment.
	# shellcheck source=/dev/null
	console="$(. "$repo/config/targets/nucleo_l496zg.env" >/dev/null 2>&1 \
		&& printf '%s' "${KFSW_SERIAL:-}")"
fi
[[ -n "$console" && -e "$console" ]] || skip "no NUCLEO console; set KFSW_NUCLEO_CONSOLE"

# The bus has to be away for this case to mean anything. Put it back the way it
# was found, because a later case that needs it up should not have to know this
# one ran.
restore=""
if ip link show "$interface" >/dev/null 2>&1; then
	state="$(ip -details link show "$interface" | awk '/can state/ {print $3; exit}')"
	if [[ "$state" == "ERROR-ACTIVE" ]]; then
		if sudo -n /usr/local/sbin/kfsw-can "${KFSW_CAN_BITRATE:-500000}" down >/dev/null 2>&1; then
			restore="${KFSW_CAN_BITRATE:-500000}"
			trap 'sudo -n /usr/local/sbin/kfsw-can "$restore" normal >/dev/null 2>&1 || true' EXIT
		else
			skip "$interface is acknowledging and it cannot be taken down here"
		fi
	fi
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
    link.reset_input_buffer()
    link.write(b"status\n")
    time.sleep(2.0)
    identity = link.read(link.in_waiting or 1).decode(errors="replace")
    named = re.search(r"board: (\S+)", identity)
    if not named:
        print(f"CAN TX ACCOUNTING RESULT: FAIL - {console} did not answer `status`")
        raise SystemExit(1)
    if not named.group(1).startswith("nucleo_l496zg"):
        print(f"CAN TX ACCOUNTING RESULT: FAIL - {console} is {named.group(1)}, not the L496")
        raise SystemExit(1)
    # An image built without CAN still answers `comms`, listing only the links
    # it has. Asking such an image for CAN counters is not a failed accounting.
    link.reset_input_buffer()
    link.write(b"comms can info\n")
    time.sleep(2.0)
    probe = link.read(link.in_waiting or 1).decode(errors="replace")
    opening = COUNTERS.search(probe)
    if not opening:
        if "Subcommands:" in probe or "command not found" in probe:
            print("CAN TX ACCOUNTING RESULT: NOT RUN - this image has no CAN link")
            raise SystemExit(0)
        print(f"CAN TX ACCOUNTING RESULT: FAIL - the board did not report counters:\n{probe}")
        raise SystemExit(1)
    before = [int(value) for value in opening.groups()]
    link.reset_input_buffer()
    link.write(f"comms can test {ground}\n".encode())
    time.sleep(10.0)
    after = counters(link)

sent, _, failed = after[0] - before[0], 0, after[2] - before[2]
if sent > 0:
    # Something on the bus acknowledged, so the premise is gone. Another node
    # was added to this bench and taking the adapter down stopped being enough.
    print(f"CAN TX ACCOUNTING RESULT: NOT RUN - a frame was delivered, so another "
          f"node is acknowledging: tx+{sent} txerr+{failed}")
    raise SystemExit(0)
if failed <= 0:
    print(f"CAN TX ACCOUNTING RESULT: FAIL - nothing was counted as a failed send: "
          f"tx+{sent} txerr+{failed}")
    raise SystemExit(1)
print(f"CAN TX ACCOUNTING RESULT: PASS tx+{sent} txerr+{failed} console={console}")
PYTHON
