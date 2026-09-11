#!/usr/bin/env bash
# Checks that a node beacons on its own and that the ground hears it.
#
# Every other housekeeping test asks for a sample. This one never does: the
# bridge runs in --listen, which binds nothing and transmits nothing, so the
# only thing that can put a frame in front of it is the node deciding to speak.
#
# That distinction is the whole feature, and it is also where the ports matter.
# A beacon is sent *from* the serving port, because a listener recognises
# housekeeping by the port a frame came from; it is sent *to* a port nothing
# binds, so it can never be mistaken for a request by another node. Get either
# backwards and this test sees silence.
#
# Software only. The node runs hosted and the bridge talks to its uart_1
# pseudo-terminal.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
python="$KFSW_ROOT/.venv/bin/python"
work_dir="$(mktemp -d /tmp/kfsw-hk-beacon.XXXXXX)"
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
	printf 'no hosted image at %s\n' "$executable" >&2
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

echo "HK BEACON SMOKE"

# Two local values, collected once a second, so a beacon always has something
# newer than the last one to carry.
printf '%s\n' 'hk define 0 1:0 3:0' 'hk period 0 1000' >&3
sleep 2

# The floor exists so an operator cannot turn a node into a transmitter that
# swamps its own link. -34 is -ERANGE.
printf '%s\n' 'hk beacon 0 16 250' >&3
sleep 1
if tr -d '\r' <"$work_dir/node.log" | grep -q 'beacon for report 0: -34'; then
	printf '  [ok]   an interval under the floor is refused\n'
else
	printf '  [FAIL] an interval under the floor was accepted\n' >&2
	failures=$((failures + 1))
fi

printf '%s\n' 'hk beacon 0 16 5000' >&3
sleep 1

# Nothing here asks. --listen binds no port and sends no packet; the frames it
# reports can only be ones the node sent unprompted.
"$python" "$KFSW_REPO_DIR/tools/ground/hk-bridge.py" \
	--device "$pty" --node 1 --listen --once --timeout 20 --yamcs none \
	>"$work_dir/bridge.log" 2>&1 || {
	printf 'the bridge failed\n' >&2
	cat "$work_dir/bridge.log" >&2
	exit 1
}

beacon_frame="$(sed -n 's/^  \([0-9a-f]\{2,\}\)$/\1/p' "$work_dir/bridge.log" | tail -1)"
[[ -n "$beacon_frame" ]] || {
	printf '  [FAIL] the ground heard nothing the node sent on its own\n' >&2
	cat "$work_dir/bridge.log" >&2
	exit 1
}
printf '  [ok]   the ground heard a node that was never asked\n'

check "the beacon carries the protocol version" "${beacon_frame:0:2}" "01"
check "the beacon names the report" "${beacon_frame:2:2}" "00"

# A beacon is the request path's frame, not a second format: same ten-byte
# header, same two values behind it.
check "the beacon is header plus the report's values" "${#beacon_frame}" "32"

heard="$(grep -c '^report 0 seq' "$work_dir/bridge.log" || true)"
if [[ "$heard" -ge 2 ]]; then
	printf '  [ok]   beacons keep coming (%s heard)\n' "$heard"
else
	printf '  [FAIL] only %s beacon arrived; one is not a period\n' "$heard" >&2
	failures=$((failures + 1))
fi

# The counters are what an operator has when the ground goes quiet.
printf '%s\n' 'hk show' >&3
sleep 2
sent="$(tr -d '\r' <"$work_dir/node.log" | sed -n 's/^beacons sent: //p' | tail -1)"
if [[ -n "$sent" ]] && [[ "$sent" -ge "$heard" ]]; then
	printf '  [ok]   the node counted what it sent (%s)\n' "$sent"
else
	printf '  [FAIL] the node reports %s sent against %s heard\n' "${sent:-none}" "$heard" >&2
	failures=$((failures + 1))
fi

# Turning it off has to be as reachable as turning it on.
printf '%s\n' 'hk beacon 0 16 0' >&3
sleep 1
if tr -d '\r' <"$work_dir/node.log" | grep -q 'report 0 stops beaconing'; then
	printf '  [ok]   an operator can take it away\n'
else
	printf '  [FAIL] the beacon could not be stopped\n' >&2
	failures=$((failures + 1))
fi

if [[ "$failures" -eq 0 ]]; then
	echo "HK BEACON SMOKE RESULT: PASS"
else
	echo "HK BEACON SMOKE RESULT: FAIL ($failures)"
	sed -n '1,40p' "$work_dir/bridge.log" >&2
	exit 1
fi
