#!/usr/bin/env bash
# Build and place the recovery image that lives in kfsw_golden_partition.
#
# The image is an ordinary signed MCUboot image, linked for the primary slot
# like any other, and kept in the golden partition until something copies it
# into a slot. Nothing copies it yet: selection is not implemented, so this
# script builds, checks that it fits, and writes it where it belongs.
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
# From kfsw_golden_partition in nucleo-mcuboot-flash.overlay.
golden_offset="${KFSW_GOLDEN_OFFSET:-0x000C0000}"
golden_size="${KFSW_GOLDEN_SIZE:-196608}"

fail() { echo "GOLDEN: FAIL $*" >&2; exit 1; }

build()
{
	KFSW_SYSBUILD=1 \
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
	printf 'GOLDEN: %s bytes of %s, %s%% spare\n' \
		"$bytes" "$golden_size" "$(( 100 * (golden_size - bytes) / golden_size ))"
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
	# Written as data, not as a bootable image: MCUboot boots the slots, and
	# this partition is only storage until something copies it into one.
	openocd -f interface/stlink.cfg -c "adapter serial $KFSW_STLINK_SERIAL" \
		-f target/stm32l4x.cfg \
		-c "init" -c "reset halt" \
		-c "flash write_image erase $image $golden_offset" \
		-c "reset run" -c "shutdown"
	echo "GOLDEN: written at $golden_offset"
	;;
*)
	echo "usage: $(basename "$0") [build|size|flash]" >&2
	exit 2
	;;
esac
