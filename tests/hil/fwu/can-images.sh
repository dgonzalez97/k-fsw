#!/usr/bin/env bash
# Build, and optionally install, the images the CAN firmware update test needs.
#
# The test compares a running image against a candidate, so it needs two flight
# images that differ by their revision, plus a ground node with room for the
# candidate and the readbacks. Keeping them prebuilt by hand is how that test
# ended up skipped on a bench that had everything else; building them here is
# cheaper than a skip nobody notices.
#
#   tests/hil/fwu/can-images.sh build     # three builds, prints their paths
#   tests/hil/fwu/can-images.sh install   # flash the baseline and confirm it
set -Eeuo pipefail

source "$(dirname "$0")/../../../tools/_common.sh" nucleo_l496zg

profiles="$KFSW_REPO_DIR/config/profiles"
fixtures="$KFSW_REPO_DIR/tests/hil/fwu"
out="${KFSW_FWU_CAN_DIR:-$KFSW_ROOT/build/fwu-can}"
key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"

flight_conf="$profiles/nucleo-mcuboot.conf;$profiles/nucleo-mcuboot-fwu.conf"
flight_conf="$flight_conf;$profiles/nucleo-mcuboot-fwu-lite.conf;$profiles/nucleo-can.conf"
flight_overlay="$profiles/nucleo-mcuboot-flash.overlay;$profiles/nucleo-mcuboot.overlay"
flight_overlay="$flight_overlay;$profiles/nucleo-mcuboot-fwu.overlay;$profiles/nucleo-can.overlay"

build_flight() # revision, directory
{
	KFSW_IMAGE_VERSION="$1" \
	KFSW_SYSBUILD=1 \
	KFSW_MCUBOOT_KEY="$key" \
	KFSW_BUILD_DIR="$2" \
	KFSW_EXTRA_CONF_FILE="$flight_conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$flight_overlay" \
	KFSW_MCUBOOT_DTC_OVERLAY_FILE="$profiles/nucleo-mcuboot-flash.overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg
}

build_all()
{
	[[ -r "$key" ]] || {
		echo "CAN FWU IMAGES: NOT RUN - no signing key at $key"
		exit 0
	}

	KFSW_BUILD_DIR="$out/ground" \
	KFSW_EXTRA_CONF_FILE="$fixtures/ground-can.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$fixtures/ground-can.overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" linux

	build_flight fwu-can-before "$out/before"
	build_flight fwu-can-after "$out/after"
}

report()
{
	echo "KFSW_FWU_CAN_GROUND=$out/ground/zephyr/zephyr.exe"
	echo "KFSW_FWU_CAN_IMAGE=$out/after/app/zephyr/zephyr.signed.bin"
	echo "KFSW_FWU_CAN_REVISION=fwu-can-after"
}

install_baseline()
{
	# west flash on the sysbuild directory writes the bootloader and the
	# primary slot by sector, so the golden region and the filesystem below
	# it are left alone. A whole-chip erase would take both with it.
	kfsw_west_flash -d "$out/before" --runner openocd

	local console="$KFSW_SERIAL"
	stty -F "$console" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
	local log
	log="$(mktemp)"
	timeout 90 cat "$console" >"$log" &
	local reader=$!
	sleep 8
	printf '%s\r\n' 'mcuboot confirm' 'mcuboot' >"$console"
	sleep 4
	kill "$reader" 2>/dev/null || true
	wait "$reader" 2>/dev/null || true

	if ! grep -q 'confirmed: 1' "$log"; then
		echo "CAN FWU IMAGES: FAIL - the baseline did not confirm itself"
		sed -n '1,40p' "$log"
		rm -f "$log"
		exit 1
	fi
	rm -f "$log"
	echo "CAN FWU IMAGES: baseline installed and confirmed"
}

case "${1:-build}" in
build)
	build_all
	report
	;;
install)
	build_all
	install_baseline
	report
	;;
*)
	echo "usage: $(basename "$0") [build|install]" >&2
	exit 2
	;;
esac
