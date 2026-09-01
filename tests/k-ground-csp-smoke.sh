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
station_dir="$work_dir/ground-station"
node16_pid=""
node19_pid=""
bridge_pid=""
relay_pid=""

cleanup()
{
	[[ -n "$relay_pid" ]] && kill "$relay_pid" 2>/dev/null || true
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$node16_pid" ]] && kill "$node16_pid" 2>/dev/null || true
	[[ -n "$node19_pid" ]] && kill "$node19_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 4>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "K-GROUND CSP RESULT: FAIL"
	echo "  $1"

	for log_file in node16.log node19.log socat.log; do
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

mkdir -p "$station_dir/nodes"
cp "$KGROUND_REPO_DIR/ground-station/nodes/kfsw-gnd-uhf.env" \
	"$station_dir/nodes/kfsw-gnd-uhf.env"
cp "$KGROUND_REPO_DIR/ground-station/nodes/kfsw-ops.env" \
	"$station_dir/nodes/kfsw-ops.env"
printf '%s\n' "KFSW_CSP_ROUTES='19/14 KISS'" \
	>>"$station_dir/nodes/kfsw-gnd-uhf.env"

KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf
KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-ops

node16_executable="$KGROUND_BUILD_ROOT/node-16/zephyr/zephyr.exe"
node19_executable="$KGROUND_BUILD_ROOT/node-19/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node19_executable" ]] || fail "node 19 executable is missing"
grep -Fq 'CONFIG_KFSW_RADIO_UHF_HOLYBRO=y' \
	"$KGROUND_BUILD_ROOT/node-16/zephyr/.config" || \
	fail "node 16 did not compose the Holybro UHF module"
grep -Fq 'CONFIG_KFSW_CSP_ROUTE_TABLE="19/14 KISS"' \
	"$KGROUND_BUILD_ROOT/node-16/zephyr/.config" || \
	fail "node 16 did not compose its configured CSP route table"
grep -Fq '# CONFIG_KFSW_RADIO_UHF is not set' \
	"$KGROUND_BUILD_ROOT/node-19/zephyr/.config" || \
	fail "node 19 unexpectedly owns the UHF radio module"

mkfifo "$work_dir/node16.in" "$work_dir/node19.in"
exec 3<>"$work_dir/node16.in"
exec 4<>"$work_dir/node19.in"

"$node16_executable" --uart_stdinout --device_id=16 --no-color \
	-flash="$work_dir/node16-flash.bin" \
	<&3 >"$work_dir/node16.log" 2>&1 &
node16_pid=$!

"$node19_executable" --uart_stdinout --device_id=19 --no-color \
	-flash="$work_dir/node19-flash.bin" \
	<&4 >"$work_dir/node19.log" 2>&1 &
node19_pid=$!

wait_for_output "$work_dir/node16.log" "@READY " "$node16_pid" || \
	fail "node 16 did not report readiness"
wait_for_output "$work_dir/node19.log" "@READY " "$node19_pid" || \
	fail "node 19 did not report readiness"
wait_for_output "$work_dir/node16.log" "kfsw-gnd-uhf# " "$node16_pid" || \
	fail "node 16 did not expose its role prompt"
wait_for_output "$work_dir/node19.log" "kfsw-ops# " "$node19_pid" || \
	fail "node 19 did not expose its role prompt"
wait_for_output "$work_dir/node16.log" \
	"uart_1 connected to pseudotty: " "$node16_pid" || \
	fail "node 16 did not expose its CSP UART"
wait_for_output "$work_dir/node19.log" \
	"uart_1 connected to pseudotty: " "$node19_pid" || \
	fail "node 19 did not expose its CSP UART"

node16_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node16.log" | head -1)"
node19_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node19.log" | head -1)"

socat -d -d "$node16_pty,raw,echo=0" "$node19_pty,raw,echo=0" \
	>"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
	"$bridge_pid" || fail "the ground CSP UART bridge did not become ready"

if [[ "$mode" == "terminal" ]]; then
	tail -n 0 -F "$work_dir/node19.log" &
	relay_pid=$!
	echo "K-GROUND PAIR: READY (interactive kfsw-ops node 19; kfsw-gnd-uhf node 16 is linked)"
	while IFS= read -r shell_command; do
		printf '%s\n' "$shell_command" >&4
	done
	exit 0
fi

printf '%s\n' 'status' 'version' 'uhf status' 'csp info' 'csp routes' \
	'csp ping 19' >&3
printf '%s\n' 'status' 'version' 'csp info' 'csp ping 16' >&4

wait_for_output "$work_dir/node16.log" "CSP ping 19: success" \
	"$node16_pid" || fail "node 16 could not ping node 19"
wait_for_output "$work_dir/node19.log" "CSP ping 16: success" \
	"$node19_pid" || fail "node 19 could not ping node 16"

node16_expected=(
	"Role: kfsw-gnd-uhf"
	"Name: kfsw-gnd-uhf"
	"CSP node: 16"
	"hostname: kfsw-gnd-uhf"
	"19/14 -> KISS direct"
	"implementation: holybro-sik"
	"expected serial: 57600 8N1"
	"RF link: unknown"
	"kfsw-gnd-uhf# "
)
node19_expected=(
	"Role: kfsw-ops"
	"Name: kfsw-ops"
	"CSP node: 19"
	"hostname: kfsw-ops"
	"kfsw-ops# "
)

for expected in "${node16_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node16.log" || \
		fail "node 16 output is missing: $expected"
done
for expected in "${node19_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node19.log" || \
		fail "node 19 output is missing: $expected"
done

cat "$work_dir/node16.log"
cat "$work_dir/node19.log"
echo "K-GROUND CSP RESULT: PASS"
