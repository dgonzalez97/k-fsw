#!/usr/bin/env bash
# The unit suites under AddressSanitizer and LeakSanitizer: out-of-bounds
# reads and writes, use after free, and memory never released. 64-bit
# native_sim only; the 32-bit runtime needs the multilib sanitizers.
set -Eeuo pipefail

KFSW_ASAN_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ASAN_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" linux

out_dir="$KFSW_ROOT/build/asan"

echo "ASAN: Twister output: $out_dir"
set +e
west twister \
	--inline-logs \
	--outdir "$out_dir" \
	--platform native_sim/native/64 \
	--testsuite-root "$KFSW_REPO_DIR/tests/unit" \
	--testsuite-root "$KFSW_ROOT/kfsw-modules/tests" \
	--enable-asan \
	--enable-lsan \
	"$@"
twister_result=$?
set -e

reports="$(grep -rhoE "ERROR: (AddressSanitizer|LeakSanitizer): [^ ]+.*" "$out_dir" 2>/dev/null |
	sort -u || true)"

if [[ -n "$reports" ]]; then
	echo "ASAN: sanitizer reports"
	printf '%s\n' "$reports"
	echo "ASAN RESULT: FAIL"
	exit 1
fi

if [[ $twister_result -ne 0 ]]; then
	echo "ASAN RESULT: FAIL (the suites did not pass)"
	exit 1
fi

echo "ASAN RESULT: PASS"
