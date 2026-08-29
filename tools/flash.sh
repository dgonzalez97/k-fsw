#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_TARGET="${1:-nucleo_l496zg}"

source "$(dirname "$0")/_common.sh" "$KFSW_TARGET"

if [[ ! -f "$KFSW_BUILD_DIR/zephyr/zephyr.elf" ]]; then
    "$KFSW_ROOT/k-fsw/tools/build.sh" "$KFSW_TARGET"
fi

echo
echo "============================================================"
echo " K-FSW FLASH"
echo "============================================================"

west flash \
    -d "$KFSW_BUILD_DIR" \
    --runner openocd
