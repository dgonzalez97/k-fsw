#!/usr/bin/env bash
# clang-tidy, driven by CodeChecker, over the NUCLEO image and the services
# only the Linux composition carries. A MEDIUM or worse report in K-FSW code
# fails the stage; LOW reports are listed. A reviewed false positive is marked
# in the source with a codechecker_false_positive comment and its reason.
#
#   ./.venv/bin/pip install codechecker clang-tidy
set -Eeuo pipefail

KFSW_CODECHECKER_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_CODECHECKER_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

command -v CodeChecker >/dev/null && command -v clang-tidy >/dev/null || {
	echo "ERROR: CodeChecker and clang-tidy are required"
	echo "  ./.venv/bin/pip install codechecker clang-tidy"
	exit 1
}

out_dir="$KFSW_ROOT/build/codechecker"
services_dir="$out_dir/services"
reports="$out_dir/reports"
rm -rf "$out_dir"
mkdir -p "$out_dir"

echo "CODECHECKER: building nucleo_l496zg"
"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$out_dir/build.log" 2>&1

# Configured, and its generated headers made, but not built: it would not fit.
echo "CODECHECKER: configuring the services the NUCLEO leaves out"
west build -p always -b "$ZEPHYR_BOARD" "$KFSW_ROOT/k-fsw/app" -d "$services_dir" --cmake-only \
	--extra-conf "$KFSW_ROOT/k-fsw/tests/config/sca-services.conf" \
	--extra-dtc-overlay "$KFSW_ROOT/k-fsw/config/profiles/nucleo-mcuboot-flash.overlay" \
	--extra-dtc-overlay "$KFSW_ROOT/k-fsw/config/profiles/nucleo-mcuboot-fwu.overlay" \
	>>"$out_dir/build.log" 2>&1
cmake --build "$services_dir" --target syscall_list_h_target driver_validation_h_target \
	kobj_types_h_target offsets_h version_h >>"$out_dir/build.log" 2>&1

# One file compiled twice with different flags is two translation units, so
# the key is the file and its command. Keying on the file alone discarded the
# second configuration and the analyser never saw it.
units="$(python3 - "$KFSW_BUILD_DIR/compile_commands.json" \
	"$services_dir/compile_commands.json" "$out_dir/compile_commands.json" <<'MERGE'
import json
import sys

seen = {}
for path in sys.argv[1:3]:
    with open(path) as handle:
        for entry in json.load(handle):
            command = entry.get("command") or " ".join(entry.get("arguments", ()))
            seen.setdefault((entry["file"], command), entry)
with open(sys.argv[3], "w") as handle:
    json.dump(list(seen.values()), handle)
print(len(seen))
MERGE
)"
if [[ ! -s "$out_dir/compile_commands.json" || "${units:-0}" -lt 1 ]]; then
	echo "CODECHECKER RESULT: FAIL (no translation units to analyse)"
	exit 1
fi
echo "CODECHECKER: $units translation units"

cat >"$out_dir/skip.txt" <<SKIP
-*/third_party/*
+$KFSW_ROOT/k-fsw/app/*
+$KFSW_ROOT/kfsw-platform/*
+$KFSW_ROOT/kfsw-services/*
+$KFSW_ROOT/kfsw-comms/*
+$KFSW_ROOT/kfsw-modules/*
-*
SKIP

# Two checkers are off: include cycles are in Zephyr's own headers, and a
# const k_tid_t is how Zephyr declares a thread.
echo "CODECHECKER: analysing"
# The analyser's own exit status decides whether it ran. Swallowing it meant a
# crashed analyser and a clean report were the same thing, and the stage passed.
analyze_status=0
CodeChecker analyze "$out_dir/compile_commands.json" --analyzers clang-tidy \
	--skip "$out_dir/skip.txt" --disable misc-header-include-cycle \
	--disable misc-misplaced-const -j "$(nproc)" -o "$reports" \
	>"$out_dir/analyze.log" 2>&1 || analyze_status=$?
analysed="$(sed -n 's/.*Total analyzed compilation commands: \([0-9]\+\).*/\1/p' \
	"$out_dir/analyze.log" | tail -1)"
failed="$(grep -acE '^\[ERROR[^]]*\] - Analyzing .* failed!' "$out_dir/analyze.log" || true)"
skipped="$(sed -n 's/.*Skipped compilation commands: \([0-9]\+\).*/\1/p' \
	"$out_dir/analyze.log" | tail -1)"
echo "CODECHECKER: ${analysed:-0} of $units units analysed, ${skipped:-0} skipped," \
	"$failed failed"
if [[ "$analyze_status" -ne 0 ]]; then
	echo "CODECHECKER RESULT: FAIL (the analyser exited $analyze_status)"
	tail -20 "$out_dir/analyze.log"
	exit 1
fi
if [[ "$analysed" -lt 1 || "$failed" -gt 0 ]]; then
	echo "CODECHECKER RESULT: FAIL (nothing analysed, or a unit failed)"
	tail -20 "$out_dir/analyze.log"
	exit 1
fi

# parse exits 2 when it has findings to show, which is not a failure. Anything
# else is, and reading a report it could not produce is how a gate lies.
parse_status=0
CodeChecker parse "$reports" --export html -o "$out_dir/html" >/dev/null 2>&1 || parse_status=$?
if [[ "$parse_status" -ne 0 && "$parse_status" -ne 2 ]]; then
	echo "CODECHECKER RESULT: FAIL (the HTML export exited $parse_status)"
	exit 1
fi
parse_status=0
CodeChecker parse "$reports" >"$out_dir/reports.txt" 2>&1 || parse_status=$?
if [[ "$parse_status" -ne 0 && "$parse_status" -ne 2 ]]; then
	echo "CODECHECKER RESULT: FAIL (parsing the reports exited $parse_status)"
	tail -20 "$out_dir/reports.txt"
	exit 1
fi
if [[ ! -f "$out_dir/reports.txt" ]]; then
	echo "CODECHECKER RESULT: FAIL (no report to read)"
	exit 1
fi

ours="$(grep -aE "^\[[A-Z]+\] $KFSW_ROOT/(k-fsw|kfsw-[a-z]+)/" "$out_dir/reports.txt" |
	grep -v "/third_party/" | sed "s#$KFSW_ROOT/##" | sort -u || true)"
failing="$(grep -aE '^\[(MEDIUM|HIGH|CRITICAL)\]' <<<"$ours" || true)"

[[ -n "$ours" ]] && printf '%s\n' "$ours"
echo "CODECHECKER: report in $out_dir/html/index.html"
if [[ -n "$failing" ]]; then
	echo "CODECHECKER RESULT: FAIL ($(wc -l <<<"$failing") MEDIUM or worse in K-FSW code)"
	exit 1
fi
echo "CODECHECKER RESULT: PASS"
