#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

mode="smoke"

if [[ "${1:-}" == "--terminal" ]]; then
    mode="terminal"
    shift
fi

if [[ $# -ne 0 ]]; then
    echo "ERROR: unknown argument: $1"
    exit 1
fi

node1_executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
node2_executable="$KFSW_ROOT/build/linux_node2/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-csp-smoke.XXXXXX)"
node1_pid=""
node2_pid=""
bridge_pid=""
relay_pid=""

cleanup()
{
    [[ -n "$relay_pid" ]] && kill "$relay_pid" 2>/dev/null || true
    [[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
    [[ -n "$node1_pid" ]] && kill "$node1_pid" 2>/dev/null || true
    [[ -n "$node2_pid" ]] && kill "$node2_pid" 2>/dev/null || true

    wait 2>/dev/null || true
    exec 3>&- 4>&-
    rm -rf "$work_dir"
}

fail()
{
    echo "CSP RESULT: FAIL"
    echo "  $1"

    for log_file in node1.log node2.log socat.log; do
        if [[ -s "$work_dir/$log_file" ]]; then
            echo "--- $log_file ---"
            cat "$work_dir/$log_file"
        fi
    done

    exit 1
}

wait_for_output()
{
    local file="$1"
    local expected="$2"
    local process_pid="$3"

    for _ in {1..200}; do
        if grep -Fq "$expected" "$file" 2>/dev/null; then
            return 0
        fi

        if ! kill -0 "$process_pid" 2>/dev/null; then
            return 1
        fi

        sleep 0.05
    done

    return 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if ! command -v socat >/dev/null 2>&1; then
    fail "socat is required to bridge the two native_sim CSP UARTs"
fi

if [[ ! -x "$node1_executable" ]]; then
    echo "CSP SMOKE: building node 1"
    "$KFSW_ROOT/k-fsw/tools/build.sh" linux
fi

if [[ ! -x "$node2_executable" ]]; then
    echo "CSP SMOKE: building node 2"
    "$KFSW_ROOT/k-fsw/tools/build.sh" linux_node2
fi

mkfifo "$work_dir/node1.in" "$work_dir/node2.in"
exec 3<>"$work_dir/node1.in"
exec 4<>"$work_dir/node2.in"

"$node1_executable" --uart_stdinout --device_id=1 --no-color \
	-flash="$work_dir/node1-flash.bin" \
    <&3 >"$work_dir/node1.log" 2>&1 &
node1_pid=$!

"$node2_executable" --uart_stdinout --device_id=2 --no-color \
	-flash="$work_dir/node2-flash.bin" \
    <&4 >"$work_dir/node2.log" 2>&1 &
node2_pid=$!

wait_for_output "$work_dir/node1.log" "@READY " "$node1_pid" || \
    fail "node 1 did not report readiness"
wait_for_output "$work_dir/node2.log" "@READY " "$node2_pid" || \
    fail "node 2 did not report readiness"
wait_for_output "$work_dir/node1.log" \
    "uart_1 connected to pseudotty: " "$node1_pid" || \
    fail "node 1 did not expose its CSP UART"
wait_for_output "$work_dir/node2.log" \
    "uart_1 connected to pseudotty: " "$node2_pid" || \
    fail "node 2 did not expose its CSP UART"

node1_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
    "$work_dir/node1.log" | head -1)"
node2_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
    "$work_dir/node2.log" | head -1)"

socat -d -d "$node1_pty,raw,echo=0" "$node2_pty,raw,echo=0" \
    >"$work_dir/socat.log" 2>&1 &
bridge_pid=$!

wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
    "$bridge_pid" || fail "the CSP UART bridge did not become ready"

if [[ "$mode" == "terminal" ]]; then
    tail -n 0 -F "$work_dir/node1.log" &
    relay_pid=$!

    echo "KFSW CSP TERMINAL: READY"
    while IFS= read -r command; do
        printf '%s\n' "$command" >&3
    done

    exit 0
fi

printf 'kfsw csp \t\n' >&3
printf 'kfsw uart \t\n' >&3
printf '%s\n' \
    'kfsw csp info' \
    'kfsw csp interfaces' \
    'kfsw csp routes' \
    'kfsw csp ping 2' \
    'kfsw param list 2' \
    'kfsw param get 2 test_u32' \
    'kfsw param set 2 test_u32 1234' \
    'kfsw param get 2 test_u32' \
    'kfsw param get 2 missing' \
    'kfsw param set 2 node_id 7' \
	'kfsw ftp generate /build/empty.bin 0' \
	'kfsw ftp generate /build/single.bin 128' \
	'kfsw ftp generate /build/multi.bin 1024' \
	'kfsw ftp generate /build/large.bin 8192' \
	'ftp 2 mkdir /flash' \
	'kfsw ftp put 2 /build/empty.bin /flash/empty.bin' \
	'kfsw ftp put 2 /build/single.bin /flash/single.bin' \
	'kfsw ftp put 2 /build/multi.bin /flash/multi.bin' \
	'kfsw ftp put 2 /build/large.bin /flash/large.bin' \
	'kfsw ftp stat 2 /flash/large.bin' \
	'ftp 2 ls /flash' \
	'kfsw ftp get 2 /flash/empty.bin /build/empty-returned.bin' \
	'kfsw ftp get 2 /flash/single.bin /build/single-returned.bin' \
	'kfsw ftp get 2 /flash/multi.bin /build/multi-returned.bin' \
	'kfsw ftp get 2 /flash/large.bin /build/large-returned.bin' \
	'kfsw ftp verify /build/empty.bin /build/empty-returned.bin' \
	'kfsw ftp verify /build/single.bin /build/single-returned.bin' \
	'kfsw ftp verify /build/multi.bin /build/multi-returned.bin' \
	'kfsw ftp verify /build/large.bin /build/large-returned.bin' \
	'kfsw ftp get 2 /flash/missing.bin /build/missing.bin' \
	'kfsw ftp stat 2 ../params/parameters.dat' \
	'kfsw csp ping 2' \
	'kfsw param get 2 test_u32' \
	'kfsw csp info' \
    'kfsw uart info' \
    'kfsw uart test' >&3

printf '%s\n' \
    'kfsw csp ping 1' \
    'kfsw uart info' \
    'kfsw uart test' >&4

wait_for_output "$work_dir/node1.log" "CSP ping 2: success" \
    "$node1_pid" || fail "node 1 could not ping CSP node 2"
wait_for_output "$work_dir/node2.log" "CSP ping 1: success" \
    "$node2_pid" || fail "node 2 could not ping CSP node 1"
wait_for_output "$work_dir/node1.log" "UART CSP test: PASS" \
    "$node1_pid" || fail "node 1 UART transport test did not pass"
wait_for_output "$work_dir/node2.log" "UART CSP test: PASS" \
    "$node2_pid" || fail "node 2 UART transport test did not pass"
wait_for_output "$work_dir/node1.log" "2:test_u32 = 1234" \
    "$node1_pid" || fail "remote parameter set/readback did not pass"
wait_for_output "$work_dir/node1.log" \
    "get: parameter 'missing' not found" "$node1_pid" || \
    fail "invalid remote parameter was not rejected"
wait_for_output "$work_dir/node1.log" \
    "set: parameter 'node_id' is read-only" "$node1_pid" || \
    fail "remote read-only parameter write was not rejected"
wait_for_output "$work_dir/node1.log" \
	"FTP verify first=/build/large.bin second=/build/large-returned.bin: PASS" \
	"$node1_pid" || fail "8 KiB FTP round trip did not pass"
wait_for_output "$work_dir/node1.log" \
	"FTP get node=2 path=/flash/missing.bin: not found" \
	"$node1_pid" || fail "missing remote FTP file was not rejected"
wait_for_output "$work_dir/node1.log" \
	"FTP stat node=2 path=../params/parameters.dat: invalid path/request" \
	"$node1_pid" || fail "FTP path traversal was not rejected"

node1_expected=(
    "CSP node: 1"
    "hostname: kfsw-1"
    "initialized: yes"
    "router: running"
    "LOOP addr=1/14"
    "KISS addr=1/0"
    "0/0 -> KISS direct"
    "UART transport"
    "device: uart_1"
    "baudrate: 115200"
    "configuration: 8N1, flow control none"
    "ready: yes"
    "CSP interface: KISS"
    "CSP peer: 2"
    "UART CSP test: PASS"
    "2:0 node_id"
    "2:test_u32 = 42"
    "2:test_u32 = 1234"
    "get: parameter 'missing' not found"
    "set: parameter 'node_id' is read-only"
	"FTP generate path=/build/empty.bin: PASS bytes=0 crc32=00000000"
	"FTP mkdir node=2 path=/flash: PASS"
	"FTP put node=2 source=/build/empty.bin destination=/flash/empty.bin: PASS bytes=0"
	"FTP put node=2 source=/build/single.bin destination=/flash/single.bin: PASS bytes=128"
	"FTP put node=2 source=/build/multi.bin destination=/flash/multi.bin: PASS bytes=1024"
	"FTP put node=2 source=/build/large.bin destination=/flash/large.bin: PASS bytes=8192"
	"FTP stat node=2 path=/flash/large.bin type=file bytes=8192"
	"FTP list node=2 path=/flash"
	"FTP list: PASS entries=4"
	"FTP get node=2 source=/flash/large.bin destination=/build/large-returned.bin: PASS bytes=8192"
	"FTP verify first=/build/empty.bin second=/build/empty-returned.bin: PASS"
	"FTP verify first=/build/single.bin second=/build/single-returned.bin: PASS"
	"FTP verify first=/build/multi.bin second=/build/multi-returned.bin: PASS"
	"FTP verify first=/build/large.bin second=/build/large-returned.bin: PASS"
	"FTP get node=2 path=/flash/missing.bin: not found"
	"FTP stat node=2 path=../params/parameters.dat: invalid path/request"
    "interface: KISS"
    "  info"
    "  test"
    "interfaces"
    "ping"
    "routes"
)

for expected in "${node1_expected[@]}"; do
    if ! grep -Fq "$expected" "$work_dir/node1.log"; then
        fail "node 1 shell output is missing: $expected"
    fi
done

cat "$work_dir/node1.log"
cat "$work_dir/node2.log"
echo "CSP RESULT: PASS"
