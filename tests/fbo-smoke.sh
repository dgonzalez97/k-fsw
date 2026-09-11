#!/usr/bin/env bash
# Checks that a node handed a file carries out what is in it.
#
# The point of file based operations is that a pass can run without an operator
# on the link for every step, so what matters is not that commands run but that
# the guards work: a failure continues or stops as the file says, and a line
# guarded by an event that never happened is skipped rather than run.
#
# The procedure is staged straight into the node's filesystem rather than sent
# over a link, because this is a test of the runner and not of file transfer.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
python="$KFSW_ROOT/.venv/bin/python"
work_dir="$(mktemp -d /tmp/kfsw-fbo.XXXXXX)"
failures=0

trap 'rm -rf -- "$work_dir"' EXIT

expect()
{
	if grep -aqF "$1" "$work_dir/out.log"; then
		printf '  [ok]   %s\n' "${2:-$1}"
	else
		printf '  [FAIL] %s\n' "${2:-$1}" >&2
		failures=$((failures + 1))
	fi
}

[[ -x "$executable" ]] || {
	printf 'no hosted image; build it with CONFIG_KFSW_FBO=y\n' >&2
	exit 2
}
[[ -x "$python" ]] || python="python3"

"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$work_dir/flash.bin" --offset 0xfc000 --size 0x40000 \
	"$KFSW_TESTS_DIR/procedures/smoke.txt" /ftp/procedures/smoke.txt >/dev/null

{
	printf 'fbo run smoke.txt\n'
	sleep 10
	printf 'fbo status\n'
	printf 'fbo run absent.txt\n'
	sleep 3
} | timeout 40 "$executable" --uart_stdinout --no-color \
	-flash="$work_dir/flash.bin" --stop_at=25 >"$work_dir/out.log" 2>&1 || true

sed -i 's/\x1b\[[0-9;]*m//g' "$work_dir/out.log"
tr -d '\r' <"$work_dir/out.log" >"$work_dir/clean.log"
mv "$work_dir/clean.log" "$work_dir/out.log"

echo "FBO SMOKE"

expect 'FBO: smoke.txt started' 'the procedure starts'
# -2 is -ENOENT: the line names a command that is not registered.
expect 'line 4 failed (-2)' 'an unknown command fails its line'
expect 'FBO: smoke.txt finished at line 8' 'on-error continue runs past the failure'
expect 'lines failed: 1' 'exactly one line failed'
expect 'lines skipped: 1' 'if-event skipped a line whose event never happened'
# Seven run and one skipped make the eight lines the file has. A skipped line
# is deliberately not counted as run: the distinction is the whole point of the
# guard, and collapsing them would hide whether a condition fired.
expect 'lines run: 7' 'blanks and comments are not lines'
expect 'run absent.txt: -2' 'a procedure that does not exist is refused'

if [[ "$failures" -eq 0 ]]; then
	echo "FBO SMOKE RESULT: PASS"
else
	echo "FBO SMOKE RESULT: FAIL ($failures)"
	sed -n '1,60p' "$work_dir/out.log" >&2
	exit 1
fi
