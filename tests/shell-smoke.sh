#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/csp-version.sh"
build_dir_override="${KFSW_BUILD_DIR:-}"
source "$(dirname "$0")/../tools/_common.sh" linux

if [[ -z "$build_dir_override" && -n "$csp_suffix" ]]; then
    KFSW_BUILD_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/linux$csp_suffix"
fi

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/kfsw-shell-smoke.XXXXXX")"
capture_file="$work_dir/output.log"
flash_image="$work_dir/flash.bin"

cleanup()
{
	[[ "${KFSW_KEEP_TEST_OUTPUT:-0}" == 1 ]] || rm -rf -- "$work_dir"
}

trap cleanup EXIT

if [[ ! -x "$executable" ]]; then
    echo "ERROR: KFSW-Linux has not been built."
    echo "Run: $KFSW_ROOT/k-fsw/tools/build.sh linux"
    exit 1
fi

{
	# Echo is off by default; turn it on to see tab completion.
	printf 'param set echo_enabled 1\n'
	sleep 0.2
	printf 'pa\t g\t test_u32\n'
	# Two matches: Tab lists them one per line with their help.
	printf 'st\t\n'
	printf 'param \t\025\n'
	printf 'param li\t -h\n'
	printf 'param wh\t -h\n'
	printf 'ftp \t\025\n'
	printf '%s\n' \
		'help' \
		'status' \
		$'\033[A' \
		'time' \
		'version' \
		'resmon show' \
		'comms uart info' \
		'param -h' \
		'param save -h' \
		'param whichtables' \
		'param get uid' \
		'param set route_table "9/9 KISS"' \
		'csp routes' \
		'csp rdp 2' \
		'csp rdp 256' \
		'csp rdp' \
		'csp rdp 258' \
		'csp rdp' \
		'csp rdp 3' \
		'csp rdp' \
		'csp rdp -18446744073709551615' \
		'csp rdp' \
		'csp rdp 184467440737095516160' \
		'csp rdp' \
		'csp rdp bogus' \
		'csp rdp' \
		'csp rdp " -18446744073709551615"' \
		'csp rdp' \
		'csp rdp ""' \
		'csp rdp' \
		'csp rdp 1' \
		'csp rdp 0' \
		'csp ping' \
		'param set route_table bad-table' \
		'param get route_table' \
		'param listall' \
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
		'ftp -h' \
		'csp -h' \
		'hk -h' \
		'ftp generate' \
		'ftp mkdir /selfnode' \
		'ftp ls /' \
		'ftp mkdir /tmp/scratch' \
		'ftp stat 1 /selfnode' \
		'ftp ls 1 /missing' \
		'ftp put 1 /selfnode/a.bin /selfnode/b.bin' \
		'log test'
} |
	"$executable" --uart_stdinout --stop_at=2.0 --no-color \
		-flash="$flash_image" >"$capture_file" 2>&1

cat "$capture_file"

