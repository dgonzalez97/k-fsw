#!/usr/bin/env bash
# Firmware update over the radio, end to end.
#
# The claim being tested is not that bytes crossed the link. It is that a
# different image is running afterwards, and the node says so itself.
#
# Two images are built from the same tree with different CSP revision strings.
# The node starts on one. Ground uploads the other over UHF, tells the node to
# flash it, and the node reboots. The proof is that ground asks the node who it
# is, over the radio, and gets a different answer than before.
#
# A revision string is used rather than a checksum of the slot because it is
# what the running image reports about itself. Reading back the slot would show
# what was written; asking the node shows what is executing.
#
# This takes a long time. An application image is around 154 KB and the link
# carries a few hundred bytes a second, with a reply after every block, so the
# upload alone is tens of minutes. It is an acceptance, not a smoke test.
#
# Required environment:
#   KGROUND_HOLYBRO_DEVICE  USB side of the radio pair, by-id path only.
#   KFSW_DEBUG_SERIAL       NUCLEO ST-LINK virtual COM port, by-id path only.

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

HOLYBRO_DIR="$KFSW_REPO_DIR/tests/hil/radio-uhf/holybro"
PROFILES="$KFSW_REPO_DIR/config/profiles"

radio_device="${KGROUND_HOLYBRO_DEVICE:-}"
debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
signing_key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"
radio_baud="${KGROUND_HOLYBRO_BAUD:-57600}"

work_dir="$(mktemp -d /tmp/kfsw-fwu-radio.XXXXXX)"
station_dir="$work_dir/station"
ground_pid=""
bridge_pid=""
debug_capture_pid=""
debug_stty=""
radio_stty=""

readonly REVISION_BEFORE="fwu-before"
readonly REVISION_AFTER="fwu-after"
readonly FLIGHT_NODE=2
readonly GROUND_NODE=16
readonly SLOT1_IMAGE_ADDR=0x08068800
readonly FLASH_BASE=0x08000000

