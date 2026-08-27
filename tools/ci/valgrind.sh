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
executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"

mkdir -p "$output_dir"

"$KFSW_TOOLS_DIR/build.sh" linux

echo "MEMORY: Valgrind log: $valgrind_log"
echo "MEMORY: Program log: $program_log"

set +e
valgrind \
	--error-exitcode=99 \
	--errors-for-leak-kinds=definite,indirect \
	--leak-check=full \
	--log-file="$valgrind_log" \
	--show-leak-kinds=definite,indirect \
	--track-origins=yes \
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
	-flash="$output_dir/kfsw-storage.bin" -flash_erase -flash_rm \
	</dev/null >"$program_log" 2>&1
result=$?
set -e

cat "$program_log"
cat "$valgrind_log"

if [[ $result -ne 0 ]]; then
	echo "MEMORY RESULT: FAIL (exit $result)"
	exit "$result"
fi

if ! grep -Fq '@READY ' "$program_log"; then
	echo "MEMORY RESULT: FAIL (KFSW-Linux did not reach @READY)"
	exit 1
fi

echo "MEMORY RESULT: PASS"
