#!/usr/bin/env bash
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
board_name=nucleo_l496zg
while [[ $# -gt 0 ]]; do
	case "$1" in
	--board)
		[[ $# -ge 2 && -n "$2" && "$2" != --* ]] || { echo '--board needs a name' >&2; exit 2; }
		board_name="$2"; shift 2 ;;
	*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done
source "$here/../resources/board.sh"
if ! hil_board "$board_name" limits-links; then
	echo "LIMITS LINKS RESULT: NOT RUN - $hil_board_reason"
	exit 0
fi
profiles="$KFSW_REPO_DIR/config/profiles"
debug="$hil_board_debug"
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
[[ "${KFSW_FTDI_BOARD:-nucleo_l496zg}" == "$board_name" ]] || skip 'FTDI is assigned to another board'
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
build "$KFSW_TARGET" board "$hil_board_conf" "$hil_board_overlay"

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

# A counter has to be asked again: board_check sends once, so the first answer
# it reads is the only one it will ever see.
board_poll() {
	local command="$1" pattern="$2" seconds="${3:-20}"
	for ((i = 0; i < seconds; i++)); do
		board_check "$command" "$pattern" && return 0
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
echo "build: $(git -C "$KFSW_REPO_DIR" rev-parse HEAD) board=$board_name debug=$debug KISS=$ftdi CAN=$interface"
board_check 'csp routes' '16/14 -> KISS' || fail 'node 16 has no KISS route'
since_mark | grep -Fq '17/14 -> CAN' || fail 'node 17 has no CAN route'
board_check 'csp ping 16' 'CSP ping 16: success' || skip 'board cannot ping 16 over KISS'
board_check 'csp ping 17' 'CSP ping 17: success' || skip 'board cannot ping 17 over CAN'
printf '%s\n' 'csp routes' 'csp interfaces' 'csp ping 1' >&3
printf '%s\n' 'csp routes' 'csp interfaces' 'csp ping 1' >&4
wait_for "$work/kiss.log" 'CSP ping 1: success' || skip 'KISS return ping failed'
wait_for "$work/can.log" 'CSP ping 1: success' || skip 'CAN return ping failed'
# A release is a command addressed to another node, so prove that path on both
# links before blaming the rules for a release that never arrives. A ping only
# proves the framing.
# A remote status answers with the node it came from, not with the local header.
board_check 'status 16' 'node: 16' || fail 'the board cannot command node 16 over KISS'
board_check 'status 17' 'node: 17' || fail 'the board cannot command node 17 over CAN'

printf '%s\n' 'ftp put 1 /limits/two-links.txt /limits/two-links.txt' >&3
wait_for "$work/kiss.log" 'FTP put 1 .*: PASS' || fail 'rule upload failed'
board_check 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'cannot set watched parameter'
# A persisted snapshot outranks the compiled default, so a board that ran an
# earlier image can arrive with the periodic sweep still on. The dwell and the
# latch are only reproducible when the test decides when a sweep happens.
board_check 'param set limits_interval_ms 0' 'limits_interval_ms = 0' ||
	fail 'the periodic sweep could not be turned off'
board_check 'limits status' 'state: manual' || fail 'the sweep is still periodic'
board_check 'limits load two-links.txt' 'Loaded 2 rules' || fail 'rules did not load'
board_check 'limits list' '1 +boot_delay_ms +> +3000 +2 +17 +armed +noop' || fail 'CAN rule not armed'
since_mark | grep -Eq '0 +boot_delay_ms +> +1000 +2 +16 +armed +noop' || fail 'KISS rule not armed'

# What the node has executed so far. The link checks above run a command on
# each of them, so a release is counted from this baseline, not from zero.
baseline() {
	local node="$1" fd="$2" start
	start="$(wc -c <"$work/$node.log")"
	printf '%s\n' 'param get cmd_invoked' >&"$fd"
	for _ in {1..120}; do
		if tail -c "+$((start + 1))" "$work/$node.log" | grep -Eq 'cmd_invoked = [0-9]+'; then
			tail -c "+$((start + 1))" "$work/$node.log" | tr -d '\r' |
				sed -n 's/.*cmd_invoked = \([0-9]*\).*/\1/p' | tail -1
			return 0
		fi
		sleep 0.1
	done
	fail "$node did not report its command counter"
}

receipt() {
	local node="$1" expected="$2" fd="$3" log="$work/$1.log" start
	start="$(wc -c <"$log")"
	printf '%s\n' 'param get cmd_invoked' 'param get cmd_failed' >&"$fd"
	for _ in {1..60}; do
		tail -c "+$((start+1))" "$log" >"$work/receipt-$node.log"
		if grep -Eq "cmd_invoked = $expected([^0-9]|$)" "$work/receipt-$node.log" &&
			grep -Eq 'cmd_failed = 0([^0-9]|$)' "$work/receipt-$node.log"; then
			cat "$work/receipt-$node.log"; return 0
		fi
		# Ask again: a counter that has not moved yet answers the old value.
		printf '%s\n' 'param get cmd_invoked' 'param get cmd_failed' >&"$fd"
		sleep 0.5
	done
	fail "$node did not receive exactly $expected successful noop commands"
}
kiss_base="$(baseline kiss 3)"
can_base="$(baseline can 4)"
echo "commands already executed: node 16 $kiss_base, node 17 $can_base"
# Two crossings, and what each one has to do: fire both rules, queue one entry
# per node, deliver each to the node the rule named, latch, and re-arm.
#
# What this does not check is the release that is refused because an identical
# entry is still queued. That needs a second crossing inside the one second
# release delay, and nine shell commands do not fit in a second on a 115200
# console: sending them as one burst overflows the shell input and the rest are
# dropped. The software smoke proves it where commands are instant.
for cycle in 1 2; do
	fired=$((cycle * 2))
	board_check 'param set boot_delay_ms 4000' 'boot_delay_ms = 4000' || fail 'crossing failed'
	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'first dwell sweep failed'
	board_check 'limits status' "fires: $((fired - 2)) failures: 0" || fail 'released before dwell'
	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'second dwell sweep failed'
	board_check 'limits status' "fires: $fired failures: 0" || fail 'the crossing did not release'
	board_check 'fbo sched list' 'node=16 .*noop' || fail 'nothing queued for node 16'
	since_mark >"$work/queued-$cycle.log"
	grep -Eq 'node=17 .*noop' "$work/queued-$cycle.log" || fail 'nothing queued for node 17'
	for node in 16 17; do
		[[ "$(grep -Ec "node=$node +scheduled +noop" "$work/queued-$cycle.log")" == 1 ]] ||
			fail "node $node has more than one queued entry"
	done
	# The queue listing only says they are queued. Wait for the board to say
	# it released them before reading the counters on the far side.
	board_poll 'fbo sched status' "completed: $((cycle * 2))" 20 ||
		fail 'the board did not release both entries'
	receipt kiss "$((kiss_base + cycle))" 3
	receipt can "$((can_base + cycle))" 4

	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'latch sweep failed'
	board_check 'limits status' "fires: $fired failures: 0" || fail 'a latched rule fired again'

	board_check 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'return failed'
	board_check 'limits sweep' 'Evaluated 2 rules' || fail 'rearm sweep failed'
	board_check 'limits list' '1 +boot_delay_ms .*armed +noop' || fail 'CAN rule did not rearm'
	since_mark | grep -Eq '0 +boot_delay_ms .*armed +noop' || fail 'KISS rule did not rearm'
done
board_check 'fbo sched status' 'completed: 4' || fail 'scheduler did not complete four releases'
since_mark | grep -Eq '^failed: 0$' || fail 'scheduler reported a failure'
since_mark | grep -Eq '^scheduled: 0$' || fail 'scheduler still has queued entries'
receipt kiss "$((kiss_base + 2))" 3
receipt can "$((can_base + 2))" 4
board_check 'csp interfaces' 'CAN' || fail 'interface counters unavailable'
printf '%s\n' 'csp interfaces' >&3
printf '%s\n' 'csp interfaces' >&4
sleep 1
cat "$work/kiss.log" "$work/can.log"
echo 'LIMITS LINKS RESULT: PASS nodes=16,17 links=KISS,CAN dwell=2 latch=yes rearm=yes'
