#!/usr/bin/env bash
# A damaged recovery image is refused before the secondary slot is touched,
# and the refusal is visible on the console.
#
# One byte of Golden's body is flipped on the way into the golden partition.
# With one attempt left and two plain resets the flight image must report the
# exhausted budget, refuse the copy, keep running and leave slot 1 as it was.
#
#   tests/hil/golden-corrupt-smoke.sh [evidence-dir]
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
work="${1:-$(mktemp -d /tmp/kfsw-golden-corrupt.XXXXXX)}"
mkdir -p "$work"
trap 'echo "GOLDEN CORRUPT: evidence in $work"' EXIT

fail() { echo "GOLDEN CORRUPT RESULT: FAIL - $*"; exit 1; }
skip() { echo "GOLDEN CORRUPT RESULT: NOT RUN - $*"; exit 0; }

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
golden_offset=0x080C0000
slot1=0x08068000

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

capture() {
	python3 - "$console" "$1" <<'PYTHON'
import sys, time, serial
deadline = time.time() + float(sys.argv[2])
with serial.Serial(sys.argv[1], 115200, timeout=1) as link:
    seen = ""
    while time.time() < deadline:
        seen += link.read(link.in_waiting or 1).decode(errors="replace")
        if "@READY" in seen:
            break
sys.stdout.write(seen)
PYTHON
}

openocd_run() {
	timeout 60 openocd -f "$openocd_cfg" -c "adapter serial $probe" -c "init" "$@" \
		-c "exit" >>"$work/openocd.log" 2>&1 || fail "openocd failed; see $work/openocd.log"
}

# The first 4 KiB of slot 1, read over SWD so the flight image cannot vouch
# for itself.
slot1_head() {
	openocd_run -c "reset halt" -c "dump_image $1 $slot1 4096" -c "reset run"
}

KFSW_SYSBUILD=1 KFSW_PRISTINE=always KFSW_MCUBOOT_KEY="$key" \
	KFSW_BUILD_DIR="$root/../build/hil/golden-corrupt/flight" \
	KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf;$profiles/nucleo-can.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay" \
	KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
	"$root/tools/build.sh" nucleo_l496zg >"$work/flight-build.log" 2>&1 ||
	fail "the flight image did not build; see $work/flight-build.log"
"$root/tools/golden.sh" build >"$work/golden-build.log" 2>&1 ||
	fail "the recovery image did not build; see $work/golden-build.log"
golden="$root/../build/golden/app/zephyr/zephyr.signed.bin"

# Flip one byte a little way into the body, past the 32-byte header.
python3 - "$golden" "$work/golden-damaged.bin" <<'PYTHON'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data[512] ^= 0x01
open(sys.argv[2], "wb").write(data)
PYTHON
openocd_run -c "reset halt" -c "flash erase_address $golden_offset 0x2F000" \
	-c "program $work/golden-damaged.bin verify $golden_offset" -c "reset run"
west flash -d "$root/../build/hil/golden-corrupt/flight" --runner openocd \
	--cmd-pre-init "adapter serial $probe" >"$work/flash.log" 2>&1 ||
	fail "could not flash the flight image; see $work/flash.log"

capture 60 >"$work/first-boot.log" || true
grep -q "@READY" "$work/first-boot.log" || fail "the flight image did not come up"
board "mcuboot confirm;golden status;golden attempts 1" >"$work/prepare.log" || true
grep -q "recovery image: present" "$work/prepare.log" ||
	fail "the damaged image does not even look present; see $work/prepare.log"
grep -q "attempts left: 1 of" "$work/prepare.log" || fail "the budget was not set"
slot1_head "$work/slot1-before.bin"

openocd_run -c "reset run"
capture 60 >"$work/spend.log" || true
grep -q "left=0" "$work/spend.log" || fail "the last attempt was not spent"
openocd_run -c "reset run"
capture 90 >"$work/refuse.log" || true
grep -q "Boot attempts exhausted; recovering" "$work/refuse.log" ||
	fail "the exhausted budget was not reported; see $work/refuse.log"
grep -q "Recovery image failed its own SHA-256" "$work/refuse.log" ||
	fail "the damaged image was not refused; see $work/refuse.log"
grep -q "Recovery image installed" "$work/refuse.log" &&
	fail "the damaged image was installed; see $work/refuse.log"
grep -q "image=application" "$work/refuse.log" ||
	fail "the flight image did not keep running; see $work/refuse.log"
slot1_head "$work/slot1-after.bin"
cmp -s "$work/slot1-before.bin" "$work/slot1-after.bin" ||
	fail "slot 1 changed although the copy was refused"

"$root/tools/golden.sh" flash >"$work/golden-restore.log" 2>&1 ||
	fail "the good recovery image was not written back; see $work/golden-restore.log"
echo "GOLDEN CORRUPT RESULT: PASS - a damaged Golden was refused before any erase"
