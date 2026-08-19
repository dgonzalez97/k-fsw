#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PROFILE="${1:-nucleo_l496zg}"

source "$(dirname "$0")/_common.sh" "$KFSW_PROFILE"

echo
echo "============================================================"
echo " K-FSW BUILD"
echo "============================================================"
echo "Profile : $KFSW_PROFILE"
echo "Board   : $ZEPHYR_BOARD"
echo "Build   : $KFSW_BUILD_DIR"
echo

west build \
    -p auto \
    -b "$ZEPHYR_BOARD" \
    "$KFSW_ROOT/k-fsw/app" \
    -d "$KFSW_BUILD_DIR"
