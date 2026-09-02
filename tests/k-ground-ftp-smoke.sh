#!/usr/bin/env bash
set -Eeuo pipefail

# Software round trip of one file between the two configured ground roles.
# kfsw-ops (node 19) uploads test.txt to kfsw-gnd-uhf (node 16), reads its
# metadata back, downloads it again, and compares the two local copies.

KGROUND_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KGROUND_TESTS_DIR="$(dirname "$KGROUND_TEST")"
KGROUND_REPO_DIR="$(dirname "$KGROUND_TESTS_DIR")"
KGROUND_WORKSPACE_ROOT="$(dirname "$KGROUND_REPO_DIR")"
KGROUND_BUILD_ROOT="${KGROUND_BUILD_ROOT:-$KGROUND_WORKSPACE_ROOT/build/k-ground}"

if [[ $# -ne 0 ]]; then
	echo "ERROR: unknown argument: $1"
	exit 1
fi

work_dir="$(mktemp -d /tmp/k-ground-ftp.XXXXXX)"
station_dir="$work_dir/ground-station"
node16_pid=""
node19_pid=""
bridge_pid=""

cleanup()
{
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$node16_pid" ]] && kill "$node16_pid" 2>/dev/null || true
	[[ -n "$node19_pid" ]] && kill "$node19_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 4>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "K-GROUND FTP RESULT: FAIL"
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

	for _ in {1..600}; do
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
printf '%s\n' "KFSW_CSP_ROUTES='16/14 KISS'" \
	>>"$station_dir/nodes/kfsw-ops.env"

KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf
KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-ops

node16_executable="$KGROUND_BUILD_ROOT/node-16/zephyr/zephyr.exe"
node19_executable="$KGROUND_BUILD_ROOT/node-19/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node19_executable" ]] || fail "node 19 executable is missing"

for node_config in node-16 node-19; do
	grep -Fq 'CONFIG_KFSW_FTP=y' \
		"$KGROUND_BUILD_ROOT/$node_config/zephyr/.config" || \
		fail "$node_config did not compose the file-transfer service"
done

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

printf '%s\n' 'csp ping 19' >&3
wait_for_output "$work_dir/node16.log" "CSP ping 19: success" "$node16_pid" || \
	fail "node 16 could not ping node 19"

# The operator node drives the whole round trip.
printf '%s\n' \
	'ftp generate /build/test.txt 256' \
	'ftp 16 mkdir /uplink' \
	'ftp put 16 /build/test.txt /uplink/test.txt' \
	'ftp stat 16 /uplink/test.txt' \
	'ftp 16 ls /uplink' \
	'ftp get 16 /uplink/test.txt /build/test-returned.txt' \
	'ftp verify /build/test.txt /build/test-returned.txt' \
	'ftp get 16 /uplink/missing.txt /build/missing.txt' \
	>&4

wait_for_output "$work_dir/node19.log" \
	"FTP verify first=/build/test.txt second=/build/test-returned.txt: PASS" \
	"$node19_pid" || fail "the uploaded and downloaded copies did not match"
wait_for_output "$work_dir/node19.log" \
	"FTP get node=16 path=/uplink/missing.txt: not found" \
	"$node19_pid" || fail "a missing remote file was not reported as not found"

# The receiving node sees the committed file in its own FTP root.
printf '%s\n' 'ftp 16 ls /uplink' 'ftp stat 16 /uplink/test.txt' >&3
wait_for_output "$work_dir/node16.log" "FTP list: PASS entries=1" \
	"$node16_pid" || fail "node 16 does not list the received file locally"

node19_expected=(
	'FTP generate path=/build/test.txt: PASS bytes=256'
	'FTP mkdir node=16 path=/uplink: PASS'
	'FTP put node=16 source=/build/test.txt destination=/uplink/test.txt: PASS bytes=256'
	'FTP stat node=16 path=/uplink/test.txt type=file bytes=256'
	'f        256 test.txt'
	'FTP get node=16 source=/uplink/test.txt destination=/build/test-returned.txt: PASS bytes=256'
)
node16_expected=(
	'FTP stat node=16 path=/uplink/test.txt type=file bytes=256'
	'f        256 test.txt'
)

for expected in "${node19_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node19.log" || \
		fail "node 19 output is missing: $expected"
done
for expected in "${node16_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node16.log" || \
		fail "node 16 output is missing: $expected"
done

# The same CRC must appear on both nodes and on both local copies.
uploaded_crc="$(sed -n 's/.*FTP generate path=\/build\/test\.txt: PASS bytes=256 crc32=\([0-9a-f]*\).*/\1/p' \
	"$work_dir/node19.log" | head -1)"
[[ -n "$uploaded_crc" ]] || fail "the generated file did not report a CRC"
grep -Fq "crc32=$uploaded_crc" "$work_dir/node16.log" || \
	fail "node 16 reports a different CRC than node 19 generated"

cat "$work_dir/node19.log"
cat "$work_dir/node16.log"
echo "K-GROUND FTP RESULT: PASS crc32=$uploaded_crc bytes=256"
