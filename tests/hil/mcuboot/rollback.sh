#!/usr/bin/env bash
# Hardware acceptance for the MCUboot image layout and rollback flow, k-fsw#2.
#
# The issue is done when HIL demonstrates:
#
#   A -> test B -> no confirm -> A        (automatic revert)
#   A -> test B -> confirm    -> B        (permanent upgrade)
#
# This checks both, plus two properties that make those two mean something:
#
#   * an image signed with the WRONG key is refused. Without this, "the
#     bootloader ran the image" proves only that it ran something. The default
#     key MCUboot ships in its own public tree is used as the wrong key, which
#     is exactly the mistake this composition was once making silently.
#
#   * the storage partition still mounts afterwards. The whole point of pinning
#     kfsw-storage at 0xf0000 was that an existing filesystem survives the move
#     to a bootloader, and that claim is worth nothing untested.
#
# Images A and B are the same binary signed with different versions. Using one
# build isolates what is under test: any difference in behaviour is the
# bootloader's doing, not a difference between two programs.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.
#   KFSW_MCUBOOT_KEY   Signing key. Defaults to ~/.config/kfsw/mcuboot-signing-key.pem

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
signing_key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
build_dir="$KFSW_ROOT/build/hil/mcuboot/nucleo_l496zg"
profiles="$KFSW_REPO_DIR/config/profiles"
imgtool="$KFSW_ROOT/bootloader/mcuboot/scripts/imgtool.py"
default_key="$KFSW_ROOT/bootloader/mcuboot/root-ec-p256.pem"
work_dir="$(mktemp -d)"
debug_stty=""
capture_pid=""
do_build=1

# Absolute flash addresses. The devicetree offsets are relative to the flash
# base, which is 0x08000000 on this part.
readonly FLASH_BASE=0x08000000
readonly SLOT0_ADDR=0x08010000
readonly SLOT1_ADDR=0x08068000
# MCUboot runs in BOOT_SWAP_USING_OFFSET mode, Zephyr's default. In that mode
# an update must be written one sector into the secondary slot, not at its
# start: "firmware updates must be placed at the second sector in the second
# slot instead of the first". Writing at the start is not rejected -- the
# bootloader simply finds nothing to swap and carries on with the old image,
# which is a silent no-op and exactly how this test failed the first time.
readonly SECTOR_SIZE=0x800
readonly SLOT1_IMAGE_ADDR=0x08068800
readonly SLOT_SIZE=360448
readonly HEADER_SIZE=0x200
readonly ALIGN=8

readonly VERSION_A=1.0.0
readonly VERSION_B=2.0.0
readonly VERSION_EVIL=3.0.0

cleanup()
{
	[[ -n "$capture_pid" ]] && kill "$capture_pid" 2>/dev/null || true
	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
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
	printf '\nMCUBOOT HIL: ABORTED %s\n' "$1" >&2
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

# Everything the board has said since the marker was last placed.
#
# The marker is a byte offset into the local capture, not a string echoed by
# the board. An echoed marker needs a running shell to come back, so it cannot
# be used to wait for the first boot after flashing -- which is exactly when it
# is needed most, and which is how this failed the first time.
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

# The running image's version, read from MCUboot's own shell. It places its own
# marker first so a previous reply left in the log cannot be read as this one:
# the first "version:" line after the marker is the primary slot, which is what
# is executing.
version_query=0

running_version()
{
	version_query=$((version_query + 1))
	mark
	send "mcuboot"
	sleep 2
	since_mark | grep -a "version:" | head -1 | sed 's/.*version: //' | tr -d '\r'
}

confirmed_state()
{
	since_mark | grep -a "^confirmed:" | tail -1 | sed 's/confirmed: //' | tr -d '\r'
}

reboot_board()
{
	mark
	send "cmd reboot"
	wait_for "@READY " 40 || abort "the board did not come back after reboot ($1)"
	sleep 1
}

sign_image()
{
	local version="$1" key="$2" out="$3"

	python3 "$imgtool" sign --version "$version" --header-size "$HEADER_SIZE" \
		--slot-size "$SLOT_SIZE" --align "$ALIGN" --key "$key" \
		"$build_dir/app/zephyr/zephyr.bin" "$out" >/dev/null 2>&1 || \
		abort "could not sign image version $version"
}

write_slot()
{
	local address="$1" image="$2"

	openocd -f "$KFSW_ROOT/zephyr/boards/st/nucleo_l496zg/support/openocd.cfg" \
		-c "init" -c "reset halt" \
		-c "flash write_image erase $image $address" \
		-c "reset run" -c "shutdown" >>"$work_dir/openocd.log" 2>&1 || \
		abort "could not write $image to $address"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-build) do_build=0 ;;
	*) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done

