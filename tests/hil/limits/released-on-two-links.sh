#!/usr/bin/env bash
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../../../tools/_common.sh" nucleo_l496zg
profiles="$KFSW_REPO_DIR/config/profiles"
debug="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
ftdi="${KFSW_FTDI_DEVICE:-}"
interface="${KFSW_CAN_INTERFACE:-can0}"
work="$(mktemp -d /tmp/kfsw-limits-links.XXXXXX)"
build_root="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/hil/limits"
pids=()
settings=""
cleanup() {
	for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
	wait 2>/dev/null || true
	[[ -z "$settings" ]] || stty -F "$debug" "$settings" 2>/dev/null || true
}
echo "LIMITS LINKS: logs in $work"
trap cleanup EXIT
fail() { echo "LIMITS LINKS RESULT: FAIL - $*"; exit 1; }
skip() { echo "LIMITS LINKS RESULT: NOT RUN - $*"; exit 0; }
trap 'fail "line $LINENO: command failed"' ERR
for device in "$debug" "$ftdi"; do
	[[ "$device" == /dev/serial/by-id/* && -e "$device" ]] ||
		skip 'set KFSW_DEBUG_SERIAL and KFSW_FTDI_DEVICE to present /dev/serial/by-id devices'
done
[[ "$(readlink -f "$debug")" != "$(readlink -f "$ftdi")" ]] || skip 'debug and KISS need separate UARTs'
[[ "${KFSW_FTDI_BOARD:-nucleo_l496zg}" == nucleo_l496zg ]] || skip 'FTDI is assigned to another board'
for tool in socat ip west; do command -v "$tool" >/dev/null || skip "$tool is missing"; done
[[ -n "${KFSW_STLINK_SERIAL:-}" ]] || skip 'KFSW_STLINK_SERIAL must name the flash probe'
ip -details link show "$interface" >"$work/can.log" 2>&1 || skip "no $interface"
grep -q 'can state ERROR-ACTIVE' "$work/can.log" || skip "$interface is not an active physical CAN bus"
grep -Eq 'bitrate 500000([[:space:]]|$)' "$work/can.log" || skip "$interface is not at 500000 bit/s"

build() {
	local target="$1" name="$2" conf="$3" overlay="${4:-}"
	KFSW_BUILD_DIR="$build_root/$name" KFSW_PRISTINE=always \
		KFSW_EXTRA_CONF_FILE="$conf" KFSW_EXTRA_DTC_OVERLAY_FILE="$overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" "$target" >"$work/build-$name.log" 2>&1 || fail "$name build failed"
}
build linux kiss "$profiles/ground-kiss.conf"
build linux can "$profiles/ground-can.conf" "$profiles/k-ground-can.overlay"
build nucleo_l496zg board "$profiles/nucleo-can.conf;$profiles/nucleo-limits-links.conf" "$profiles/nucleo-can.overlay"

wait_for() {
	local file="$1" pattern="$2" seconds="${3:-30}"
	for ((i=0; i<seconds*10; i++)); do
		grep -Eq "$pattern" "$file" 2>/dev/null && return 0
		sleep 0.1
	done
	return 1
}
mark() { offset="$(wc -c <"$work/board.log")"; }
since_mark() { tail -c "+$((offset+1))" "$work/board.log" | tr -d '\r' | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'; }
send() { printf '%s\r' "$@" >"$debug"; }
board_check() {
	mark; send "$1"
	for _ in {1..100}; do
		since_mark | grep -Eq "$2" && { since_mark; return 0; }
		sleep 0.1
	done
	return 1
}

python="$KFSW_ROOT/.venv/bin/python"
[[ -x "$python" ]] || python=python3
"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$work/kiss.bin" --offset 0xfc000 --size 0x40000 \
	"$here/two-links.txt" /ftp/limits/two-links.txt >"$work/stage.log" 2>&1
mkfifo "$work/kiss.in" "$work/can.in"
exec 3<>"$work/kiss.in" 4<>"$work/can.in"
"$build_root/kiss/zephyr/zephyr.exe" --uart_stdinout --no-color \
	-flash="$work/kiss.bin" <&3 >"$work/kiss.log" 2>&1 &
pids+=("$!")
"$build_root/can/zephyr/zephyr.exe" --uart_stdinout --no-color "--can-if=$interface" \
	-flash="$work/can.bin" <&4 >"$work/can.log" 2>&1 &
pids+=("$!")
wait_for "$work/kiss.log" '@READY ' || fail 'KISS ground did not start'
wait_for "$work/can.log" '@READY ' || fail 'CAN ground did not start'
pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' "$work/kiss.log" | head -1 | tr -d '\r')"
[[ -n "$pty" ]] || fail 'KISS PTY not reported'
socat -d -d "$pty,raw,echo=0" "$ftdi,b115200,raw,echo=0" >"$work/bridge.log" 2>&1 &
pids+=("$!")
wait_for "$work/bridge.log" 'starting data transfer loop' || fail 'FTDI bridge failed'
settings="$(stty -F "$debug" -g)"
stty -F "$debug" 115200 cs8 -cstopb -parenb -crtscts raw -echo
cat "$debug" >"$work/board.log" &
pids+=("$!")
kfsw_west_flash --build-dir "$build_root/board" --runner openocd \
	>"$work/flash.log" 2>&1 || fail 'board flash failed'
wait_for "$work/board.log" '@READY ' 40 || fail 'board did not report @READY'
echo "build: $(git -C "$KFSW_REPO_DIR" rev-parse HEAD) board=nucleo_l496zg debug=$debug KISS=$ftdi CAN=$interface"
board_check 'csp routes' '16/14 -> KISS' || fail 'node 16 has no KISS route'
since_mark | grep -Fq '17/14 -> CAN' || fail 'node 17 has no CAN route'
board_check 'csp ping 16' 'CSP ping 16: success' || skip 'board cannot ping 16 over KISS'
board_check 'csp ping 17' 'CSP ping 17: success' || skip 'board cannot ping 17 over CAN'
printf '%s\n' 'csp routes' 'csp interfaces' 'csp ping 1' >&3
printf '%s\n' 'csp routes' 'csp interfaces' 'csp ping 1' >&4
wait_for "$work/kiss.log" 'CSP ping 1: success' || skip 'KISS return ping failed'
wait_for "$work/can.log" 'CSP ping 1: success' || skip 'CAN return ping failed'
printf '%s\n' 'ftp put 1 /limits/two-links.txt /limits/two-links.txt' >&3
wait_for "$work/kiss.log" 'FTP put 1 .*: PASS' || fail 'rule upload failed'
board_check 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'cannot set watched parameter'
board_check 'limits load two-links.txt' 'Loaded 2 rules' || fail 'rules did not load'
board_check 'limits list' '1 +boot_delay_ms +> +3000 +2 +17 +armed +noop' || fail 'CAN rule not armed'
since_mark | grep -Eq '0 +boot_delay_ms +> +1000 +2 +16 +armed +noop' || fail 'KISS rule not armed'

receipt() {
	local node="$1" expected="$2" fd="$3" log="$work/$1.log" start
	start="$(wc -c <"$log")"
	printf '%s\n' 'param get cmd_invoked' 'param get cmd_failed' >&"$fd"
	for _ in {1..100}; do
		tail -c "+$((start+1))" "$log" >"$work/receipt-$node.log"
		if grep -Eq "cmd_invoked = $expected([^0-9]|$)" "$work/receipt-$node.log" &&
			grep -Eq 'cmd_failed = 0([^0-9]|$)' "$work/receipt-$node.log"; then
			cat "$work/receipt-$node.log"; return 0
		fi
		sleep 0.1
	done
	fail "$node did not receive exactly $expected successful noop commands"
}
receipt kiss 0 3
receipt can 0 4
for cycle in 1 2; do
	board_check 'param set boot_delay_ms 4000' 'boot_delay_ms = 4000' || fail 'crossing failed'
	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'first dwell sweep failed'
	board_check 'limits status' "fires: $(((cycle-1)*2)) failures: 0" || fail 'released before dwell'
	sleep 1
	# The whole burst must finish before the one-second release delay expires.
	# Assert scheduled entries below; a slow console cannot silently pass this check.
	mark
	send 'limits sweep' 'param set boot_delay_ms 0' 'limits sweep' \
		'param set boot_delay_ms 4000' 'limits sweep' 'limits sweep' 'limits sweep' \
		'limits status' 'fbo sched list'
	sleep 0.3
	since_mark >"$work/queued-$cycle.log"
	grep -Eq "fires: $((cycle*2)) failures: 0" "$work/queued-$cycle.log" || fail 'duplicate crossing changed release count'
	for node in 16 17; do
		[[ "$(grep -Ec "node=$node +scheduled +noop" "$work/queued-$cycle.log")" == 1 ]] || fail "node $node did not have exactly one queued entry"
	done
	cat "$work/queued-$cycle.log"
	sleep 5
	receipt kiss "$cycle" 3
	receipt can "$cycle" 4
	board_check 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'return failed'
	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'rearm sweep failed'
	board_check 'limits list' '1 +boot_delay_ms .*armed +noop' || fail 'CAN rule did not rearm'
	since_mark | grep -Eq '0 +boot_delay_ms .*armed +noop' || fail 'KISS rule did not rearm'
done
board_check 'fbo sched status' 'completed: 4' || fail 'scheduler did not complete four releases'
since_mark | grep -Eq '^failed: 0$' || fail 'scheduler reported a failure'
since_mark | grep -Eq '^scheduled: 0$' || fail 'scheduler still has queued entries'
receipt kiss 2 3
receipt can 2 4
board_check 'csp interfaces' 'CAN' || fail 'interface counters unavailable'
printf '%s\n' 'csp interfaces' >&3
printf '%s\n' 'csp interfaces' >&4
sleep 1
cat "$work/kiss.log" "$work/can.log"
echo 'LIMITS LINKS RESULT: PASS nodes=16,17 links=KISS,CAN dwell=2 latch=yes rearm=yes'
