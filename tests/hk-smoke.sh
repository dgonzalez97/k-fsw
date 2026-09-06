#!/usr/bin/env bash
# Checks that a housekeeping report says what its parameters say.
#
# That comparison is the whole point of the service. Collecting a set is only
# worth doing if the set agrees with reading its members one at a time; a frame
# that is fast and wrong is worse than the round trips it replaced.
#
# So the script reads three values individually, collects a report naming the
# same three, and asserts the frame carries exactly those numbers at exactly
# the declared widths. It also checks the refusals, because a report that
# cannot be collected must be refused when it is defined rather than discovered
# during a pass.
#
# Runs against the hosted image by default. Pass --serial to run it against a
# board over its debug UART instead, in which case the by-id path is required.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
serial=""
work_dir="$(mktemp -d)"
failures=0

trap 'rm -rf "$work_dir"' EXIT

while [[ $# -gt 0 ]]; do
	case "$1" in
	--serial)
		serial="${2:-}"
		shift 2
		;;
	--executable)
		executable="${2:-}"
		shift 2
		;;
	*)
		printf 'unknown argument: %s\n' "$1" >&2
		exit 2
		;;
	esac
done

expect()
{
	if grep -aqF "$1" "$work_dir/output.log"; then
		printf '  [ok]   %s\n' "$1"
	else
		printf '  [FAIL] missing: %s\n' "$1" >&2
		failures=$((failures + 1))
	fi
}

expect_re()
{
	if grep -aqE "$1" "$work_dir/output.log"; then
		printf '  [ok]   %s\n' "$2"
	else
		printf '  [FAIL] %s\n' "$2" >&2
		failures=$((failures + 1))
	fi
}

# board.node_id (u16), telemetry.uptime_s (u32) and storage.fs_total_kb (u32),
# read individually and then as one report.
commands=(
	'param get node_id'
	'param get fs_total_kb'
	'hk define 0 1:0 5:0'
	'hk collect 0'
	'hk get 0'
	'hk show'
	'hk define 1 1:99'
	'hk period 0 1'
)

if [[ -n "$serial" ]]; then
	[[ "$serial" == /dev/serial/by-id/* ]] ||
		{ printf 'the serial device must be a stable /dev/serial/by-id path\n' >&2; exit 1; }
	[[ -e "$serial" ]] || { printf '%s is not present\n' "$serial" >&2; exit 1; }

	stty_saved="$(stty -F "$serial" -g)"
	trap 'stty -F "$serial" "$stty_saved" 2>/dev/null || true; rm -rf "$work_dir"' EXIT
	stty -F "$serial" 115200 cs8 -cstopb -parenb -crtscts raw -echo
	timeout 40s cat "$serial" >"$work_dir/output.log" &
	capture_pid=$!
	sleep 1
	printf '\r' >"$serial"
	sleep 0.5
	for command in "${commands[@]}"; do
		printf '%s\r' "$command" >"$serial"
		sleep 1.5
	done
	kill "$capture_pid" 2>/dev/null || true
	wait "$capture_pid" 2>/dev/null || true
else
	[[ -x "$executable" ]] || { printf '%s is not built\n' "$executable" >&2; exit 1; }
	printf '%s\n' "${commands[@]}" |
		"$executable" --uart_stdinout --stop_at=12.0 --no-color \
			-flash="$work_dir/flash.bin" >"$work_dir/output.log" 2>&1 || true
fi

printf '\n=== The report collects ===\n'
expect 'report 0 defines 2 values'
expect 'report 0 collected'

printf '\n=== The frame agrees with the parameters ===\n'
# node_id is a u16 and fs_total_kb a u32, so the payload is six bytes: the
# widths come from the declarations, not from what the values happen to need.
expect_re 'seq 0 .* 2 values .* 16 bytes' 'the frame is the declared width'

# The shell terminates its lines with CR, so it is stripped before matching
# rather than every pattern here having to allow for it.
tr -d '\r' <"$work_dir/output.log" >"$work_dir/plain.log"
node_id="$(sed -n 's/^node_id = \([0-9]*\)$/\1/p' "$work_dir/plain.log" | head -1)"
total_kb="$(sed -n 's/^fs_total_kb = \([0-9]*\)$/\1/p' "$work_dir/plain.log" | head -1)"
payload="$(sed -n 's/^  \([0-9a-f]\{12\}\)$/\1/p' "$work_dir/plain.log" | head -1)"

if [[ -z "$node_id" || -z "$total_kb" || -z "$payload" ]]; then
	printf '  [FAIL] could not read the values back to compare\n' >&2
	failures=$((failures + 1))
else
	expected="$(printf '%04x%08x' "$node_id" "$total_kb")"
	if [[ "$payload" == "$expected" ]]; then
		printf '  [ok]   the frame carries node_id=%s and fs_total_kb=%s\n' \
			"$node_id" "$total_kb"
	else
		printf '  [FAIL] the frame says %s, the parameters say %s\n' \
			"$payload" "$expected" >&2
		failures=$((failures + 1))
	fi
fi

printf '\n=== A report that cannot be collected is refused when defined ===\n'
expect 'define report 1: -2'
expect 'period for report 0: -34'

printf '\n=== The counters are published ===\n'
expect 'reports defined: 1'

printf '\n=== Result ===\n'
if [[ "$failures" -eq 0 ]]; then
	printf 'HK RESULT: PASS\n'
else
	printf 'HK RESULT: FAIL checks=%d\n' "$failures" >&2
	exit 1
fi
