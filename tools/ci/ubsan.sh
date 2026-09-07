#!/usr/bin/env bash
# The unit suites under the undefined behaviour sanitizer.
#
# Valgrind watches memory; this watches the arithmetic. Signed overflow,
# oversized shifts and misaligned loads are not memory errors, so Valgrind
# never sees them, and the compiler only rejects the ones it can prove.
set -Eeuo pipefail

KFSW_UBSAN_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_UBSAN_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_UBSAN_TOOL")/.."

source "$KFSW_TOOLS_DIR/_common.sh" linux

out_dir="${KFSW_UBSAN_OUT_DIR:-$KFSW_ROOT/build/ubsan}"

echo "UBSAN: Twister output: $out_dir"
set +e
KFSW_TWISTER_OUT_DIR="$out_dir" "$KFSW_CI_DIR/unit.sh" --enable-ubsan
twister_result=$?
set -e

# A violation aborts the run, so Twister already fails. Checked again here
# because a sanitizer that reports and continues would leave the gate green,
# and the reports are worth naming either way.
reports="$(grep -rho "runtime error: .*" "$out_dir" 2>/dev/null | sort -u || true)"

if [[ -n "$reports" ]]; then
	echo "UBSAN: undefined behaviour reported"
	printf '%s\n' "$reports"
	echo "UBSAN RESULT: FAIL"
	exit 1
fi

if [[ $twister_result -ne 0 ]]; then
	echo "UBSAN RESULT: FAIL (the suites did not pass)"
	exit 1
fi

echo "UBSAN RESULT: PASS"