cleanup()
{
	local status=$?

	for pid in "$ground_pid" "$bridge_pid" "$debug_capture_pid"; do
		[[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
	done

	# A failed run is the one whose logs are worth having. Removing them on
	# the way out leaves nothing to diagnose from.
	if [[ "$status" -ne 0 ]]; then
		local kept="${KFSW_HIL_LOG_DIR:-$KFSW_ROOT/build/hil/fwu/failed}"

		mkdir -p "$kept" 2>/dev/null || true
		cp "$work_dir"/*.log "$kept/" 2>/dev/null || true
		printf 'logs kept in %s\n' "$kept" >&2
	fi
	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	[[ -n "$radio_stty" && -e "$radio_device" ]] && \
		stty -F "$radio_device" "$radio_stty" 2>/dev/null || true
	rm -rf "$work_dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

fail()
{
	printf '\nFWU RADIO RESULT: FAIL %s\n' "$1" >&2
	exit 1
}

banner()
{
	printf '\n=== %s ===\n' "$1"
}

wait_for_output()
{
	local file="$1" expected="$2" pid="$3" limit="${4:-600}"
	local waited=0

	while [[ "$waited" -lt "$limit" ]]; do
		grep -aFq "$expected" "$file" 2>/dev/null && return 0
		[[ -n "$pid" ]] && { kill -0 "$pid" 2>/dev/null || return 1; }
		sleep 1
		waited=$((waited + 1))
	done
	return 1
}

# Wait until the flight node answers at all. After a reboot the radio pair and
# the routing need a moment, and an identity request sent into that gap simply
# times out -- which looks the same as a node running the wrong image.
wait_for_link()
{
	local marker="$1" attempt

	for attempt in $(seq 1 20); do
		printf '%s\n' "csp ping $FLIGHT_NODE" >&3
		sleep 5
		if sed -n "/$marker/,\$p" "$work_dir/ground.log" 2>/dev/null |
			grep -aq "CSP ping $FLIGHT_NODE: success"; then
			return 0
		fi
	done
	return 1
}

# Ground's identity view of the flight node, read over the radio. Retried,
# because a single timed-out request is not evidence of anything.
remote_revision()
{
	local marker="$1" attempt revision

	for attempt in $(seq 1 6); do
		printf '%s\n' "csp ident $FLIGHT_NODE" >&3
		sleep 10
		revision="$(sed -n "/$marker/,\$p" "$work_dir/ground.log" 2>/dev/null |
			sed -n 's/^revision: //p' | tail -1 | tr -d '\r')"
		if [[ -n "$revision" ]]; then
			printf '%s' "$revision"
			return 0
		fi
	done

	return 1
}

build_image()
{
	local revision="$1" build_dir="$2"
	local revision_conf="$work_dir/revision-$revision.conf"

	# The only difference between the two images. Building both from the
	# same tree keeps the test about the update rather than about a
	# difference between two programs.
	printf 'CONFIG_KFSW_CSP_REVISION="%s"\n' "$revision" >"$revision_conf"

	KFSW_SYSBUILD=1 \
		KFSW_MCUBOOT_KEY="$signing_key" \
		KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$PROFILES/nucleo-mcuboot.conf;$PROFILES/nucleo-mcuboot-fwu.conf;$PROFILES/nucleo-mcuboot-fwu-lite.conf;$HOLYBRO_DIR/nucleo_l496zg.conf;$revision_conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$PROFILES/nucleo-mcuboot-flash.overlay;$PROFILES/nucleo-mcuboot.overlay;$PROFILES/nucleo-mcuboot-fwu.overlay;$HOLYBRO_DIR/nucleo_l496zg.overlay" \
		KFSW_MCUBOOT_DTC_OVERLAY_FILE="$PROFILES/nucleo-mcuboot-flash.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build-$revision.log" 2>&1 || \
		fail "the $revision image did not build"
}

[[ -n "$radio_device" ]] || fail "KGROUND_HOLYBRO_DEVICE is not set"
[[ -n "$debug_serial" ]] || fail "KFSW_DEBUG_SERIAL is not set"
[[ "$radio_device" == /dev/serial/by-id/* ]] || \
	fail "the radio device must be a stable /dev/serial/by-id path"
[[ "$debug_serial" == /dev/serial/by-id/* ]] || \
	fail "the serial device must be a stable /dev/serial/by-id path"
[[ -e "$radio_device" && -e "$debug_serial" ]] || fail "a configured device is missing"
[[ -r "$signing_key" ]] || fail "signing key not readable: $signing_key"
command -v socat >/dev/null || fail "socat is required"
command -v openocd >/dev/null || fail "openocd is required"

banner "Build both images"
build_image "$REVISION_BEFORE" "$KFSW_ROOT/build/hil/fwu/before"
build_image "$REVISION_AFTER" "$KFSW_ROOT/build/hil/fwu/after"

before_image="$KFSW_ROOT/build/hil/fwu/before/app/zephyr/zephyr.signed.bin"
after_image="$KFSW_ROOT/build/hil/fwu/after/app/zephyr/zephyr.signed.bin"
[[ -r "$before_image" && -r "$after_image" ]] || fail "a signed image is missing"

after_size="$(stat -c %s "$after_image")"
after_crc="$(python3 -c "
import zlib, pathlib, sys
print(f'{zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes()) & 0xFFFFFFFF:08x}')
" "$after_image")"
printf 'uploading %s bytes, crc32=%s\n' "$after_size" "$after_crc"

banner "Install the bootloader and the first image"
openocd -f "$KFSW_ROOT/zephyr/boards/st/nucleo_l496zg/support/openocd.cfg" \
	-c "init" -c "reset halt" -c "flash erase_sector 0 0 last" \
	-c "shutdown" >>"$work_dir/openocd.log" 2>&1 || fail "could not erase flash"
openocd -f "$KFSW_ROOT/zephyr/boards/st/nucleo_l496zg/support/openocd.cfg" \
	-c "init" -c "reset halt" \
	-c "flash write_image erase $KFSW_ROOT/build/hil/fwu/before/mcuboot/zephyr/zephyr.bin $FLASH_BASE" \
	-c "flash write_image erase $before_image 0x08010000" \
	-c "reset run" -c "shutdown" >>"$work_dir/openocd.log" 2>&1 || \
	fail "could not install the bootloader and first image"

debug_stty="$(stty -F "$debug_serial" -g)" || fail "cannot read the serial settings"
radio_stty="$(stty -F "$radio_device" -g)" || fail "cannot read the radio settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
stty -F "$radio_device" "$radio_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 4000s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" 60 || \
	fail "the node did not boot the first image"

# The image was written straight to the primary slot, so the bootloader treats
# it as already confirmed. Confirming explicitly gives the revert a destination
# if the uploaded image never confirms itself.
printf '%s\r' 'mcuboot confirm' >"$debug_serial"
sleep 2

banner "Start ground on the radio"
mkdir -p "$station_dir/nodes"
cp "$KFSW_REPO_DIR/ground-station/nodes/kfsw-gnd-uhf.env" "$station_dir/nodes/"
{
	printf 'KFSW_EXTRA_KCONFIG=%s\n' "'CONFIG_KFSW_FWU=y
CONFIG_KFSW_FWU_MCUBOOT=n
CONFIG_KFSW_FWU_LITE=y
CONFIG_KFSW_FWU_LITE_CSP=y
CONFIG_KFSW_FWU_LITE_RDP=n
CONFIG_KFSW_FWU_LITE_TIMEOUT_MS=20000
CONFIG_KFSW_FWU_LITE_BLOCK_RETRIES=8'"
} >>"$station_dir/nodes/kfsw-gnd-uhf.env"

KGROUND_STATION_DIR="$station_dir" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/k-ground.overlay" \
	"$KFSW_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf --peer "$FLIGHT_NODE" \
	>"$work_dir/ground-build.log" 2>&1 || fail "the ground station did not build"

ground_executable="$KFSW_ROOT/build/k-ground/node-$GROUND_NODE/zephyr/zephyr.exe"
[[ -x "$ground_executable" ]] || fail "the ground executable is missing"

mkfifo "$work_dir/ground.in"
exec 3<>"$work_dir/ground.in"
"$ground_executable" --uart_stdinout --device_id="$GROUND_NODE" --no-color \
	-flash="$work_dir/ground-flash.bin" \
	<&3 >"$work_dir/ground.log" 2>&1 &
ground_pid=$!

wait_for_output "$work_dir/ground.log" "@READY " "$ground_pid" 60 || \
	fail "ground did not report readiness"
wait_for_output "$work_dir/ground.log" "uart_1 connected to pseudotty: " \
	"$ground_pid" 60 || fail "ground did not expose its KISS pseudo-terminal"

ground_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/ground.log" | head -1)"
socat -d -d "$ground_pty,raw,echo=0,b${radio_baud}" \
	"$radio_device,raw,echo=0,b${radio_baud}" >"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/socat.log" "starting data transfer loop" "$bridge_pid" 30 || \
	fail "the bridge to the radio did not start"

printf '%s\n' "csp ping $FLIGHT_NODE" >&3
wait_for_output "$work_dir/ground.log" "CSP ping $FLIGHT_NODE: success" "$ground_pid" 60 || \
	fail "ground cannot reach the flight node over the radio"

banner "Who is running, before"
printf '\n@@MARK before\n' >&3
wait_for_link "@@MARK before" || fail "the flight node does not answer before the update"
revision_before="$(remote_revision "@@MARK before")" || \
	fail "the flight node did not report its identity before the update"
[[ "$revision_before" == "$REVISION_BEFORE" ]] || \
	fail "expected revision '$REVISION_BEFORE' before the update, got '$revision_before'"
printf 'node %s reports revision %s\n' "$FLIGHT_NODE" "$revision_before"

banner "Upload the second image over the radio"
printf 'this is tens of minutes at %s baud\n' "$radio_baud"
printf '%s\n' "fwu send $FLIGHT_NODE $after_image" >&3

# Waiting for success alone turns any failure into the full timeout, which on a
# transfer this long is an hour of watching nothing happen.
upload_done=0
for _ in $(seq 1 3600); do
	if grep -aq "Image accepted and verified" "$work_dir/ground.log"; then
		upload_done=1
		break
	fi
	if grep -aq "firmware upload failed" "$work_dir/ground.log"; then
		printf '%s\n' "$(grep -a "refused\|firmware upload failed" \
			"$work_dir/ground.log" | tail -2)" >&2
		fail "the flight node rejected the upload"
	fi
	kill -0 "$ground_pid" 2>/dev/null || fail "the ground station stopped"
	sleep 1
done
[[ "$upload_done" -eq 1 ]] || fail "the upload did not finish in time"

resent="$(sed -n 's/.*verified; \([0-9]*\) block(s) resent.*/\1/p' "$work_dir/ground.log" | tail -1)"
printf 'accepted after %s resent block(s)\n' "${resent:-0}"

banner "Flash and reboot"
printf '%s\n' "fwu flash $FLIGHT_NODE" >&3
wait_for_output "$work_dir/ground.log" "scheduled a swap" "$ground_pid" 120 || \
	fail "the node did not schedule a swap"

printf '%s\r' 'cmd reboot' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" 180 || \
	fail "the node did not come back after the swap"
sleep 5

banner "Who is running, after"
printf '\n@@MARK after\n' >&3
wait_for_link "@@MARK after" || \
	fail "the flight node does not answer after the update; it may not have booted"
revision_after="$(remote_revision "@@MARK after")" || \
	fail "the flight node did not report its identity after the update"
if [[ "$revision_after" != "$REVISION_AFTER" ]]; then
	fail "expected revision '$REVISION_AFTER' after the update, got '$revision_after'"
fi
printf 'node %s reports revision %s\n' "$FLIGHT_NODE" "$revision_after"

# Keep it. Without this the bootloader restores the previous image on the next
# reboot, which is the designed behaviour and would undo what was proven.
printf '%s\r' 'mcuboot confirm' >"$debug_serial"
sleep 3

banner "Result"
printf 'FWU RADIO RESULT: PASS before=%s after=%s bytes=%s crc32=%s resent=%s\n' \
	"$revision_before" "$revision_after" "$after_size" "$after_crc" "${resent:-0}"
