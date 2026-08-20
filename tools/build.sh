#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PROFILE="${1:-nucleo_l496zg}"
KFSW_PRISTINE="${KFSW_PRISTINE:-auto}"

case "$KFSW_PRISTINE" in
	auto|always|never)
		;;
	*)
		echo "ERROR: KFSW_PRISTINE must be auto, always, or never"
		exit 1
		;;
esac

source "$(dirname "$0")/_common.sh" "$KFSW_PROFILE"

echo
echo "============================================================"
echo " K-FSW BUILD"
echo "============================================================"
echo "Profile : $KFSW_PROFILE"
echo "Board   : $ZEPHYR_BOARD"
echo "Build   : $KFSW_BUILD_DIR"
echo "Pristine: $KFSW_PRISTINE"
echo

west_args=()

if [[ -n "${KFSW_EXTRA_CONF_FILE:-}" ]]; then
	west_args+=(--extra-conf "$KFSW_EXTRA_CONF_FILE")
fi

if [[ -n "${KFSW_EXTRA_DTC_OVERLAY_FILE:-}" ]]; then
	west_args+=(--extra-dtc-overlay "$KFSW_EXTRA_DTC_OVERLAY_FILE")
fi

west build \
    -p "$KFSW_PRISTINE" \
    -b "$ZEPHYR_BOARD" \
    "$KFSW_ROOT/k-fsw/app" \
    -d "$KFSW_BUILD_DIR" \
	"${west_args[@]}"
