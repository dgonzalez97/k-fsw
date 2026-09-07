#!/usr/bin/env bash
# Checks that the ground bridge reads a housekeeping sample the node agrees with.
#
# The bridge is a second implementation of the wire: it frames CSP over KISS on
# the host, where the node does it in C. Two implementations of one protocol is
# how a protocol quietly forks, so this asserts the only thing that stops it —
# the bytes the bridge pulls off the link are the bytes the node's own shell
# prints for the same sample.
#
# It also covers the CSP side of housekeeping, which nothing else does: the
# unit suites exercise the ring and the collector, and the server on port 14
# had no test before this.
#
# Software only. No Yamcs, no radio: the node runs hosted and the bridge talks
# to its uart_1 pseudo-terminal.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
definition="$KFSW_REPO_DIR/ground-station/reports/nucleo-temperature.yaml"
python="${KFSW_PYTHON:-$KFSW_ROOT/.venv/bin/python}"
work_dir="$(mktemp -d /tmp/kfsw-hk-yamcs.XXXXXX)"
node_pid=""
failures=0

cleanup()
{
	exec 3>&- 2>/dev/null || true
	[[ -n "$node_pid" ]] && kill "$node_pid" 2>/dev/null || true
	[[ -n "$node_pid" ]] && wait "$node_pid" 2>/dev/null || true
	rm -rf -- "$work_dir"
}
trap cleanup EXIT

while [[ $# -gt 0 ]]; do
	case "$1" in
	--executable)
		executable="${2:?--executable requires a path}"
		shift 2
		;;
	*)
		printf 'unknown argument: %s\n' "$1" >&2
		exit 2
		;;
	esac
done

check()
{
	if [[ "$2" == "$3" ]]; then
		printf '  [ok]   %s\n' "$1"
	else
		printf '  [FAIL] %s\n         wanted %s\n         got    %s\n' "$1" "$3" "$2" >&2
		failures=$((failures + 1))
	fi
}

[[ -x "$executable" ]] || {
	printf 'no hosted image at %s; build it with the linux-temperature profile\n' \
		"$executable" >&2
	exit 2
}
[[ -x "$python" ]] || python="python3"

mkfifo "$work_dir/stdin"
"$executable" --uart_stdinout --no-color \
	-flash="$work_dir/flash.bin" -flash_erase -flash_rm \
	<"$work_dir/stdin" >"$work_dir/node.log" 2>&1 &
node_pid=$!
exec 3>"$work_dir/stdin"

for _ in $(seq 1 100); do
	grep -q 'uart_1 connected to pseudotty: ' "$work_dir/node.log" && break
	sleep 0.1
done
pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/node.log" | head -1)"
[[ -n "$pty" ]] || {
	printf 'the node never exposed uart_1\n' >&2
	sed -n '1,60p' "$work_dir/node.log" >&2
	exit 1
}

# The same file that generates the mission database tells the node what to
# collect, which is the only reason the two can be expected to agree.
define_command="$("$python" "$KFSW_REPO_DIR/tools/ground/hk-report.py" "$definition" define)"
printf '%s\n' "$define_command" 'hk collect 0' 'hk get 0' >&3
sleep 2

# The shell terminates its lines with CR.
shell_payload="$(tr -d '\r' <"$work_dir/node.log" |
	sed -n 's/^  \([0-9a-f]\{2,\}\)$/\1/p' | tail -1)"
[[ -n "$shell_payload" ]] || {
	printf 'the shell printed no sample\n' >&2
	tr -d '\r' <"$work_dir/node.log" | tail -30 >&2
	exit 1
}

"$python" "$KFSW_REPO_DIR/tools/ground/hk-bridge.py" \
	--device "$pty" --node 1 --report 0 --count 1 --once --yamcs none \
	>"$work_dir/bridge.log" 2>&1 || {
	printf 'the bridge failed\n' >&2
	cat "$work_dir/bridge.log" >&2
	exit 1
}

bridge_frame="$(sed -n 's/^  \([0-9a-f]\{2,\}\)$/\1/p' "$work_dir/bridge.log" | tail -1)"
[[ -n "$bridge_frame" ]] || {
	printf 'the bridge returned no sample\n' >&2
	cat "$work_dir/bridge.log" >&2
	exit 1
}

echo "HK YAMCS BRIDGE SMOKE"

# The shell prints the payload only; the bridge forwards the whole frame, so
# its first ten bytes are the header the shell reports in words.
check "the bridge and the shell read the same values" \
	"${bridge_frame:20}" "$shell_payload"
check "the frame carries the protocol version" "${bridge_frame:0:2}" "01"
report="$(awk '/^report:/ {print $2}' "$definition")"
check "the frame names the report the file defines" \
	"${bridge_frame:2:2}" "$(printf '%02x' "$report")"

wanted_bytes=$(( (10 + ${#shell_payload} / 2) * 2 ))
check "the frame is header plus values" "${#bridge_frame}" "$wanted_bytes"

if grep -q 'incomplete' "$work_dir/bridge.log"; then
	printf '  [FAIL] the sample was flagged incomplete\n' >&2
	failures=$((failures + 1))
else
	printf '  [ok]   every value was read\n'
fi

if [[ "$failures" -eq 0 ]]; then
	echo "HK YAMCS BRIDGE SMOKE RESULT: PASS"
else
	echo "HK YAMCS BRIDGE SMOKE RESULT: FAIL ($failures)"
	sed -n '1,40p' "$work_dir/bridge.log" >&2
	exit 1
fi
