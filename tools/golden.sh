#!/usr/bin/env bash
# Build and place the recovery image for kfsw_golden_partition.
#
# It is an ordinary signed MCUboot image, linked for the primary slot, kept in
# the golden partition until the recovery service copies it into a slot.
#
#   tools/golden.sh build
#   tools/golden.sh flash
#   tools/golden.sh size
set -Eeuo pipefail

GOLDEN_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TOOLS_DIR="$(dirname "$GOLDEN_TOOL")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

profiles="$KFSW_REPO_DIR/config/profiles"
build_dir="${KFSW_GOLDEN_BUILD_DIR:-$KFSW_ROOT/build/golden}"
image="$build_dir/app/zephyr/zephyr.signed.bin"
# kfsw_golden_partition in nucleo-mcuboot-flash.overlay gives an offset inside
# flash0; the debugger addresses memory, and this part maps flash at 0x08000000.
golden_offset="${KFSW_GOLDEN_OFFSET:-0x080C0000}"
golden_size="${KFSW_GOLDEN_SIZE:-192512}"

fail() { echo "GOLDEN: FAIL $*" >&2; exit 1; }

build()
{
	KFSW_SYSBUILD=1 \
		KFSW_MCUBOOT_KEY="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}" \
		KFSW_BUILD_DIR="$build_dir" \
		KFSW_PRISTINE=always \
		KFSW_EXTRA_CONF_FILE="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-golden.conf${KFSW_GOLDEN_EXTRA_CONF:+;$KFSW_GOLDEN_EXTRA_CONF}" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay" \
		KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg
}

size()
{
	[[ -f "$image" ]] || fail "no image at $image; run build first"
	local bytes
	bytes="$(stat -c %s "$image")"
	if (( bytes > golden_size )); then
		fail "the image is $bytes bytes and the golden partition holds $golden_size"
	fi
	printf 'GOLDEN: %s bytes of %s, %s bytes spare (%s%%)\n' \
		"$bytes" "$golden_size" "$((golden_size - bytes))" \
		"$(( 100 * (golden_size - bytes) / golden_size ))"
}

case "${1:-build}" in
build)
	build
	size
	;;
size)
	size
	;;
flash)
	size
	command -v openocd >/dev/null || fail "openocd is not on PATH"
	[[ -n "${KFSW_STLINK_SERIAL:-}" ]] || fail "the target profile names no ST-Link serial"
	# Written as data: MCUboot boots the slots, not this partition.
	# The board's own configuration, the same one west flashes with.
	support="$ZEPHYR_BASE/boards/st/nucleo_l496zg/support"

	openocd -s "$support" -f "$support/openocd.cfg" \
		-c "adapter serial $KFSW_STLINK_SERIAL" \
		-c "init" -c "reset halt" \
		-c "program $image verify $golden_offset" \
		-c "reset run" -c "shutdown" || fail "the image was not written"
	echo "GOLDEN: written and verified at $golden_offset"
	;;
*)
	echo "usage: $(basename "$0") [build|size|flash]" >&2
	exit 2
	;;
esac
