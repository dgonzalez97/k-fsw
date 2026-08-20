#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/_common.sh" linux

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
