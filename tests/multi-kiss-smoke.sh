#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(readlink -f "$(dirname "$0")/..")"
build_root="${KFSW_MULTI_KISS_BUILD_ROOT:-$(dirname "$repo_dir")/build/tests/multi-kiss}"
work_dir="$(mktemp -d /tmp/kfsw-multi-kiss.XXXXXX)"
router_pid=""
node_a_pid=""
node_b_pid=""
bridge_a_pid=""
bridge_b_pid=""

cleanup()
{
	for pid in "$bridge_a_pid" "$bridge_b_pid" "$router_pid" "$node_a_pid" "$node_b_pid"; do
		[[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
	done
	wait 2>/dev/null || true
	exec 3>&- 4>&- 5>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "MULTI-KISS RESULT: FAIL"
	echo "  $1"
	for log_file in router.log node-a.log node-b.log bridge-a.log bridge-b.log; do
		if [[ -s "$work_dir/$log_file" ]]; then
			echo "--- $log_file ---"
			sed -n '1,260p' "$work_dir/$log_file"
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

last_interface_line()
{
	local name="$1"
	grep -F "$name addr=" "$work_dir/router.log" | tail -1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

command -v socat >/dev/null 2>&1 || fail "socat is required"

for executable in router node-a node-b; do
	[[ -x "$build_root/$executable/zephyr/zephyr.exe" ]] || {
		echo "MULTI-KISS: building software topology"
		"$repo_dir/tests/build-multi-kiss.sh"
		break
	}
done

mkfifo "$work_dir/router.in" "$work_dir/node-a.in" "$work_dir/node-b.in"
exec 3<>"$work_dir/router.in"
exec 4<>"$work_dir/node-a.in"
exec 5<>"$work_dir/node-b.in"

"$build_root/router/zephyr/zephyr.exe" --uart_stdinout --device_id=9 --no-color \
	-flash="$work_dir/router-flash.bin" -flash_erase -flash_rm \
	<&3 >"$work_dir/router.log" 2>&1 &
router_pid=$!
"$build_root/node-a/zephyr/zephyr.exe" --uart_stdinout --device_id=10 --no-color \
	-flash="$work_dir/node-a-flash.bin" -flash_erase -flash_rm \
	<&4 >"$work_dir/node-a.log" 2>&1 &
node_a_pid=$!
"$build_root/node-b/zephyr/zephyr.exe" --uart_stdinout --device_id=11 --no-color \
	-flash="$work_dir/node-b-flash.bin" -flash_erase -flash_rm \
	<&5 >"$work_dir/node-b.log" 2>&1 &
node_b_pid=$!

wait_for_output "$work_dir/router.log" "@READY " "$router_pid" || fail "router did not start"
wait_for_output "$work_dir/node-a.log" "@READY " "$node_a_pid" || fail "node A did not start"
wait_for_output "$work_dir/node-b.log" "@READY " "$node_b_pid" || fail "node B did not start"
wait_for_output "$work_dir/router.log" "uart_1 connected to pseudotty: " "$router_pid" || \
	fail "router did not expose uart_1"
wait_for_output "$work_dir/router.log" "uart_2 connected to pseudotty: " "$router_pid" || \
	fail "router did not expose uart_2"
wait_for_output "$work_dir/node-a.log" "uart_1 connected to pseudotty: " "$node_a_pid" || \
	fail "node A did not expose uart_1"
wait_for_output "$work_dir/node-b.log" "uart_1 connected to pseudotty: " "$node_b_pid" || \
	fail "node B did not expose uart_1"

router_pty_1="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/router.log" | head -1)"
router_pty_2="$(sed -n 's/^uart_2 connected to pseudotty: //p' "$work_dir/router.log" | head -1)"
node_a_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/node-a.log" | head -1)"
node_b_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work_dir/node-b.log" | head -1)"

socat -d -d "$router_pty_1,raw,echo=0" "$node_a_pty,raw,echo=0" \
	>"$work_dir/bridge-a.log" 2>&1 &
bridge_a_pid=$!
socat -d -d "$router_pty_2,raw,echo=0" "$node_b_pty,raw,echo=0" \
	>"$work_dir/bridge-b.log" 2>&1 &
bridge_b_pid=$!
wait_for_output "$work_dir/bridge-a.log" "starting data transfer loop" "$bridge_a_pid" || \
	fail "KISS_1 bridge did not start"
wait_for_output "$work_dir/bridge-b.log" "starting data transfer loop" "$bridge_b_pid" || \
	fail "KISS_2 bridge did not start"

printf '%s\n' 'csp interfaces' 'csp routes' 'csp ping 10' 'uart test 10' \
	'csp ping 11' 'uart test 11' >&3
wait_for_output "$work_dir/router.log" "CSP ping 10: success" "$router_pid" || \
	fail "router traffic to node A over KISS_1 failed"
wait_for_output "$work_dir/router.log" "interface: KISS_1" "$router_pid" || \
	fail "route to node A did not select KISS_1"
wait_for_output "$work_dir/router.log" "CSP ping 11: success" "$router_pid" || \
	fail "router traffic to node B over KISS_2 failed"
wait_for_output "$work_dir/router.log" "interface: KISS_2" "$router_pid" || \
	fail "route to node B did not select KISS_2"

# Exercise real forwarding in both directions, not only router-originated traffic.
printf '%s\n' 'csp ping 11' >&4
printf '%s\n' 'csp ping 10' >&5
wait_for_output "$work_dir/node-a.log" "CSP ping 11: success" "$node_a_pid" || \
	fail "node A -> router -> node B transit failed"
wait_for_output "$work_dir/node-b.log" "CSP ping 10: success" "$node_b_pid" || \
	fail "node B -> router -> node A transit failed"

printf '%s\n' 'csp interfaces' 'csp routes' 'uart info' >&3
wait_for_output "$work_dir/router.log" "UART transport: KISS_2" "$router_pid" || \
	fail "UART diagnostics collapsed the two interfaces"
sleep 0.1

grep -Fq "10/14 -> KISS_1 direct" "$work_dir/router.log" || \
	fail "KISS_1 route is missing"
grep -Fq "11/14 -> KISS_2 via 11" "$work_dir/router.log" || \
	fail "KISS_2 VIA route is missing or lost its next hop"

kiss_1_stats="$(last_interface_line KISS_1)"
kiss_2_stats="$(last_interface_line KISS_2)"
grep -Eq 'KISS_1 addr=8/14 .*tx=[1-9][0-9]* rx=[1-9][0-9]*' <<<"$kiss_1_stats" || \
	fail "KISS_1 does not have independent nonzero counters"
grep -Eq 'KISS_2 addr=9/14 .*tx=[1-9][0-9]* rx=[1-9][0-9]*' <<<"$kiss_2_stats" || \
	fail "KISS_2 does not have independent nonzero counters"

echo "$kiss_1_stats"
echo "$kiss_2_stats"
echo "MULTI-KISS RESULT: PASS direct_kiss_1=yes direct_kiss_2=yes via=preserved transit=bidirectional"
