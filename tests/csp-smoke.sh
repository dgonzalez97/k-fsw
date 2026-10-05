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

node1_executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
node2_build_dir="$KFSW_ROOT/build/tests/linux-node2"
node2_executable="$node2_build_dir/zephyr/zephyr.exe"
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
	KFSW_BUILD_DIR="$node2_build_dir" "$KFSW_ROOT/k-fsw/tests/build-linux-node2.sh"
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

printf 'csp \t\n' >&3
printf 'comms uart \t\n' >&3
printf '%s\n' \
	'csp info' \
	'csp counters' \
	'csp counters clear' \
	'csp interfaces' \
	'csp ifstat 2 KISS' \
	'csp ifstat 2 LOOP' \
	'csp ifstat 2 missing' \
	'csp routes' \
	'csp ping 2' \
	$'csp p\t 2' \
	'csp ifstat 2 KISS' \
	'param list 2' \
	'param get 2 test_u32' \
	'param set 2 test_u32 1234' \
	'param get 2 test_u32' \
	$'pa\t g\t 2 test_u32' \
	'param set 2 log_level 3' \
	'param get 2 log_level' \
	'param tables 2' \
	'param table 2 32' \
	'param table 2 1' \
	'param get 2 uid' \
	'param get 2 csp_buf_free' \
	'param get 2 log_level' \
	'param set 2 log_level 5' \
	'param get 2 log_level' \
	'param get 2 missing' \
	'param set 2 node_id 7' \
	'ftp generate /build/empty.bin 0' \
	'ftp generate /build/single.bin 128' \
	'ftp generate /build/multi.bin 1024' \
	'ftp generate /build/large.bin 8192' \
	'ftp mkdir 2 /flash' \
	'ftp put 2 /build/empty.bin /flash/empty.bin' \
	'ftp put 2 /build/single.bin /flash/single.bin' \
	$'ftp p\t 2 /build/single.bin /flash/single.bin' \
	'ftp put 2 /build/multi.bin /flash/multi.bin' \
	'ftp put 2 /build/large.bin /flash/large.bin' \
	'ftp stat 2 /flash/large.bin' \
	'ftp ls 2 /flash' \
	'ftp get 2 /flash/empty.bin /build/empty-returned.bin' \
	'ftp get 2 /flash/single.bin /build/single-returned.bin' \
	'ftp get 2 /flash/multi.bin /build/multi-returned.bin' \
	'ftp get 2 /flash/large.bin /build/large-returned.bin' \
	'ftp verify /build/empty.bin /build/empty-returned.bin' \
	'ftp verify /build/single.bin /build/single-returned.bin' \
	'ftp verify /build/multi.bin /build/multi-returned.bin' \
	'ftp verify /build/large.bin /build/large-returned.bin' \
	'ftp get 2 /flash/missing.bin /build/missing.bin' \
	'ftp ls 2 /' \
	'ftp put 2 /build/large.bin /tmp/large.bin' \
	'ftp get 2 /tmp/large.bin /build/tmp-returned.bin' \
	'ftp verify /build/large.bin /build/tmp-returned.bin' \
	'ftp generate /build/huge.bin 32768' \
	'ftp put 2 /build/huge.bin /tmp/huge.bin' \
	'ftp ls 2 /tmp' \
	'ftp stat 2 ../params/parameters.dat' \
	'csp ping 1' \
	'status 2' \
	'event stats 2' \
	'event tail 2 0' \
	'event tail 2 999' \
	'journal stats 2' \
	'journal remote 2 4' \
	'log remote 2 4' \
	'param set 2 log_remote_format 1' \
	'log remote 2 4' \
	'param set 2 log_remote_format 0' \
	'status 16383' \
	'csp ping 2' \
	'param get 2 test_u32' \
	'csp info' \
	'comms uart info' \
	'comms uart test' >&3

