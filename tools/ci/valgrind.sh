#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_VALGRIND_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_VALGRIND_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

source "$KFSW_TOOLS_DIR/_common.sh" linux

if ! command -v valgrind >/dev/null 2>&1; then
	echo "ERROR: valgrind is required"
	exit 1
fi

output_dir="${KFSW_VALGRIND_OUT_DIR:-$KFSW_ROOT/build/valgrind}"
program_log="$output_dir/kfsw-linux.log"
valgrind_log="$output_dir/valgrind.log"
corrupt_program_log="$output_dir/kfsw-linux-corrupt-snapshot.log"
corrupt_valgrind_log="$output_dir/valgrind-corrupt-snapshot.log"
corrupt_flash="$output_dir/kfsw-corrupt-storage.bin"
executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"

mkdir -p "$output_dir"

"$KFSW_TOOLS_DIR/build.sh" linux

echo "MEMORY: Valgrind log: $valgrind_log"
echo "MEMORY: Program log: $program_log"

printf '%s\n' 'param set test_u32 1234' 'param save' |
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		-flash="$corrupt_flash" -flash_erase \
		>"$output_dir/corrupt-fixture.log" 2>&1
"$KFSW_ROOT/k-fsw/tests/corrupt-param-flash.py" "$corrupt_flash"

run_valgrind()
{
	local memory_log="$1"
	local application_log="$2"
	shift 2

	valgrind \
		--error-exitcode=99 \
		--errors-for-leak-kinds=definite,indirect \
		--leak-check=full \
		--log-file="$memory_log" \
		--show-leak-kinds=definite,indirect \
		--track-origins=yes \
		"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		"$@" </dev/null >"$application_log" 2>&1
}

set +e
run_valgrind "$valgrind_log" "$program_log" \
	-flash="$output_dir/kfsw-storage.bin" -flash_erase -flash_rm
boot_result=$?
run_valgrind "$corrupt_valgrind_log" "$corrupt_program_log" \
	-flash="$corrupt_flash" -flash_rm
corrupt_result=$?
set -e

cat "$program_log"
cat "$valgrind_log"
cat "$corrupt_program_log"
cat "$corrupt_valgrind_log"

if [[ $boot_result -ne 0 || $corrupt_result -ne 0 ]]; then
	echo "MEMORY RESULT: FAIL (boot=$boot_result corrupt=$corrupt_result)"
	exit 1
fi

if ! grep -Fq '@READY ' "$program_log"; then
	echo "MEMORY RESULT: FAIL (KFSW-Linux did not reach @READY)"
	exit 1
fi

if ! grep -Fq 'Parameter snapshot restore failed' "$corrupt_program_log" ||
	! grep -Fq 'using defaults' "$corrupt_program_log"; then
	echo "MEMORY RESULT: FAIL (corrupt snapshot did not use safe defaults)"
	exit 1
fi

if ! grep -Fq '@READY ' "$corrupt_program_log"; then
	echo "MEMORY RESULT: FAIL (corrupt-snapshot boot did not reach @READY)"
	exit 1
fi

echo "MEMORY RESULT: PASS"
