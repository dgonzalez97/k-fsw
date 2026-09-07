#!/usr/bin/env bash
# Line, function and branch coverage of the unit suites.
#
# Twister builds the suites instrumented, runs them and composes the gcovr
# report. Nothing here re-renders its output.
#
# Only the unit suites are measured. The integration and HIL runs exercise far
# more, but they drive a built image rather than instrumented objects, and
# counting them here would mean claiming coverage the numbers do not describe.
set -Eeuo pipefail

KFSW_COVERAGE_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_COVERAGE_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" linux

command -v gcovr >/dev/null || {
	echo "ERROR: gcovr is required"
	echo "  ./.venv/bin/pip install gcovr"
	exit 1
}

out_dir="${KFSW_COVERAGE_OUT_DIR:-$KFSW_ROOT/build/coverage}"
twister_out_dir="$out_dir/twister"
html_dir="$out_dir/html"

rm -rf "$out_dir"
mkdir -p "$out_dir"

# native_sim compiles with the host compiler, so the host gcov is the one that
# reads its notes. Named explicitly because Twister otherwise reaches for
# ZEPHYR_TOOLCHAIN_VARIANT and crashes when it is unset, which it is here.
gcov_tool="${KFSW_GCOV_TOOL:-$(command -v gcov)}"
[[ -x "$gcov_tool" ]] || {
	echo "ERROR: no gcov found; set KFSW_GCOV_TOOL"
	exit 1
}

# Twister has no option for gcovr's filters, but gcovr reads gcovr.cfg from its
# root, which Twister sets from --coverage-basedir. Without it the report covers
# all of Zephyr and picolibc: 1208 files rather than 46.
install -m 644 "$KFSW_REPO_DIR/config/gcovr.cfg" "$KFSW_ROOT/gcovr.cfg"

echo "COVERAGE: running the unit suites instrumented, gcov $gcov_tool"
KFSW_TWISTER_OUT_DIR="$twister_out_dir" \
	ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}" \
	"$KFSW_CI_DIR/unit.sh" \
	--coverage \
	--coverage-basedir "$KFSW_ROOT" \
	--coverage-tool gcovr \
	--coverage-formats html,xml,txt \
	--gcov-tool "$gcov_tool" \
	>"$out_dir/twister.log" 2>&1 || {
	echo "COVERAGE RESULT: FAIL - see $out_dir/twister.log"
	exit 1
}

mv "$twister_out_dir/coverage" "$html_dir"

# gcovr writes an index and exits 0 even when it measured nothing, so a written
# report is not evidence of a measured one.
summary="$twister_out_dir/coverage_summary.json"
measured="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["line_total"])' \
	"$summary" 2>/dev/null || echo 0)"
if [[ "$measured" -eq 0 ]]; then
	echo "COVERAGE RESULT: FAIL - nothing was measured; see $out_dir/twister.log"
	exit 1
fi

python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for key, label in (("line", "lines"), ("function", "functions"), ("branch", "branches")):
    print("%-10s %5.1f%%  %6d / %d" % (
        label, d[key + "_percent"], d[key + "_covered"], d[key + "_total"]))
' "$summary"

echo
echo "COVERAGE: $html_dir/index.html"
echo "COVERAGE: serve it with $KFSW_REPO_DIR/tools/coverage/serve.sh"
echo "COVERAGE RESULT: PASS"
