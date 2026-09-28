#!/usr/bin/env bash
# Check explicit CSP feeds. Unit tests advance the clock through the timeout.
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

build_dir="$KFSW_ROOT/build/tests/linux-gndwdt"
work_dir="$(mktemp -d /tmp/kfsw-gndwdt-smoke.XXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT

KFSW_BUILD_DIR="$build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_ROOT/k-fsw/tests/config/linux-gndwdt.conf" \
	"$KFSW_ROOT/k-fsw/tools/build.sh" linux

python3 "$KFSW_ROOT/k-fsw/tests/gndwdt-smoke.py" \
	--executable "$build_dir/zephyr/zephyr.exe" --output "$work_dir/run"
