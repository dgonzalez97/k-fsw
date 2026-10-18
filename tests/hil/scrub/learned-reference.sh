#!/usr/bin/env bash
# Hardware test for the image scrub: a reference is learned, survives a reset,
# and catches a slot that changed underneath it.
#
#   learn slot0 -> sweep ok -> reboot -> sweep ok      (the reference persists)
#   write another image into slot0 -> sweep mismatch   (the point of the service)
#   learn slot0 -> sweep ok                            (the operator accepts it)
#
# Twister covers the state machine. What only hardware can show is that the
# reference outlives a reset and that a real slot, re-read from real flash,
# measures what it measured before.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.
#   KFSW_MCUBOOT_KEY   Signing key. Defaults to ~/.config/kfsw/mcuboot-signing-key.pem

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
signing_key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
build_dir="$KFSW_ROOT/build/hil/scrub/nucleo_l496zg"
profiles="$KFSW_REPO_DIR/config/profiles"
# The same set the firmware update images are built with: the flash layout, the
# application's own slot view, and the update partitions.
overlays="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay"
overlays="$overlays;$profiles/nucleo-mcuboot-fwu.overlay"
imgtool="$KFSW_ROOT/bootloader/mcuboot/scripts/imgtool.py"
work_dir="$(mktemp -d)"
debug_stty=""
capture_pid=""
do_build=1

readonly FLASH_BASE=0x08000000
readonly SLOT0_ADDR=0x08010000
readonly SLOT_SIZE=360448
readonly HEADER_SIZE=0x200
readonly ALIGN=8
readonly VERSION_A=1.0.0
readonly VERSION_B=2.0.0

cleanup()
{
	[[ -n "$capture_pid" ]] && kill "$capture_pid" 2>/dev/null || true
	[[ -n "$debug_stty" && -e "$debug_serial" ]] &&
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	rm -rf "$work_dir"
}
trap cleanup EXIT

failures=0

fail()
{
	printf '  [FAIL] %s\n' "$1" >&2
	failures=$((failures + 1))
}

pass()
{
	printf '  [ok]   %s\n' "$1"
}

abort()
{
	printf '\nSCRUB HIL: ABORTED %s\n' "$1" >&2
	exit 1
}

banner()
{
	printf '\n=== %s ===\n' "$1"
}

send()
{
	printf '%s\r' "$1" >"$debug_serial"
	sleep 0.6
}

marker_offset=0

mark()
{
	marker_offset="$(wc -c <"$work_dir/nucleo.log" 2>/dev/null || echo 0)"
}

since_mark()
{
	tail -c "+$((marker_offset + 1))" "$work_dir/nucleo.log" 2>/dev/null || true
}

wait_for()
{
	local needle="$1" limit="${2:-30}" waited=0

	while [[ "$waited" -lt "$limit" ]]; do
		grep -aq "$needle" <<<"$(since_mark)" && return 0
		sleep 1
		waited=$((waited + 1))
	done
	return 1
}

# One region's row of the scrub table, as the shell prints it: name, bytes,
# reference, measured, state.
region_field()
{
	since_mark | tr -d '\r' | sed 's/\x1b\[[0-9;]*m//g' |
		awk -v region="$1" -v field="$2" \
			'$1 == region && NF == 5 { value = $field } END { print value }'
}

sweep_region()
{
	mark
	send "scrub sweep $1"
	sleep 2
}

sign_image()
{
	python3 "$imgtool" sign --version "$1" --header-size "$HEADER_SIZE" \
		--slot-size "$SLOT_SIZE" --align "$ALIGN" --key "$signing_key" \
		"$build_dir/app/zephyr/zephyr.bin" "$2" >/dev/null 2>&1 ||
		abort "could not sign image version $1"
}

write_slot()
{
	openocd -f "$KFSW_ROOT/zephyr/boards/st/nucleo_l496zg/support/openocd.cfg" \
		-c "init" -c "reset halt" \
		-c "flash write_image erase $2 $1" \
		-c "reset run" -c "shutdown" >>"$work_dir/openocd.log" 2>&1 ||
		abort "could not write $2 to $1"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-build) do_build=0 ;;
	*) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done

