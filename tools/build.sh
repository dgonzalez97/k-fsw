#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_TARGET="${1:-nucleo_l496zg}"
KFSW_PRISTINE="${KFSW_PRISTINE:-auto}"

case "$KFSW_PRISTINE" in
	auto|always|never)
		;;
	*)
		echo "ERROR: KFSW_PRISTINE must be auto, always, or never"
		exit 1
		;;
esac

source "$(dirname "$0")/_common.sh" "$KFSW_TARGET"

echo
echo "============================================================"
echo " K-FSW BUILD"
echo "============================================================"
echo "Target  : $KFSW_TARGET"
echo "Board   : $ZEPHYR_BOARD"
echo "Build   : $KFSW_BUILD_DIR"
echo "Pristine: $KFSW_PRISTINE"
echo

west_args=()
cmake_args=()

# Sysbuild is off by default; only the MCUboot composition turns it on.
if [[ -n "${KFSW_SYSBUILD:-}" ]]; then
	west_args+=(--sysbuild)
fi

if [[ -n "${KFSW_EXTRA_CONF_FILE:-}" ]]; then
	west_args+=(--extra-conf "$KFSW_EXTRA_CONF_FILE")
fi

if [[ -n "${KFSW_EXTRA_DTC_OVERLAY_FILE:-}" ]]; then
	west_args+=(--extra-dtc-overlay "$KFSW_EXTRA_DTC_OVERLAY_FILE")

fi

# The bootloader image only gets the shared flash map overlay.
if [[ -n "${KFSW_MCUBOOT_DTC_OVERLAY_FILE:-}" ]]; then
	cmake_args+=("-Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$KFSW_MCUBOOT_DTC_OVERLAY_FILE")
fi

if [[ -n "${KFSW_CONF_FILE:-}" ]]; then
	cmake_args+=("-DCONF_FILE=$KFSW_CONF_FILE")
fi

# The bootloader is configured in app/sysbuild.conf. Only the signing key is
# passed here; without it MCUboot uses its public development key.
if [[ -n "${KFSW_MCUBOOT_KEY:-}" ]]; then
	if [[ ! -r "$KFSW_MCUBOOT_KEY" ]]; then
		echo "ERROR: KFSW_MCUBOOT_KEY is set but not readable: $KFSW_MCUBOOT_KEY"
		exit 1
	fi
	cmake_args+=("-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\"$KFSW_MCUBOOT_KEY\"")
fi

if [[ ${#cmake_args[@]} -gt 0 ]]; then
	west_args+=(-- "${cmake_args[@]}")
fi

west build \
    -p "$KFSW_PRISTINE" \
    -b "$ZEPHYR_BOARD" \
    "$KFSW_ROOT/k-fsw/app" \
    -d "$KFSW_BUILD_DIR" \
	"${west_args[@]}"
