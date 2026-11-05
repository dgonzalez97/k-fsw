#!/usr/bin/env bash
# An image that spins at the top of main() still ends in a known image.
#
#   tests/hil/golden-hang-smoke.sh trial       the bootloader's watchdog resets
#                                              it and the trial is reverted
#   tests/hil/golden-hang-smoke.sh confirmed   made permanent, it spends the
#                                              boot budget and Golden comes up
#
# The hang image (tests/hil/golden/hang) carries the real boot budget and
# Golden installer and arms no watchdog of its own, so the reset can only come
# from the bootloader's.
set -Eeuo pipefail

mode="${1:-}"
[[ "$mode" == trial || "$mode" == confirmed ]] || {
	echo "usage: $(basename "$0") trial|confirmed [evidence-dir]" >&2
	exit 2
}

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
work="${2:-$(mktemp -d /tmp/kfsw-golden-hang.XXXXXX)}"
mkdir -p "$work"
trap 'echo "GOLDEN HANG: evidence in $work"' EXIT

fail() { echo "GOLDEN HANG RESULT: FAIL - $*"; exit 1; }
skip() { echo "GOLDEN HANG RESULT: NOT RUN - $*"; exit 0; }

probe="${KFSW_L496_STLINK:-0670FF3732504E3043093407}"
console="$(ls "/dev/serial/by-id/"*"$probe"*-if02 2>/dev/null | head -1)"
[[ -n "$console" ]] || skip "no L496 console; is it plugged in?"
key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
[[ -r "$key" ]] || skip "no MCUboot signing key at $key"

cd "$root"
# shellcheck source=/dev/null
[[ -f "$root/../.venv/bin/activate" ]] && . "$root/../.venv/bin/activate"
openocd_cfg="$root/../zephyr/boards/st/nucleo_l496zg/support/openocd.cfg"
profiles="$root/config/profiles"
# Slot 1 starts at 0x68000; swap using offset expects the image one 2 KiB
# sector in.
slot1_image=0x08068800
budget="${KFSW_GOLDEN_HANG_ATTEMPTS:-3}"
# The bootloader's watchdog, a margin for the bootloader, and the Golden copy.
reset_s=25

board() {
	python3 - "$console" "$1" <<'PYTHON'
import sys, time, serial
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
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
    time.sleep(3)
    sys.stdout.write(link.read(link.in_waiting or 1).decode(errors="replace"))
PYTHON
}

# Everything the console prints until the marker or the deadline.
capture() {
	python3 - "$console" "$1" "$2" <<'PYTHON'
import sys, time, serial
deadline = time.time() + float(sys.argv[2])
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    seen = ""
    while time.time() < deadline:
        seen += link.read(link.in_waiting or 1).decode(errors="replace")
        if sys.argv[3] in seen:
            break
sys.stdout.write(seen)
PYTHON
}

openocd_run() {
	timeout 60 openocd -f "$openocd_cfg" -c "adapter serial $probe" -c "init" "$@" \
		-c "exit" >>"$work/openocd.log" 2>&1 || fail "openocd failed; see $work/openocd.log"
}

KFSW_SYSBUILD=1 KFSW_PRISTINE=always KFSW_MCUBOOT_KEY="$key" \
	KFSW_BUILD_DIR="$root/../build/hil/golden-hang/flight" \
	KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf;$profiles/nucleo-can.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay" \
	KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
	"$root/tools/build.sh" nucleo_l496zg >"$work/flight-build.log" 2>&1 ||
	fail "the flight image did not build; see $work/flight-build.log"
west build -p always -b nucleo_l496zg -d "$root/../build/hil/golden-hang/hang" \
	"$here/golden/hang" -- \
	-DEXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
	-DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="\"$key\"" >"$work/hang-build.log" 2>&1 ||
	fail "the hang image did not build; see $work/hang-build.log"
hang="$root/../build/hil/golden-hang/hang/zephyr/zephyr.signed.bin"
"$root/tools/golden.sh" build >"$work/golden-build.log" 2>&1 ||
	fail "the recovery image did not build; see $work/golden-build.log"
"$root/tools/golden.sh" flash >"$work/golden-flash.log" 2>&1 ||
	fail "the recovery image was not written; see $work/golden-flash.log"
west flash -d "$root/../build/hil/golden-hang/flight" --runner openocd \
	--cmd-pre-init "adapter serial $probe" >"$work/flash.log" 2>&1 ||
	fail "could not flash the flight image; see $work/flash.log"

capture 60 "@READY" >"$work/first-boot.log" || true
grep -q "@READY" "$work/first-boot.log" || fail "the flight image did not come up"
board "mcuboot confirm;golden attempts $budget;mcuboot" >"$work/prepare.log" || true
grep -q "confirmed: 1" "$work/prepare.log" || fail "the flight image is not confirmed"
grep -q "attempts left: $budget of" "$work/prepare.log" || fail "the budget was not set"

# Written while the flight image runs, then offered to the bootloader by the
# flight image's own MCUboot shell, the way an upload would be.
openocd_run -c "reset halt" -c "flash erase_address 0x08068000 0x58000" \
	-c "program $hang verify $slot1_image" -c "reset run"
capture 60 "@READY" >"$work/second-boot.log" || true
request="mcuboot request_upgrade"
[[ "$mode" == confirmed ]] && request="mcuboot request_upgrade permanent"
board "$request;kernel reboot cold" >"$work/request.log" || true

if [[ "$mode" == trial ]]; then
	capture "$((reset_s + 60))" "@READY" >"$work/revert.log" || true
	grep -q "image=application" "$work/revert.log" ||
		fail "no application came back after the hang; see $work/revert.log"
	grep -q "ended by the watchdog" "$work/revert.log" ||
		fail "the reset that ended the hang was not the watchdog; see $work/revert.log"
	board "mcuboot" >"$work/after.log" || true
	grep -q "confirmed: 1" "$work/after.log" ||
		fail "the image that came back is not the confirmed flight image"
	echo "GOLDEN HANG RESULT: PASS - the bootloader's watchdog ended the hang and the trial was reverted"
	exit 0
fi

# Each hang costs one watchdog period; the startup after the last one copies
# Golden. N attempts, the startup that finds none, and Golden's own start.
capture "$(((budget + 3) * reset_s + 60))" "image=golden" >"$work/recovery.log" || true
grep -q "image=golden" "$work/recovery.log" ||
	fail "Golden did not come up after the hangs; see $work/recovery.log"
echo "GOLDEN HANG RESULT: PASS - a permanent image that hangs in main() ended in Golden"
echo "GOLDEN HANG: the board is running Golden; restore the application with west flash"
