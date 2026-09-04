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

{
	printf 'pa\t g\t test_u32\n'
	printf '%s\n' \
		'help' \
		'status' \
		'time' \
		'version' \
		'uart info' \
		'param tables' \
		'param get uid' \
		'param set route_table "9/9 KISS"' \
		'csp routes' \
		'param set route_table bad-table' \
		'param list' \
		'param get test_u32' \
		'param set test_u32 1234' \
		'param get test_u32' \
		'param set node_id 2' \
		'param get missing' \
		'param save' \
		'param defaults' \
		'param get test_u32' \
		'param load' \
		'param get test_u32' \
		'param clear' \
		'storage info' \
		'storage test' \
		'ftp' \
		'ftp generate' \
		'ftp 1 mkdir /selfnode' \
		'ftp 1 ls /' \
		'ftp stat 1 /selfnode' \
		'ftp 1 ls /missing' \
		'ftp put 1 /selfnode/a.bin /selfnode/b.bin' \
		'log test'
} |
	"$executable" --uart_stdinout --stop_at=2.0 --no-color \
		-flash="$flash_image" >"$capture_file" 2>&1

cat "$capture_file"

expected_output=(
    '@BOOT '
    '@READY '
    'kfsw:~$ param  get  test_u32'
	'Available commands:'
	'csp      : K-FSW CSP commands.'
	'ftp      : K-FSW file transfer:'
	'param    : K-FSW parameter commands.'
	'status   : Show basic K-FSW runtime status.'
	'storage  : K-FSW filesystem storage commands.'
	'version  : Show K-FSW build information.'
    'K-FSW status'
	'Role: flight'
	'Name: kfsw'
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
    # Listed as table, offset, name: the columns are what makes a listing
    # readable, so the smoke test checks the columns and not just the names.
    # Every core table is registered and named, in ascending identifier order.
    '  1  core     board'
    '  2  core     system'
    '  3  core     telemetry'
    '  4  core     csp'
    '  5  core     storage'
    ' 25  service  log'
    'board       0x00  node_id'
    # Strings render quoted, so an empty value reads as a value.
    'board       0x10  uid                               string  r     "kfsw-1"'
    'system      0x00  boot_delay_ms'
    'telemetry   0x00  uptime_s'
    'test        0x08  test_u32'
    'log         0x00  log_level'
    'test_u32 = 42'
    'test_u32 = 1234'
    "set: parameter 'node_id' is read-only"
    "get: parameter 'missing' not found"
    'uid = "kfsw-1"'
    # Applied to the running router, not merely stored.
    'route_table = "9/9 KISS"'
    '9/9 -> KISS direct'
    "set: parameter 'route_table' failed (-22)"
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
	'  mkdir     : Create a directory: mkdir <node> <path>; <node> may be this node.'
	'generate: wrong parameter count'
	'generate - Create deterministic local data: generate <path> <bytes 0..32768>.'
	'FTP mkdir node=1 path=/selfnode: PASS'
	'FTP list: PASS entries='
	'FTP stat node=1 path=/selfnode type=directory'
	'FTP list node=1 path=/missing: not found'
	'FTP put node=1: transfers need two nodes'
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

if grep -Eq '^  kfsw +:' "$capture_file"; then
	echo "SHELL RESULT: FAIL"
	echo "  kfsw must be a prompt, not a root command"
	exit 1
fi

echo "SHELL RESULT: PASS"