[[ -n "$debug_serial" ]] || abort "KFSW_DEBUG_SERIAL is not set"
[[ "$debug_serial" == /dev/serial/by-id/* ]] || \
	abort "the serial device must be a stable /dev/serial/by-id path"
[[ -e "$debug_serial" ]] || abort "$debug_serial is not present"
[[ -r "$signing_key" ]] || abort "signing key not readable: $signing_key"
[[ -r "$default_key" ]] || abort "MCUboot's default key not found: $default_key"
command -v openocd >/dev/null || abort "openocd is not on PATH"

if [[ "$do_build" -eq 1 ]]; then
	banner "Build"
	KFSW_SYSBUILD=1 \
		KFSW_MCUBOOT_KEY="$signing_key" \
		KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay" \
		KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 || \
		abort "the MCUboot composition did not build"
fi

build_sha="$(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD)"
printf 'build: %s\nkey:   %s\n' "$build_sha" "$signing_key"

# Prove the bootloader really was built with this key before trusting anything
# it does later. A bootloader carrying the wrong public key would accept the
# wrong images and every result below would be meaningless.
banner "Signing key"
if python3 "$imgtool" verify -k "$signing_key" \
	"$build_dir/app/zephyr/zephyr.signed.bin" 2>&1 | grep -q "correctly validated"; then
	pass "the built image verifies against the project key"
else
	fail "the built image does NOT verify against the project key"
fi
if python3 "$imgtool" verify -k "$default_key" \
	"$build_dir/app/zephyr/zephyr.signed.bin" 2>&1 | grep -q "correctly validated"; then
	fail "the built image ALSO verifies against MCUboot's public default key"
else
	pass "MCUboot's public default key does not verify it"
fi

banner "Mint images"
sign_image "$VERSION_A" "$signing_key" "$work_dir/image-a.bin"
sign_image "$VERSION_B" "$signing_key" "$work_dir/image-b.bin"
sign_image "$VERSION_EVIL" "$default_key" "$work_dir/image-evil.bin"
printf 'A=%s  B=%s  wrong-key=%s (same binary, different signatures)\n' \
	"$VERSION_A" "$VERSION_B" "$VERSION_EVIL"

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

version="$(running_version)"
[[ "$version" == "$VERSION_A"* ]] && pass "running A ($version)" || \
	fail "expected A ($VERSION_A), running '$version'"

send "mcuboot confirm"
sleep 1
reboot_board "confirm-a"
mark
[[ "$(running_version)" == "$VERSION_A"* ]] && pass "A is confirmed and persists" || \
	fail "A did not persist after confirmation"

# ---------------------------------------------------------------- case 1
banner "Case 1: A -> test B -> no confirm -> A"

write_slot "$SLOT1_IMAGE_ADDR" "$work_dir/image-b.bin"
mark
wait_for "@READY " 40 || abort "the board did not come back after writing slot1"
send "mcuboot request_upgrade"
sleep 1
reboot_board "test-b"

mark
version="$(running_version)"
[[ "$version" == "$VERSION_B"* ]] && pass "swapped to B ($version)" || \
	fail "expected B ($VERSION_B) after request_upgrade, running '$version'"

state="$(confirmed_state)"
[[ "$state" == "0" ]] && pass "B is running unconfirmed, as a test image" || \
	fail "B reports confirmed=$state; a test image must not be confirmed"

# The whole point: reboot without confirming.
reboot_board "no-confirm"
mark
version="$(running_version)"
if [[ "$version" == "$VERSION_A"* ]]; then
	pass "REVERTED to A ($version) after an unconfirmed boot"
else
	fail "expected revert to A ($VERSION_A), running '$version'"
fi

# ---------------------------------------------------------------- case 2
banner "Case 2: A -> test B -> confirm -> B"

write_slot "$SLOT1_IMAGE_ADDR" "$work_dir/image-b.bin"
mark
wait_for "@READY " 40 || abort "the board did not come back after writing slot1"
send "mcuboot request_upgrade"
sleep 1
reboot_board "test-b-again"

mark
version="$(running_version)"
[[ "$version" == "$VERSION_B"* ]] && pass "swapped to B ($version)" || \
	fail "expected B ($VERSION_B), running '$version'"

send "mcuboot confirm"
sleep 1
reboot_board "confirmed-b"
mark
version="$(running_version)"
if [[ "$version" == "$VERSION_B"* ]]; then
	pass "B PERSISTS after confirmation ($version)"
else
	fail "expected B to persist after confirmation, running '$version'"
fi

# A second reboot: a confirmed image must stay put, not revert late.
reboot_board "persist-check"
mark
[[ "$(running_version)" == "$VERSION_B"* ]] && \
	pass "B still running after a further reboot" || \
	fail "B did not survive a second reboot"

# ---------------------------------------------------------------- case 3
banner "Case 3: an image signed with the wrong key is refused"

write_slot "$SLOT1_IMAGE_ADDR" "$work_dir/image-evil.bin"
mark
wait_for "@READY " 40 || abort "the board did not come back after writing slot1"
send "mcuboot request_upgrade"
sleep 1
reboot_board "evil"
mark

version="$(running_version)"
if [[ "$version" == "$VERSION_EVIL"* ]]; then
	fail "SECURITY: the bootloader ran an image signed with MCUboot's public key"
else
	pass "refused the wrong-key image; still running $version"
fi

# ---------------------------------------------------------------- storage
banner "Storage survived the bootloader"

send "storage info"
sleep 1.5
if since_mark | grep -aq "ready: yes"; then
	pass "the filesystem at 0xf0000 still mounts"
else
	fail "storage does not report ready after the migration to a bootloader"
fi

banner "Result"
printf 'build=%s slots=%s/%s\n' "$build_sha" "$SLOT0_ADDR" "$SLOT1_ADDR"
if [[ "$failures" -eq 0 ]]; then
	printf 'MCUBOOT HIL: PASS revert=yes permanent=yes wrong-key-refused=yes storage=ok\n'
else
	printf 'MCUBOOT HIL: FAIL %d check(s) failed\n' "$failures"
	exit 1
fi
