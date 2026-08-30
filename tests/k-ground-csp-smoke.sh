#!/usr/bin/env bash
set -Eeuo pipefail

KGROUND_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KGROUND_TESTS_DIR="$(dirname "$KGROUND_TEST")"
KGROUND_REPO_DIR="$(dirname "$KGROUND_TESTS_DIR")"
KGROUND_WORKSPACE_ROOT="$(dirname "$KGROUND_REPO_DIR")"
KGROUND_BUILD_ROOT="${KGROUND_BUILD_ROOT:-$KGROUND_WORKSPACE_ROOT/build/k-ground}"

mode="smoke"
if [[ "${1:-}" == "--terminal" ]]; then
	mode="terminal"
	shift
fi
if [[ $# -ne 0 ]]; then
	echo "ERROR: unknown argument: $1"
	exit 1
fi

work_dir="$(mktemp -d /tmp/k-ground-csp.XXXXXX)"
node16_pid=""
node17_pid=""
bridge_pid=""
relay_pid=""

cleanup()
{
	[[ -n "$relay_pid" ]] && kill "$relay_pid" 2>/dev/null || true
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$node16_pid" ]] && kill "$node16_pid" 2>/dev/null || true
	[[ -n "$node17_pid" ]] && kill "$node17_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 4>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "K-GROUND CSP RESULT: FAIL"
	echo "  $1"

	for log_file in node16.log node17.log socat.log; do
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

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

command -v socat >/dev/null 2>&1 || fail "socat is required"

"$KGROUND_REPO_DIR/tools/k-ground" build --node 16 --name main --peer 17
"$KGROUND_REPO_DIR/tools/k-ground" build --node 17 --name peer --peer 16

node16_executable="$KGROUND_BUILD_ROOT/node-16/zephyr/zephyr.exe"
node17_executable="$KGROUND_BUILD_ROOT/node-17/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node17_executable" ]] || fail "node 17 executable is missing"

mkfifo "$work_dir/node16.in" "$work_dir/node17.in"
exec 3<>"$work_dir/node16.in"
exec 4<>"$work_dir/node17.in"

"$node16_executable" --uart_stdinout --device_id=16 --no-color \
	-flash="$work_dir/node16-flash.bin" \
	<&3 >"$work_dir/node16.log" 2>&1 &
node16_pid=$!

"$node17_executable" --uart_stdinout --device_id=17 --no-color \
	-flash="$work_dir/node17-flash.bin" \
	<&4 >"$work_dir/node17.log" 2>&1 &
node17_pid=$!

wait_for_output "$work_dir/node16.log" "@READY " "$node16_pid" || \
	fail "node 16 did not report readiness"
wait_for_output "$work_dir/node17.log" "@READY " "$node17_pid" || \
	fail "node 17 did not report readiness"
wait_for_output "$work_dir/node16.log" "k-ground# " "$node16_pid" || \
	fail "node 16 did not expose the ground prompt"
wait_for_output "$work_dir/node17.log" "k-ground# " "$node17_pid" || \
	fail "node 17 did not expose the ground prompt"
wait_for_output "$work_dir/node16.log" \
	"uart_1 connected to pseudotty: " "$node16_pid" || \
	fail "node 16 did not expose its CSP UART"
wait_for_output "$work_dir/node17.log" \
	"uart_1 connected to pseudotty: " "$node17_pid" || \
	fail "node 17 did not expose its CSP UART"

node16_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node16.log" | head -1)"
node17_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node17.log" | head -1)"

socat -d -d "$node16_pty,raw,echo=0" "$node17_pty,raw,echo=0" \
	>"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
	"$bridge_pid" || fail "the ground CSP UART bridge did not become ready"

if [[ "$mode" == "terminal" ]]; then
	tail -n 0 -F "$work_dir/node16.log" &
	relay_pid=$!
	echo "K-GROUND PAIR: READY (interactive node 16/main; peer 17 is linked)"
	while IFS= read -r shell_command; do
		printf '%s\n' "$shell_command" >&3
	done
	exit 0
fi

printf '%s\n' 'status' 'version' 'csp info' 'csp ping 17' >&3
printf '%s\n' 'status' 'version' 'csp info' 'csp ping 16' >&4

wait_for_output "$work_dir/node16.log" "CSP ping 17: success" \
	"$node16_pid" || fail "node 16 could not ping node 17"
wait_for_output "$work_dir/node17.log" "CSP ping 16: success" \
	"$node17_pid" || fail "node 17 could not ping node 16"

node16_expected=(
	"Role: ground"
	"Name: main"
	"CSP node: 16"
	"hostname: k-ground-main"
	"k-ground# "
)
node17_expected=(
	"Role: ground"
	"Name: peer"
	"CSP node: 17"
	"hostname: k-ground-peer"
	"k-ground# "
)

for expected in "${node16_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node16.log" || \
		fail "node 16 output is missing: $expected"
done
for expected in "${node17_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node17.log" || \
		fail "node 17 output is missing: $expected"
done

cat "$work_dir/node16.log"
cat "$work_dir/node17.log"
echo "K-GROUND CSP RESULT: PASS"