printf '%s\n' \
	'csp ping 1' \
	'comms uart info' \
	'comms uart test' >&4

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
# Remote write to a sampled parameter: sampling must not overwrite the new value
# when it is applied.
wait_for_output "$work_dir/node1.log" "2:log_level = 3" \
    "$node1_pid" || fail "a remote write to a sampled parameter did not take effect"

# One table and the table list, reusing the descriptors from the first read.
wait_for_output "$work_dir/node1.log" " 32  service" \
    "$node1_pid" || fail "the remote table summary did not list the boot table"
wait_for_output "$work_dir/node1.log" "32          0x00  boot_image" \
    "$node1_pid" || fail "the remote table did not list its parameters"

# A table listing reads values in windows and a get reads one; both must agree.
# uid is a string, which a wrongly packed request breaks first.
wait_for_output "$work_dir/node1.log" "1           0x10  uid" \
    "$node1_pid" || fail "the batched table read did not list the board identity"
if ! grep -Eq '^1 +0x10 +uid +string +r +"kfsw-2"' "$work_dir/node1.log"; then
    fail "the batched table read disagreed with the single read of uid"
fi
# A scalar in the same listing too.
if ! grep -Eq '^1 +0x00 +node_id +u16 +r +2' "$work_dir/node1.log"; then
    fail "the batched table read did not return the node identifier"
fi

# A string over the link, and a sampled value that is current when read.
wait_for_output "$work_dir/node1.log" '2:uid = "kfsw-2"' \
    "$node1_pid" || fail "the remote identity did not arrive as text"
# Free buffers can't be zero on a node that just answered, unlike uptime this
# early.
wait_for_output "$work_dir/node1.log" "2:csp_buf_free = " \
    "$node1_pid" || fail "the remote buffer count was not readable"
if grep -Fq "2:csp_buf_free = 0" "$work_dir/node1.log"; then
    fail "the remote buffer count read back as zero, so nothing sampled it"
fi

wait_for_output "$work_dir/node1.log" "2:log_level = 1" \
	"$node1_pid" || fail "remote validation did not restore the compiled default"
wait_for_output "$work_dir/node1.log" \
    "get: parameter 'missing' not found" "$node1_pid" || \
    fail "invalid remote parameter was not rejected"
wait_for_output "$work_dir/node1.log" \
    "set: parameter 'node_id' is read-only" "$node1_pid" || \
    fail "remote read-only parameter write was not rejected"
wait_for_output "$work_dir/node1.log" \
	"FTP verify /build/large.bin /build/large-returned.bin: PASS" \
	"$node1_pid" || fail "8 KiB FTP round trip did not pass"
wait_for_output "$work_dir/node1.log" \
	"FTP get 2 /flash/missing.bin: not found" \
	"$node1_pid" || fail "missing remote FTP file was not rejected"
wait_for_output "$work_dir/node1.log" \
	"FTP stat 2 ../params/parameters.dat: invalid path/request" \
	"$node1_pid" || fail "FTP path traversal was not rejected"

