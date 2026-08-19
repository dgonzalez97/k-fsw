#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"

if [[ ! -x "$executable" ]]; then
    echo "ERROR: KFSW-Linux has not been built."
    echo "Run: $KFSW_ROOT/k-fsw/tools/build.sh linux"
    exit 1
fi

exec "$executable" "$@"
