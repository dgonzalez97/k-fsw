#!/usr/bin/env bash
# Line, function and branch coverage of the unit suites, reported by gcovr
# through Twister. Integration and hardware tests are not included.
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

out_dir="$KFSW_ROOT/build/coverage"
twister_out_dir="$out_dir/twister"
html_dir="$out_dir/html"

rm -rf "$out_dir"
mkdir -p "$out_dir"

# native_sim uses the host compiler, so use the host gcov. Twister fails when
# ZEPHYR_TOOLCHAIN_VARIANT is unset.
gcov_tool="${KFSW_GCOV_TOOL:-$(command -v gcov)}"
[[ -x "$gcov_tool" ]] || {
	echo "ERROR: no gcov found; set KFSW_GCOV_TOOL"
	exit 1
}

# gcovr reads gcovr.cfg from --coverage-basedir; without it the report covers
# all of Zephyr.
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