expected_output=(
	'Every parameter of a node, with its value: listall [node]'
	'The tables a node carries, without values: whichtables [node]'
	'Save persistent parameters to storage'
	'param listall  -h'
	'param whichtables  -h'
	"  status   : Show status, here or on a node: status [node]"
	"  storage  : Filesystem storage commands."
    '@BOOT '
    '@READY '
    # Not anchored to the prompt, which has colour codes.
    'param  get  test_u32'
    # Off by default, and the test turned it on for the completion check above.
    'echo_enabled = 1'
	'Available commands:'
	'csp      : CSP commands.'
	'ftp      : File transfer. Without a node, this node.'
	'param    : Parameter commands.'
	'status   : Show status, here or on a node: status [node]'
	'storage  : Filesystem storage commands.'
	'version  : Show build information: version'
    'K-FSW status'
    'revisions:'
    '  kfsw-libcsp: '
    '  kfsw-libparam: '
	'Role: flight'
	'Name: kfsw'
    'board: native_sim/native/64'
    'uptime_ms: '
    'monotonic_ms: '
    'monotonic_us: '
    # The resource monitor sweeps once at start-up, so it has numbers already.
    'state: sweeping'
    'worst_used: '
    'alert_at: 90%'
    # Every repository compiled into the image, not just the k-fsw version.
    'Revisions: app:'
    ' comms:'
    ' mod:'
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
    # Listed as table, offset, name, with every core table in ID order.
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
    # Runtime writes are rejected; the compiled route remains active.
    "set: parameter 'route_table' is read-only"
    'route_table = "0/0 KISS"'
    '0/0 -> KISS direct'
    "1/$csp_host_bits -> LOOP direct"
    'CSP ping 1: success'
    'rtt_ms: '
	'Parameter snapshot save: PASS'
	'Parameter defaults: PASS (saved snapshot unchanged)'
	'test_u32 = 42'
	'Parameter snapshot load: PASS'
	'Parameter snapshot clear: PASS (RAM unchanged)'
	'K-FSW storage'
	'filesystem: LittleFS'
	'mount_point: /kfsw'
	'ready: yes'
	'total_bytes: '
	'Storage test: PASS'
	'  mkdir     : Create a directory'
	'Download from a remote node: get <node> <remote> <local>'
	'Upload to a remote node: put <node> <local> <remote>'
	'Identify a node, or this one when no node is given: ident [node]'
	'Ping a node, or this one when no node is given: ping [node]'
	'Forget a report: clear [node] <report> [--retry]'
	'generate - Create deterministic local data: generate <path> <bytes 0..32768>'
	'generate: wrong parameter count'
	'FTP mkdir 1 /selfnode: PASS'
	# The firmware slots and the RAM volume show in a listing of the root.
	'dir           0 boot'
	'dir           0 tmp'
	'FTP mkdir 1 /tmp/scratch: PASS'
	'tmp_total_bytes: 32768'
	'entries: '
	'FTP stat 1 /selfnode'
	'type: dir'
	'FTP ls 1 /missing: not found'
	'FTP put 1: transfers need two nodes'
    '[ERROR] K-FSW shell log test: error'
    '[WARNING] K-FSW shell log test: warning'
    '[INFO] K-FSW shell log test: info'
)

if [[ $(grep -Fc 'CSP RDP trace: 2' "$capture_file") -ne 9 ||
      $(grep -Fc 'level is 0 off, 1 errors, 2 with the protocol' "$capture_file") -ne 8 ]]; then
    echo "SHELL RESULT: FAIL"
    echo "  rejected RDP level changed the trace or was accepted"
    exit 1
fi
for level in 0 1; do
    grep -Fq "CSP RDP trace: $level" "$capture_file" || exit 1
done

for expected in "${expected_output[@]}"; do
    if ! grep -Fq "$expected" "$capture_file"; then
        echo "SHELL RESULT: FAIL"
        echo "  missing: $expected"
        exit 1
    fi
done

if [[ $(grep -Fc 'K-FSW status' "$capture_file") -ne 2 ]]; then
    echo "SHELL RESULT: FAIL"
    echo "  arrow-up did not recall status exactly once"
    exit 1
fi

# Both Tab and -h must show the transfer argument order.
if [[ $(grep -Fc 'Download from a remote node: get <node> <remote> <local>' "$capture_file") -lt 2 ]]; then
    echo "SHELL RESULT: FAIL"
    echo "  FTP argument hints missing from Tab or help"
    exit 1
fi

# The version comes from the build, so only check that one is printed.
if ! grep -Eq '^K-FSW: .+' "$capture_file"; then
	echo "SHELL RESULT: FAIL"
	echo "  version reported no image version"
	exit 1
fi

if grep -Eq '^  kfsw +:' "$capture_file"; then
	echo "SHELL RESULT: FAIL"
	echo "  kfsw must be a prompt, not a root command"
	exit 1
fi

# Run rejected commands separately so valid listings cannot mask a dispatch error.
negative_commands=(
    'param list'
    'param tables'
    'param listall 1 extra'
    'param whichtables 1 extra'
    'param listall invalid'
    'param whichtables invalid'
    'param listall 16384'
    'param whichtables 16384'
)
for index in "${!negative_commands[@]}"; do
    command="${negative_commands[$index]}"
    negative_capture="$work_dir/negative-$index.log"
    printf '%s\n' "$command" |
        "$executable" --uart_stdinout --stop_at=1.0 --no-color \
            -flash="$work_dir/negative-flash.bin" >"$negative_capture" 2>&1
    case "$index" in
        0|1) expected='Parameter commands.' ;;
        2|3) expected='wrong parameter count' ;;
        *) expected="CSP node must be in range 1..$csp_broadcast" ;;
    esac
    if ! grep -Fq "$expected" "$negative_capture" ||
        grep -Eq '0x00  node_id|core +board|parameter list failed|did not answer' "$negative_capture"; then
        echo "SHELL RESULT: FAIL: unexpected response to $command"
        cat "$negative_capture"
        exit 1
    fi
done

echo "SHELL RESULT: PASS"
