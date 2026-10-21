#!/usr/bin/env bash
# The image in the golden partition replaces the running one and answers.
#
# An image written straight into the primary slot is confirmed, so the boot
# attempt counter never climbs and the automatic path cannot be reached on a
# bench with a working bootloader; that path is for a node whose revert is not
# rescuing it. What this proves is the mechanism underneath: the recovery image
# is installed on command, the bootloader swaps it in, and the node comes back
# reachable carrying the smaller parameter set of a recovery build.
#
#   tests/hil/golden-recovery-smoke.sh
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
keep=0
trap '[[ "$keep" == 1 ]] && echo "GOLDEN RECOVERY: logs in $work" || rm -rf "$work"' EXIT

# shellcheck source=/dev/null
set -a; . "$here/stm32/nucleo-l496zg/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
flight="${KFSW_CAN_FLIGHT_NODE:-2}"
ground="${KFSW_CAN_GROUND_NODE:-16}"
pin="${KFSW_REBOOT_PIN:-0000}"

fail() { keep=1; echo "GOLDEN RECOVERY RESULT: FAIL - $*"; exit 1; }
skip() { echo "GOLDEN RECOVERY RESULT: NOT RUN - $*"; exit 0; }

probe="${KFSW_L496_STLINK:-0670FF3732504E3043093407}"
console="$(ls "/dev/serial/by-id/"*"$probe"*-if02 2>/dev/null | head -1)"
[[ -n "$console" ]] || skip "no L496 console; is it plugged in?"
ip link show "$interface" >/dev/null 2>&1 || skip "no $interface; is the adapter plugged in?"
state="$(ip -details link show "$interface" | awk '/can state/ {print $3; exit}')"
[[ "$state" == "ERROR-ACTIVE" ]] || skip "$interface is $state; bring it up with can-up.sh"
[[ -f "$HOME/.config/kfsw/mcuboot-signing-key.pem" ]] || skip "no MCUboot signing key"

cd "$root"
# shellcheck source=/dev/null
[[ -f "$root/../.venv/bin/activate" ]] && . "$root/../.venv/bin/activate"

profiles="$root/config/profiles"
key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
ground_exe="$root/../build/k-ground/kfsw-gnd-can-node-$ground/zephyr/zephyr.exe"
[[ -x "$ground_exe" ]] || skip "no ground node at $ground_exe; build it with k-ground"

echo "GOLDEN RECOVERY: node $flight on $interface"

KFSW_SYSBUILD=1 KFSW_PRISTINE=always KFSW_MCUBOOT_KEY="$key" \
	KFSW_BUILD_DIR="$root/../build/hil/golden/flight" \
	KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf;$profiles/nucleo-can.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay" \
	KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
	"$root/tools/build.sh" nucleo_l496zg >"$work/flight-build.log" 2>&1 ||
	fail "the flight image did not build; see $work/flight-build.log"

"$root/tools/golden.sh" build >"$work/golden-build.log" 2>&1 ||
	fail "the recovery image did not build; see $work/golden-build.log"
"$root/tools/golden.sh" flash >"$work/golden-flash.log" 2>&1 ||
	fail "the recovery image was not written; see $work/golden-flash.log"

west flash -d "$root/../build/hil/golden/flight" --runner openocd \
	--cmd-pre-init "adapter serial $probe" >"$work/flash.log" 2>&1 ||
	fail "could not flash the flight image; see $work/flash.log"

wait_ready() {
	# The ground node pings as soon as it starts, so give the board the boot it
	# needs first rather than reading a timeout as an unreachable node.
	python3 - "$console" <<'PYTHON'
import sys, time, serial
deadline = time.time() + 30.0
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    seen = ""
    while time.time() < deadline:
        seen += link.read(link.in_waiting or 1).decode(errors="replace")
        if "@READY" in seen:
            raise SystemExit(0)
        time.sleep(0.2)
raise SystemExit(1)
PYTHON
}

count_parameters() {
	printf '%s\n' "csp ping $flight" "param listall $flight" |
		timeout 150 "$ground_exe" --uart_stdinout "--can-if=$interface" \
			--stop_at=70.0 --no-color >"$1" 2>&1 || true
	grep -c "^ *[0-9]\+  " "$1" || true
}

wait_ready || fail "the board did not report readiness after the flash"
before="$(count_parameters "$work/before.log")"
grep -q "CSP ping $flight: success" "$work/before.log" ||
	fail "the flight image did not answer over CAN; see $work/before.log"
[[ "$before" -gt 0 ]] || fail "the flight image listed no parameters; see $work/before.log"
echo "GOLDEN RECOVERY: the flight image answers and lists $before parameters"

python3 - "$console" "$pin" <<'PYTHON' >"$work/install.log" 2>&1 || true
import sys, time, serial
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    time.sleep(1.5)
    link.reset_input_buffer()
    link.write(f"golden install {sys.argv[2]}\n".encode())
    time.sleep(8.0)
    sys.stdout.write(link.read(link.in_waiting or 1).decode(errors="replace"))
PYTHON
grep -q 'rebooting in' "$work/install.log" ||
	fail "the node did not accept golden install; see $work/install.log"
# The swap is the bootloader's work and takes longer than a restart.
sleep 20

after="$(count_parameters "$work/after.log")"
grep -q "CSP ping $flight: success" "$work/after.log" ||
	fail "the node did not answer over CAN after the swap; see $work/after.log"
[[ "$after" -gt 0 ]] || fail "the node listed no parameters after the swap; see $work/after.log"
if [[ "$after" -ge "$before" ]]; then
	fail "after the swap the node still lists $after parameters against $before before it, so the recovery image is not what is running"
fi

echo "GOLDEN RECOVERY RESULT: PASS parameters ${before} -> ${after}"
