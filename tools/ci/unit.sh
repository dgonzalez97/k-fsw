#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_UNIT_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_UNIT_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" linux

twister_out_dir="${KFSW_TWISTER_OUT_DIR:-${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/twister}"

cache_args=()
if [[ -n "${KFSW_OUTPUT_ROOT:-}" ]]; then
	cache_args+=("--extra-args=USER_CACHE_DIR=$KFSW_OUTPUT_ROOT/cache")
fi

echo "UNIT: Twister output: $twister_out_dir"
west twister \
	--inline-logs \
	--outdir "$twister_out_dir" \
	--platform native_sim/native/64 \
	--platform native_sim/native \
	--testsuite-root "$KFSW_REPO_DIR/tests/unit" \
	--testsuite-root "$KFSW_ROOT/kfsw-modules/tests" \
	"${cache_args[@]}" "$@"

echo "UNIT RESULT: PASS ($twister_out_dir)"
