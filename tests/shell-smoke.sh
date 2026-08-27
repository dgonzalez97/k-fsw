#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-shell-smoke.XXXXXX)"
capture_file="$work_dir/output.log"
flash_image="$work_dir/flash.bin"

cleanup()
{
	rm -rf -- "$work_dir"
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
	'kfsw param save' \
	'kfsw param defaults' \
	'kfsw param get test_u32' \
	'kfsw param load' \
	'kfsw param get test_u32' \
	'kfsw param clear' \
	'kfsw storage info' \
	'kfsw storage test' \
    'kfsw log test' |
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		-flash="$flash_image" >"$capture_file" 2>&1

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
	'Parameter snapshot save: PASS'
	'Parameter defaults: PASS (saved snapshot unchanged)'
	'test_u32 = 42'
	'Parameter snapshot load: PASS'
	'Parameter snapshot clear: PASS (RAM unchanged)'
	'K-FSW storage'
	'filesystem: LittleFS'
	'mount_point: /kfsw'
	'ready: yes'
	'total_bytes: 65536'
	'Storage test: PASS'
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
