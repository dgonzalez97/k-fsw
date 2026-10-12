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
# Twister runs this many builds at once and each one compiles in parallel, so
# the two multiply: --jobs 8 with eight-way builds measured a peak of 36
# compilers and a load of 28 on a 16-core machine. Give Twister the job count
# and build each instance serially, which keeps the peak at the job count.
twister_jobs=()
if [[ "${KFSW_JOBS:-0}" != "0" ]]; then
	twister_jobs=(--jobs "$KFSW_JOBS")
	export CMAKE_BUILD_PARALLEL_LEVEL=1
fi

west twister \
	"${twister_jobs[@]}" \
	--inline-logs \
	--outdir "$twister_out_dir" \
	--platform native_sim/native/64 \
	--platform native_sim/native \
	--testsuite-root "$KFSW_REPO_DIR/tests/unit" \
	--testsuite-root "$KFSW_ROOT/kfsw-modules/tests" \
	"${cache_args[@]}" "$@"

echo "UNIT RESULT: PASS ($twister_out_dir)"
