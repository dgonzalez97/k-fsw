#!/usr/bin/env bash
# The budget reaching zero installs the recovery image without being told to.
#
# tests/hil/golden-recovery-smoke.sh proves the commanded path: an operator asks
# for the install. This proves the path that matters when nobody is asking,
# which boot_budget.c reaches by calling kfsw_golden_check() once the attempts
# are spent. A revert pending in the bootloader takes precedence over that
# check, so the running image is confirmed first.
#
#   tests/hil/golden-auto-smoke.sh
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
# Recovery evidence is the deliverable, so the captures are kept either way.
# A directory given as the first argument is where they land.
work="${1:-$(mktemp -d /tmp/kfsw-golden-auto.XXXXXX)}"
mkdir -p "$work"
trap 'echo "GOLDEN AUTO: evidence in $work"' EXIT

# shellcheck source=/dev/null
set -a; . "$here/stm32/nucleo-l496zg/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
flight="${KFSW_CAN_FLIGHT_NODE:-2}"
ground="${KFSW_CAN_GROUND_NODE:-17}"

fail() { echo "GOLDEN AUTO RESULT: FAIL - $*"; exit 1; }
skip() { echo "GOLDEN AUTO RESULT: NOT RUN - $*"; exit 0; }

probe="${KFSW_L496_STLINK:-0670FF3732504E3043093407}"
console="$(ls "/dev/serial/by-id/"*"$probe"*-if02 2>/dev/null | head -1)"
[[ -n "$console" ]] || skip "no L496 console; is it plugged in?"
[[ -f "$HOME/.config/kfsw/mcuboot-signing-key.pem" ]] || skip "no MCUboot signing key"

cd "$root"
# shellcheck source=/dev/null
[[ -f "$root/../.venv/bin/activate" ]] && . "$root/../.venv/bin/activate"
openocd_cfg="$root/../zephyr/boards/st/nucleo_l496zg/support/openocd.cfg"

# One exchange on the console: write the commands, return what came back.
board() {
	python3 - "$console" "$1" "${2:-4}" <<'PYTHON'
import sys, time, serial

# Reopening the port right after a capture drops the first writes on a USB
# serial device, so wake the shell and wait for its prompt before commanding.
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    time.sleep(0.5)
    seen = ""
    deadline = time.time() + 20.0
    while time.time() < deadline:
        link.write(b"\n")
        time.sleep(0.5)
        seen += link.read(link.in_waiting or 1).decode(errors="replace")
        if "kfsw" in seen and "$" in seen:
            break
    else:
        raise SystemExit("no shell prompt")
    link.reset_input_buffer()
    for command in sys.argv[2].split(";"):
        link.write(f"{command}\n".encode())
        time.sleep(0.8)
    time.sleep(float(sys.argv[3]))
    sys.stdout.write(link.read(link.in_waiting or 1).decode(errors="replace"))
PYTHON
}

capture_boot() {
	python3 - "$console" "$1" <<'PYTHON'
import sys, time, serial
deadline = time.time() + float(sys.argv[2])
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    seen = ""
    while time.time() < deadline:
        seen += link.read(link.in_waiting or 1).decode(errors="replace")
        if "@READY" in seen:
            break
        time.sleep(0.2)
sys.stdout.write(seen)
PYTHON
}

# The board must be carrying this tree's image, or image=golden cannot appear
# whatever the budget does. golden.sh writes the recovery partition as well.
profiles="$root/config/profiles"
key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
KFSW_SYSBUILD=1 KFSW_PRISTINE=always KFSW_MCUBOOT_KEY="$key" \
	KFSW_BUILD_DIR="$root/../build/hil/golden-auto/flight" \
	KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf;$profiles/nucleo-can.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay" \
	KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
	"$root/tools/build.sh" nucleo_l496zg >"$work/build.log" 2>&1 ||
	fail "the flight image did not build; see $work/build.log"
"$root/tools/golden.sh" build >"$work/golden-build.log" 2>&1 ||
	fail "the recovery image did not build; see $work/golden-build.log"
"$root/tools/golden.sh" flash >"$work/golden-flash.log" 2>&1 ||
	fail "the recovery image was not written; see $work/golden-flash.log"
west flash -d "$root/../build/hil/golden-auto/flight" --runner openocd \
	--cmd-pre-init "adapter serial $probe" >"$work/flash.log" 2>&1 ||
	fail "could not flash the flight image; see $work/flash.log"

# Let the board finish booting before talking to it, or the console still
# carries the recovery upload's output and the reply is never seen.
capture_boot 60 >"$work/first-boot.log" 2>&1 || true
grep -q "@READY" "$work/first-boot.log" ||
	fail "the board did not report readiness after the flash; see $work/first-boot.log"

board "mcuboot confirm;mcuboot" >"$work/confirm.log" 2>&1 || true
grep -q "confirmed: 1" "$work/confirm.log" ||
	fail "the running image is not confirmed, so a revert would mask the budget; see $work/confirm.log"

board "golden status" >"$work/golden.log" 2>&1 || true
grep -qiE "image|present|bytes" "$work/golden.log" ||
	fail "no recovery image reported in the golden partition; see $work/golden.log"

board "golden attempts 1" >"$work/attempts.log" 2>&1 || true
grep -q "attempts left: 1 of" "$work/attempts.log" ||
	fail "the budget was not set to one attempt; see $work/attempts.log"
echo "GOLDEN AUTO: one attempt left, image confirmed, recovery image present"

# An openocd reset is a reset with no commanded note, so it spends the attempt.
reset_board() {
	timeout 60 openocd -f "$openocd_cfg" -c "adapter serial $probe" \
		-c "init; reset run; exit" >"$work/reset-$1.log" 2>&1 ||
		fail "could not reset the board; see $work/reset-$1.log"
}

# Two resets, because kfsw_boot_budget_start() decides on the value it loaded,
# before the decrement: the startup that spends the last attempt still runs the
# application, and the one after it finds nothing left.
reset_board one
capture_boot 60 >"$work/spend.log" 2>&1 || true
grep -q "left=0" "$work/spend.log" ||
	fail "the last attempt was not spent; see $work/spend.log"
echo "GOLDEN AUTO: the last attempt is spent and the application still ran"

reset_board two
capture_boot 120 >"$work/boot.log" 2>&1 || true
grep -q "image=golden" "$work/boot.log" ||
	fail "the node did not come up as the recovery image; see $work/boot.log"
grep -q "Boot attempts exhausted; recovering" "$work/boot.log" ||
	fail "the node did not report exhausting its attempts; see $work/boot.log"
grep -q "Recovery image installed" "$work/boot.log" ||
	fail "the node did not report installing the recovery image; see $work/boot.log"
echo "GOLDEN AUTO RESULT: PASS - an exhausted budget installed the recovery image unasked"
echo "GOLDEN AUTO: the board is now running Golden; restore the application with west flash"