node1_expected=(
    "CSP ping 1: success"
    # Remote commands answer one field per line, after the node they came from.
    "node: 2"
    "free_bytes: "
    "recorded: "
    "data: "
    "event_tail: failed, no record at age 999"
    "ready: 1"
    "Node must be 1..16382: 16383"
    "CSP node: 1"
    "hostname: kfsw-1"
    "revision: "
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
    # Remote listings show the table number instead of the name.
    "1           0x00  node_id"
    "2:test_u32 = 42"
    "2:test_u32 = 1234"
	"2:log_level = 5"
	"2:log_level = 1"
    '2:uid = "kfsw-2"'
    "get: parameter 'missing' not found"
    "set: parameter 'node_id' is read-only"
	"FTP generate /build/empty.bin: PASS"
	"crc32: 00000000"
	"FTP mkdir 2 /flash: PASS"
	"FTP put 2 /build/empty.bin -> /flash/empty.bin: PASS"
	"FTP put 2 /build/single.bin -> /flash/single.bin: PASS"
	"FTP put 2 /build/multi.bin -> /flash/multi.bin: PASS"
	"FTP put 2 /build/large.bin -> /flash/large.bin: PASS"
	"bytes: 128"
	"bytes: 1024"
	"bytes: 8192"
	"FTP stat 2 /flash/large.bin"
	"type: file"
	"FTP ls 2 /flash"
	"entries: 4"
	"FTP get 2 /flash/large.bin -> /build/large-returned.bin: PASS"
	"FTP verify /build/empty.bin /build/empty-returned.bin: PASS"
	"FTP verify /build/single.bin /build/single-returned.bin: PASS"
	"FTP verify /build/multi.bin /build/multi-returned.bin: PASS"
	"FTP verify /build/large.bin /build/large-returned.bin: PASS"
	"FTP get 2 /flash/missing.bin: not found"
	"file       8192 large.bin"
	"dir           0 tmp"
	"FTP put 2 /build/large.bin -> /tmp/large.bin: PASS"
	"FTP verify /build/large.bin /build/tmp-returned.bin: PASS"
	# 32 KB does not fit the RAM volume with the margin; refused before any data moves.
	"FTP put 2 /tmp/huge.bin: not enough free space"
	"FTP stat 2 ../params/parameters.dat: invalid path/request"
    "interface: KISS"
    "  info"
    "  test"
    "counters"
    "interfaces"
    "ping"
    "routes"
    "last_can_error=0 (none)"
    "CSP counters cleared"
    "CSP ifstat 2 KISS"
    "CSP ifstat 2 LOOP"
    # No such interface: the node does not answer, which is logged as a warning.
    "csp ifstat: node 2 did not answer"
    "format: text"
    "format: dictionary"
    " pkg="
    "src=boot id=1"
    "autherr: 0"
    "txbytes: "
)

for expected in "${node1_expected[@]}"; do
    if ! grep -Fq "$expected" "$work_dir/node1.log"; then
        fail "node 1 shell output is missing: $expected"
    fi
done

python3 - "$work_dir/node1.log" <<'PYTEST'
import re
import sys
from pathlib import Path
rows = re.findall(r'CSP ifstat 2 KISS\s+tx: (\d+)\s+rx: (\d+)',
                  Path(sys.argv[1]).read_text())
assert len(rows) >= 2, rows
first, last = tuple(map(int, rows[0])), tuple(map(int, rows[-1]))
assert last[0] > first[0] and last[1] > first[1], rows
PYTEST

# A dictionary record, decoded with node 2's image, must read as the text the
# node itself sends for the same sequence.
python3 "$KFSW_ROOT/k-fsw/tools/ground/log-decode.py" --elf "$node2_executable" \
	"$work_dir/node1.log" >"$work_dir/node1-decoded.log" ||
	fail "a dictionary record of node 2 did not decode"
python3 - "$work_dir/node1.log" "$work_dir/node1-decoded.log" <<'PYTEST'
import re
import sys
from pathlib import Path
record = re.compile(r'^(\d+) t=\d+ms \S+ level=\d( truncated)? (.*)$')
def block(lines, name):
    start = lines.index(f'format: {name}')
    out = {}
    for line in lines[start + 2:]:
        match = record.match(line)
        if not match:
            break
        out[match.group(1)] = match.group(3)
    return out
raw = Path(sys.argv[1]).read_text(errors='replace').splitlines()
decoded = Path(sys.argv[2]).read_text(errors='replace').splitlines()
text = block(raw, 'text')
dictionary = block(decoded, 'dictionary')
common = set(text) & set(dictionary)
assert len(common) >= 2, (text, dictionary)
for sequence in common:
    assert text[sequence] == dictionary[sequence], (sequence, text[sequence], dictionary[sequence])
PYTEST

cat "$work_dir/node1.log"
cat "$work_dir/node2.log"
echo "CSP RESULT: PASS"
