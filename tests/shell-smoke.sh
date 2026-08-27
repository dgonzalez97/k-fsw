#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
capture_file="$(mktemp)"

cleanup()
{
    rm -f "$capture_file"
}

trap cleanup EXIT

if [[ ! -x "$executable" ]]; then
    echo "ERROR: KFSW-Linux has not been built."
    echo "Run: $KFSW_ROOT/k-fsw/tools/build.sh linux"
    exit 1
fi

printf '%s\n' \
    'kfsw status' \
    'kfsw time' \
    'kfsw version' \
    'kfsw uart info' \
    'kfsw param list' \
    'kfsw param get test_u32' \
    'kfsw param set test_u32 1234' \
    'kfsw param get test_u32' \
    'kfsw param set node_id 2' \
    'kfsw param get missing' \
    'kfsw log test' |
    "$executable" --uart_stdinout --stop_at=1.0 --no-color >"$capture_file" 2>&1

cat "$capture_file"

expected_output=(
    '@BOOT '
    '@READY '
    'K-FSW status'
    'board: native_sim/native/64'
    'uptime_ms: '
    'monotonic_ms: '
    'monotonic_us: '
    'K-FSW: kfsw-dev'
    'Zephyr: 4.4.0'
    'Board: native_sim/native/64'
    'UART transport'
    'device: uart_1'
    'baudrate: 115200'
    'configuration: 8N1, flow control none'
    'ready: yes'
    'CSP interface: KISS'
    'CSP node: 1'
    'CSP peer: 2'
    '0:0 node_id'
    '0:1 log_level'
    '0:2 test_u32'
    '0:3 test_i32'
    '0:4 test_float'
    'test_u32 = 42'
    'test_u32 = 1234'
    "set: parameter 'node_id' is read-only"
    "get: parameter 'missing' not found"
    '[ERROR] K-FSW shell log test: error'
    '[WARNING] K-FSW shell log test: warning'
    '[INFO] K-FSW shell log test: info'
)

for expected in "${expected_output[@]}"; do
    if ! grep -Fq "$expected" "$capture_file"; then
        echo "SHELL RESULT: FAIL"
        echo "  missing: $expected"
        exit 1
    fi
done

echo "SHELL RESULT: PASS"
