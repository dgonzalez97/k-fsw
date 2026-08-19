#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PROFILE="${1:-nucleo_l496zg}"

source "$(dirname "$0")/_common.sh" "$KFSW_PROFILE"

if [[ ! -f "$KFSW_BUILD_DIR/zephyr/zephyr.elf" ]]; then
    "$KFSW_ROOT/k-fsw/tools/build.sh" "$KFSW_PROFILE"
fi

west debug \
    -d "$KFSW_BUILD_DIR" \
    --runner openocd
