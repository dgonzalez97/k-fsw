#!/usr/bin/env bash
# A table file goes out, comes back, and is refused when it is broken.
#
# The unit suite writes files by hand and checks every rejection. This checks
# the operator's path instead: dump what is running, upload it again, and see
# the values change and change back, across two runs of the image so the file
# has to survive on its own.
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-table-file.XXXXXX)"
flash_image="$work_dir/table-flash.bin"
table_path="/kfsw/tables/test.tbl"

cleanup() {
	rm -rf -- "$work_dir"
}

fail() {
	echo "TABLE FILE RESULT: FAIL"
	echo "  $1"

	for log_file in "$work_dir"/*.log; do
		[[ -s "$log_file" ]] || continue
		echo "--- $(basename "$log_file") ---"
		cat "$log_file"
	done
	exit 1
}

run_kfsw() {
	local log_file="$1"
	local erase="$2"
	shift 2
	local -a flash_arguments=("-flash=$flash_image")

	[[ "$erase" == yes ]] && flash_arguments+=(-flash_erase)
	printf '%s\n' "$@" |
		"$executable" --uart_stdinout --stop_at=2.0 --no-color \
			"${flash_arguments[@]}" >"$log_file" 2>&1
}

expect() {
	local log_file="$1"
	local expected="$2"
	local message="$3"

	grep -Fq "$expected" "$log_file" || fail "$message"
}

trap cleanup EXIT

if [[ ! -x "$executable" ]]; then
	echo "TABLE FILE: building KFSW-Linux"
	"$KFSW_ROOT/k-fsw/tools/build.sh" linux
fi

# The file is written from the compiled defaults, then the live value is moved
# away from what the file holds.
run_kfsw "$work_dir/dump.log" yes \
	'table show' \
	"table dump 24 $table_path" \
	'param set test_u32 1234' \
	'param get test_u32'
expect "$work_dir/dump.log" 'state: empty' \
	"the service did not start with nothing loaded"
expect "$work_dir/dump.log" "Table 24 written to $table_path" \
	"the table was not written out"
expect "$work_dir/dump.log" 'test_u32 = 1234' \
	"the live value was not moved away from the file"

# A second run, so the file had to survive without the image that wrote it.
run_kfsw "$work_dir/load.log" no \
	'param set test_u32 1234' \
	"table check $table_path" \
	'param get test_u32' \
	"table load $table_path" \
	'param get test_u32' \
	'table show' \
	'table revert' \
	'param get test_u32' \
	'table revert'
expect "$work_dir/load.log" 'Table file would be accepted whole; nothing was applied' \
	"checking the file did not accept it"
expect "$work_dir/load.log" 'Table 24 loaded' \
	"the file was not adopted"
expect "$work_dir/load.log" 'state: loaded' \
	"the service did not report the file as loaded"
expect "$work_dir/load.log" 'Previous values are back' \
	"the load could not be undone"
expect "$work_dir/load.log" 'Nothing to revert' \
	"a second revert was accepted although nothing is held"
# 1234 twice before the load, 42 once after it, 1234 again after the revert.
[[ "$(grep -Fc 'test_u32 = 42' "$work_dir/load.log")" -eq 1 ]] ||
	fail "the load did not put the file's value in place exactly once"
[[ "$(grep -Fc 'test_u32 = 1234' "$work_dir/load.log")" -eq 3 ]] ||
	fail "checking or reverting did not leave the live value alone"

# A file that is not one.
run_kfsw "$work_dir/refuse.log" no \
	'param set test_u32 1234' \
	'table check /kfsw/tables/absent.tbl' \
	'table load /kfsw/params/parameters.dat' \
	'param get test_u32'
expect "$work_dir/refuse.log" 'Table file refused: -2' \
	"a file that does not exist was not reported as missing"
expect "$work_dir/refuse.log" 'Table file refused, nothing applied' \
	"a file that is not a table was not refused"
expect "$work_dir/refuse.log" 'test_u32 = 1234' \
	"a refused file changed a value"

echo "TABLE FILE RESULT: PASS"