[[ -n "$debug_serial" ]] || abort "KFSW_DEBUG_SERIAL is not set"
[[ "$debug_serial" == /dev/serial/by-id/* ]] ||
	abort "the serial device must be a stable /dev/serial/by-id path"
[[ -e "$debug_serial" ]] || abort "$debug_serial is not present"
command -v openocd >/dev/null || abort "openocd is not on PATH"
if [[ ! -r "$signing_key" ]]; then
	printf 'SCRUB HIL RESULT: NOT RUN - no signing key at %s\n' "$signing_key"
	exit 0
fi

if [[ "$do_build" -eq 1 ]]; then
	banner "Build"
	KFSW_SYSBUILD=1 \
		KFSW_MCUBOOT_KEY="$signing_key" \
		KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$overlays" \
		KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 ||
		abort "the MCUboot composition with scrub did not build"
fi
grep -Fq 'CONFIG_KFSW_SCRUB=y' "$build_dir/app/zephyr/.config" ||
	abort "the image under test has no scrub service"

printf 'build: %s\nkey:   %s\n' "$(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD)" \
	"$signing_key"

banner "Mint images"
sign_image "$VERSION_A" "$work_dir/image-a.bin"
sign_image "$VERSION_B" "$work_dir/image-b.bin"
printf 'A=%s  B=%s (same binary, different version, so different bytes)\n' \
	"$VERSION_A" "$VERSION_B"

debug_stty="$(stty -F "$debug_serial" -g)" || abort "cannot read the serial settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
timeout 900s cat "$debug_serial" >"$work_dir/nucleo.log" &
capture_pid=$!

banner "Install bootloader and image A"
openocd -f "$KFSW_ROOT/zephyr/boards/st/nucleo_l496zg/support/openocd.cfg" \
	-c "init" -c "reset halt" -c "flash erase_sector 0 0 last" \
	-c "shutdown" >>"$work_dir/openocd.log" 2>&1 || abort "could not erase flash"
write_slot "$FLASH_BASE" "$build_dir/mcuboot/zephyr/zephyr.bin"
mark
write_slot "$SLOT0_ADDR" "$work_dir/image-a.bin"
wait_for "@READY " 40 || abort "the board did not boot image A"
send "mcuboot confirm"

# Nothing is learned yet, and that must not read as agreement.
banner "Before anything is learned"
mark
send "scrub status"
sleep 2
state="$(region_field slot0 5)"
[[ "$state" == unknown ]] && pass "slot0 reports unknown, not ok" ||
	fail "slot0 reports '$state' before a reference was learned"
measured_a="$(region_field slot0 4)"
[[ -n "$measured_a" && "$measured_a" != 00000000 ]] &&
	pass "slot0 measured $measured_a on silicon" ||
	fail "slot0 was not measured"

banner "Learn and sweep"
mark
send "scrub learn slot0"
sleep 3
reference="$(region_field slot0 3)"
[[ "$reference" == "$measured_a" ]] &&
	pass "the reference is what was on flash ($reference)" ||
	fail "learned '$reference' for a slot measuring '$measured_a'"
sweep_region slot0
state="$(region_field slot0 5)"
[[ "$state" == ok ]] && pass "a learned slot sweeps ok" ||
	fail "a learned slot reports '$state'"

# The reference is a persistent parameter, so it reaches flash on a save.
send "param save"
sleep 2

banner "Across a reset"
mark
send "reboot ${KFSW_REBOOT_PIN:-0000}"
wait_for "@READY " 40 || abort "the board did not come back"
sleep 2
mark
send "scrub status"
sleep 2
reference="$(region_field slot0 3)"
state="$(region_field slot0 5)"
[[ "$reference" == "$measured_a" ]] &&
	pass "the reference outlived the reset ($reference)" ||
	fail "after the reset the reference is '$reference', not '$measured_a'"
[[ "$state" == ok ]] && pass "and the slot still matches it" ||
	fail "after the reset the slot reports '$state'"

banner "A slot that changed underneath"
mark
write_slot "$SLOT0_ADDR" "$work_dir/image-b.bin"
wait_for "@READY " 40 || abort "the board did not boot image B"
send "mcuboot confirm"
sweep_region slot0
state="$(region_field slot0 5)"
measured_b="$(region_field slot0 4)"
[[ "$state" == mismatch ]] &&
	pass "a changed slot reports mismatch ($measured_b against $measured_a)" ||
	fail "a changed slot reports '$state'"
mark
send "scrub status"
sleep 2
mismatches="$(since_mark | tr -d '\r' | sed -n 's/.*mismatches: \([0-9]*\).*/\1/p' | tail -1)"
[[ "${mismatches:-0}" -ge 1 ]] && pass "the crossing is counted ($mismatches)" ||
	fail "the mismatch was not counted"

banner "Accepting the new image"
mark
send "scrub learn slot0"
sleep 3
sweep_region slot0
state="$(region_field slot0 5)"
[[ "$state" == ok ]] && pass "learning again accepts what is there now" ||
	fail "after learning the new image the slot reports '$state'"

banner "Result"
if [[ "$failures" -eq 0 ]]; then
	printf 'SCRUB HIL RESULT: PASS slot0 %s learned, %s caught\n' "$measured_a" "$measured_b"
else
	printf 'SCRUB HIL RESULT: FAIL %u check(s)\n' "$failures"
	printf 'capture: %s\n' "$work_dir/nucleo.log"
	exit 1
fi
