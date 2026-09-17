#!/usr/bin/env bash
# Two Linux nodes talking CSP over a virtual CAN bus: ping both ways, a remote
# parameter read, and the libcsp counters afterwards.
#
# Needs vcan0. Create it once with: sudo k-fsw/tests/vcan-up.sh
set -Eeuo pipefail

KFSW_CAN_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_CAN_TEST")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

interface="${KFSW_CAN_INTERFACE:-vcan0}"
node2_build_dir="$KFSW_WORKSPACE_ROOT/build/tests/linux-can"
node16_build_dir="$KFSW_WORKSPACE_ROOT/build/k-ground/node-16"
node2_executable="$node2_build_dir/zephyr/zephyr.exe"
node16_executable="$node16_build_dir/zephyr/zephyr.exe"

mode="smoke"
if [[ "${1:-}" == "--terminal" ]]; then
	mode="terminal"
	shift
fi
if [[ $# -ne 0 ]]; then
	echo "ERROR: unknown argument: $1"
	exit 1
fi

work_dir="$(mktemp -d /tmp/kfsw-can-smoke.XXXXXX)"
node2_pid=""
node16_pid=""
relay_pid=""

cleanup()
{
	[[ -n "$relay_pid" ]] && kill "$relay_pid" 2>/dev/null || true
	[[ -n "$node2_pid" ]] && kill "$node2_pid" 2>/dev/null || true
	[[ -n "$node16_pid" ]] && kill "$node16_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 4>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "CAN RESULT: FAIL"
	echo "  $1"

	for log_file in node2.log node16.log; do
		if [[ -s "$work_dir/$log_file" ]]; then
			echo "--- $log_file ---"
			sed -n '1,200p' "$work_dir/$log_file"
		fi
	done

	exit 1
}

wait_for_output()
{
	local file="$1"
	local expected="$2"
	local process_pid="$3"

	for _ in {1..300}; do
		grep -Fq "$expected" "$file" 2>/dev/null && return 0
		kill -0 "$process_pid" 2>/dev/null || return 1
		sleep 0.05
	done

	return 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

ip link show "$interface" >/dev/null 2>&1 || \
	fail "$interface does not exist; create it with: sudo $KFSW_TESTS_DIR/vcan-up.sh"

if [[ ! -x "$node2_executable" ]]; then
	echo "CAN SMOKE: building node 2"
	KFSW_BUILD_DIR="$node2_build_dir" \
		KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/linux-can.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$KFSW_TESTS_DIR/config/linux-can.overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" linux
fi

if [[ ! -x "$node16_executable" ]]; then
	echo "CAN SMOKE: building node 16"
	"$KFSW_REPO_DIR/tools/k-ground" build kfsw-gnd-can
fi

mkfifo "$work_dir/node2.in" "$work_dir/node16.in"
exec 3<>"$work_dir/node2.in"
exec 4<>"$work_dir/node16.in"

"$node2_executable" --uart_stdinout --device_id=2 --no-color \
	--can-if="$interface" -flash="$work_dir/node2-flash.bin" \
	<&3 >"$work_dir/node2.log" 2>&1 &
node2_pid=$!

"$node16_executable" --uart_stdinout --device_id=16 --no-color \
	--can-if="$interface" -flash="$work_dir/node16-flash.bin" \
	<&4 >"$work_dir/node16.log" 2>&1 &
node16_pid=$!

wait_for_output "$work_dir/node2.log" "@READY " "$node2_pid" || \
	fail "node 2 did not report readiness"
wait_for_output "$work_dir/node16.log" "@READY " "$node16_pid" || \
	fail "node 16 did not report readiness"

if [[ "$mode" == "terminal" ]]; then
	tail -n 0 -F "$work_dir/node16.log" &
	relay_pid=$!
	echo "KFSW CAN TERMINAL: READY (interactive node 16; node 2 is on $interface)"
	while IFS= read -r shell_command; do
		printf '%s\n' "$shell_command" >&4
	done
	exit 0
fi

printf '%s\n' \
	'csp ping 16' \
	'csp interfaces' \
	'csp counters' >&3

printf '%s\n' \
	'csp ping 2' \
	'csp ident 2' \
	'param get 2 uid' \
	'param get 2 node_id' \
	'csp interfaces' \
	'csp counters' >&4

wait_for_output "$work_dir/node16.log" "CSP ping 2: success" "$node16_pid" || \
	fail "node 16 could not ping node 2 over $interface"
wait_for_output "$work_dir/node2.log" "CSP ping 16: success" "$node2_pid" || \
	fail "node 2 could not ping node 16 over $interface"
wait_for_output "$work_dir/node16.log" "hostname: kfsw-can-2" "$node16_pid" || \
	fail "the remote identity did not arrive over $interface"
wait_for_output "$work_dir/node16.log" '2:uid = ' "$node16_pid" || \
	fail "a remote string parameter was not readable over $interface"
wait_for_output "$work_dir/node16.log" "2:node_id = 2" "$node16_pid" || \
	fail "a remote scalar parameter was not readable over $interface"

# The CAN interface has to show traffic in both directions on both nodes.
for log_file in node2.log node16.log; do
	if ! grep -Eq '^CAN addr=[0-9]+/[0-9]+ default=(yes|no) tx=[1-9][0-9]* rx=[1-9][0-9]*' \
		"$work_dir/$log_file"; then
		fail "${log_file%.log} did not carry traffic on its CAN interface"
	fi
done

# libcsp's own counters, which is what a framing or buffer problem shows up in.
for log_file in node2.log node16.log; do
	grep -Fq "last_can_error=0 (none)" "$work_dir/$log_file" || \
		fail "${log_file%.log} reported a CAN framing error"
	grep -Fq "buffer_out=0 conn_out=0 conn_ovf=0 conn_noroute=0 invalid_reply=0" \
		"$work_dir/$log_file" || \
		fail "${log_file%.log} reported a libcsp error counter"
done

cat "$work_dir/node2.log"
cat "$work_dir/node16.log"
echo "CAN RESULT: PASS"
