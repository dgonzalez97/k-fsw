#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-param-persistence.XXXXXX)"
persistence_flash="$work_dir/persistence-flash.bin"
corruption_flash="$work_dir/corruption-flash.bin"

cleanup()
{
	rm -rf -- "$work_dir"
}

fail()
{
	echo "PARAM PERSISTENCE RESULT: FAIL"
	echo "  $1"

	for log_file in "$work_dir"/*.log; do
		[[ -s "$log_file" ]] || continue
		echo "--- $(basename "$log_file") ---"
		cat "$log_file"
	done
	exit 1
}

run_kfsw()
{
	local flash_image="$1"
	local log_file="$2"
	local erase="$3"
	shift 3
	local -a flash_arguments=("-flash=$flash_image")

	[[ "$erase" == yes ]] && flash_arguments+=(-flash_erase)
	printf '%s\n' "$@" |
		"$executable" --uart_stdinout --stop_at=1.0 --no-color \
			"${flash_arguments[@]}" >"$log_file" 2>&1
}

expect()
{
	local log_file="$1"
	local expected="$2"
	local message="$3"

	grep -Fq "$expected" "$log_file" || fail "$message"
}

trap cleanup EXIT

if [[ ! -x "$executable" ]]; then
	echo "PARAM PERSISTENCE: building KFSW-Linux"
	"$KFSW_ROOT/k-fsw/tools/build.sh" linux
fi

run_kfsw "$persistence_flash" "$work_dir/execution-a.log" yes \
	'kfsw param get test_u32' \
	'kfsw param set test_u32 1234' \
	'kfsw param save'
expect "$work_dir/execution-a.log" 'test_u32 = 42' \
	"execution A did not start from the compiled default"
expect "$work_dir/execution-a.log" 'test_u32 = 1234' \
	"execution A did not update the live value"
expect "$work_dir/execution-a.log" 'Parameter snapshot save: PASS' \
	"execution A did not save the snapshot"

run_kfsw "$persistence_flash" "$work_dir/execution-b.log" no \
	'kfsw param get test_u32' \
	'kfsw param defaults' \
	'kfsw param get test_u32' \
	'kfsw param load' \
	'kfsw param get test_u32' \
	'kfsw param clear'
expect "$work_dir/execution-b.log" 'Persistent parameters restored' \
	"execution B did not automatically restore the snapshot"
[[ "$(grep -Fc 'test_u32 = 1234' "$work_dir/execution-b.log")" -eq 2 ]] || \
	fail "execution B did not show restored values before and after defaults/load"
expect "$work_dir/execution-b.log" 'Parameter defaults: PASS (saved snapshot unchanged)' \
	"defaults did not preserve the saved snapshot"
expect "$work_dir/execution-b.log" 'test_u32 = 42' \
	"defaults did not restore the compiled value"
expect "$work_dir/execution-b.log" 'Parameter snapshot load: PASS' \
	"explicit reload failed"
expect "$work_dir/execution-b.log" 'Parameter snapshot clear: PASS (RAM unchanged)' \
	"snapshot clear failed"

run_kfsw "$persistence_flash" "$work_dir/execution-c.log" no \
	'kfsw param get test_u32' \
	'kfsw param load'
expect "$work_dir/execution-c.log" 'No parameter snapshot; using compiled defaults' \
	"cleared snapshot was unexpectedly restored"
expect "$work_dir/execution-c.log" 'test_u32 = 42' \
	"execution C did not retain the compiled default"
expect "$work_dir/execution-c.log" 'Parameter snapshot load: no saved snapshot' \
	"missing snapshot was not reported"

run_kfsw "$corruption_flash" "$work_dir/corrupt-write.log" yes \
	'kfsw param set test_u32 1234' \
	'kfsw param save'
expect "$work_dir/corrupt-write.log" 'Parameter snapshot save: PASS' \
	"corruption fixture snapshot was not saved"

"$KFSW_ROOT/k-fsw/tests/corrupt-param-flash.py" "$corruption_flash"

run_kfsw "$corruption_flash" "$work_dir/corrupt-read.log" no \
	'kfsw param get test_u32'
expect "$work_dir/corrupt-read.log" 'Parameter snapshot restore failed' \
	"corrupt snapshot error was not visible"
expect "$work_dir/corrupt-read.log" 'using defaults' \
	"corrupt snapshot did not select the safe fallback"
expect "$work_dir/corrupt-read.log" 'test_u32 = 42' \
	"corrupt snapshot changed the compiled default"
expect "$work_dir/corrupt-read.log" '@READY ' \
	"boot did not continue after a corrupt snapshot"

cat "$work_dir/execution-a.log"
cat "$work_dir/execution-b.log"
cat "$work_dir/execution-c.log"
cat "$work_dir/corrupt-read.log"
echo "PARAM PERSISTENCE RESULT: PASS"
