#!/usr/bin/env bash
# Line coverage of the unit suites, reported per repository.
#
# One number for the whole workspace would say almost nothing: the layers are
# owned separately and tested separately, and a well-covered service would hide
# a thin one behind it. So the report is split the way the code is.
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

echo "COVERAGE: running the unit suites instrumented, gcov $gcov_tool"
KFSW_TWISTER_OUT_DIR="$twister_out_dir" \
	ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}" \
	"$KFSW_CI_DIR/unit.sh" \
	--coverage \
	--coverage-basedir "$KFSW_ROOT" \
	--coverage-tool gcovr \
	--gcov-tool "$gcov_tool" \
	>"$out_dir/twister.log" 2>&1 || {
	echo "COVERAGE RESULT: FAIL - the suites did not pass; see $out_dir/twister.log"
	exit 1
}

# Each repository reported on its own. gcovr is pointed at the whole tree and
# filtered per repository rather than run four times, so every report comes from
# one set of measurements and the numbers can be compared with each other.
declare -A repositories=(
	[k-fsw]="$KFSW_REPO_DIR/app"
	[kfsw-platform]="$KFSW_ROOT/kfsw-platform/src"
	[kfsw-services]="$KFSW_ROOT/kfsw-services/src"
	[kfsw-comms]="$KFSW_ROOT/kfsw-comms/src"
	[kfsw-modules]="$KFSW_ROOT/kfsw-modules"
)

summary="$out_dir/summary.txt"
: >"$summary"

for repository in k-fsw kfsw-platform kfsw-services kfsw-comms kfsw-modules; do
	source_dir="${repositories[$repository]}"
	[[ -d "$source_dir" ]] || continue

	report_dir="$out_dir/$repository"
	mkdir -p "$report_dir"

	# third_party holds vendored libcsp and libparam. They are pinned
	# upstream code with their own tests; counting them would move the
	# number without saying anything about K-FSW.
	gcovr \
		--root "$KFSW_ROOT" \
		--filter "$source_dir" \
		--exclude '.*/third_party/.*' \
		--exclude '.*/tests/.*' \
		--gcov-ignore-parse-errors \
		--html-details "$report_dir/index.html" \
		--json-summary "$report_dir/summary.json" \
		--print-summary \
		"$twister_out_dir" >"$report_dir/gcovr.log" 2>&1 || true

	if [[ -s "$report_dir/summary.json" ]]; then
		python3 - "$repository" "$report_dir/summary.json" >>"$summary" <<'PY'
import json
import sys

repository, path = sys.argv[1], sys.argv[2]
with open(path) as handle:
    data = json.load(handle)
print("%-16s %6.1f%%  %5d / %-5d lines" % (
    repository,
    data.get("line_percent", 0.0),
    data.get("line_covered", 0),
    data.get("line_total", 0),
))
PY
	else
		printf '%-16s %s\n' "$repository" "no measurements" >>"$summary"
	fi
done

echo
echo "COVERAGE: line coverage of the unit suites"
cat "$summary"
echo
echo "COVERAGE: reports under $out_dir/<repository>/index.html"
echo "COVERAGE RESULT: PASS"
