#!/usr/bin/env bash
# A parameter limit on a board: crossed, dwelled, released over CAN, re-armed.
#
# The software sequence already proves the parser, the dwell and the latch on a
# hosted node. What only hardware shows is the release leaving the board: the
# rule names another node, so the command crosses a real bus and is counted by
# the node that received it.
#
# The two-link variant lives in released-on-two-links.sh and needs the KISS
# cable on this board; this one needs nothing but the bus.
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
if ! hil_board "$board_name" limits-can; then
	echo "LIMITS CAN RESULT: NOT RUN - $hil_board_reason"
	exit 0
fi
profiles="$KFSW_REPO_DIR/config/profiles"
debug="$hil_board_debug"
interface="${KFSW_CAN_INTERFACE:-can0}"
board_node=2
ground_node=17
rules=over-can.txt
work="$(mktemp -d /tmp/kfsw-limits-can.XXXXXX)"
build_root="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/hil/limits-can"
pids=()
settings=""

cleanup() {
	for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done
	wait 2>/dev/null || true
	[[ -z "$settings" ]] || stty -F "$debug" "$settings" 2>/dev/null || true
}
trap cleanup EXIT
fail() { echo "LIMITS CAN RESULT: FAIL - $*"; exit 1; }
skip() { echo "LIMITS CAN RESULT: NOT RUN - $*"; exit 0; }

echo "LIMITS CAN: logs in $work"
[[ "$debug" == /dev/serial/by-id/* && -e "$debug" ]] ||
	skip 'KFSW_DEBUG_SERIAL must name a present /dev/serial/by-id device'
[[ -n "${KFSW_STLINK_SERIAL:-}" ]] || skip 'KFSW_STLINK_SERIAL must name the flash probe'
command -v ip >/dev/null || skip 'ip is missing'
ip -details link show "$interface" >"$work/can.state" 2>&1 || skip "no $interface"
grep -q 'can state ERROR-ACTIVE' "$work/can.state" || skip "$interface is not an active physical bus"

python="$KFSW_ROOT/.venv/bin/python"
[[ -x "$python" ]] || python=python3

build() {
	local target="$1" name="$2" conf="$3" overlay="${4:-}"
	KFSW_BUILD_DIR="$build_root/$name" KFSW_PRISTINE=always \
		KFSW_EXTRA_CONF_FILE="$conf" KFSW_EXTRA_DTC_OVERLAY_FILE="$overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" "$target" >"$work/build-$name.log" 2>&1 ||
		fail "the $name image did not build; see $work/build-$name.log"
}
build linux ground "$profiles/ground-can.conf" "$profiles/k-ground-can.overlay"
build "$KFSW_TARGET" board "$hil_board_conf" "$hil_board_overlay"

wait_for() {
	local file="$1" pattern="$2" seconds="${3:-30}"
	for ((i = 0; i < seconds * 10; i++)); do
		grep -Eq "$pattern" "$file" 2>/dev/null && return 0
		sleep 0.1
	done
	return 1
}
offset=0
mark() { offset="$(wc -c <"$work/board.log")"; }
since_mark() { tail -c "+$((offset + 1))" "$work/board.log" | tr -d '\r' | sed 's/\x1b\[[0-9;]*[A-Za-z]//g'; }
board() {
	mark
	printf '%s\r' "$1" >"$debug"
	for _ in {1..120}; do
		since_mark | grep -Eq "$2" && return 0
		sleep 0.1
	done
	return 1
}

"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$work/ground.bin" --offset 0xfc000 --size 0x40000 \
	"$here/$rules" "/ftp/limits/$rules" >"$work/stage.log" 2>&1 ||
	fail 'the rule file could not be staged on the ground node'

mkfifo "$work/ground.in"
exec 3<>"$work/ground.in"
"$build_root/ground/zephyr/zephyr.exe" --uart_stdinout --no-color "--can-if=$interface" \
	-flash="$work/ground.bin" <&3 >"$work/ground.log" 2>&1 &
ground_pid="$!"
pids+=("$ground_pid")
wait_for "$work/ground.log" '@READY ' || fail 'the ground node did not start'

settings="$(stty -F "$debug" -g)"
stty -F "$debug" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
cat "$debug" >"$work/board.log" &
pids+=("$!")
kfsw_west_flash --build-dir "$build_root/board" --runner openocd >"$work/flash.log" 2>&1 ||
	fail 'the board would not flash'
wait_for "$work/board.log" '@READY ' 40 || fail 'the board did not report @READY'
echo "build: $(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD) board=$board_node ground=$ground_node bus=$interface"

printf '%s\n' "csp ping $board_node" >&3
wait_for "$work/ground.log" "CSP ping $board_node: success" ||
	skip "the ground node cannot reach the board over $interface"

# The service makes its own directory, so an upload has somewhere to land.
printf '%s\n' "ftp ls $board_node /" >&3
wait_for "$work/ground.log" '^dir +0 limits' 30 ||
	fail 'the board did not create its limits directory'
printf '%s\n' "ftp put $board_node /limits/$rules /limits/$rules" >&3
wait_for "$work/ground.log" "FTP put $board_node .*: PASS" 60 ||
	fail 'the rule file did not cross the bus'

# A persisted snapshot outranks the compiled default, so a board that ran an
# earlier image can arrive with the periodic sweep still on. The dwell and the
# latch are only reproducible when the test decides when a sweep happens.
board 'param set limits_interval_ms 0' 'limits_interval_ms = 0' ||
	fail 'the periodic sweep could not be turned off'
board 'limits status' 'state: manual' || fail 'the sweep is still periodic'
board "limits load $rules" 'Loaded 1 rules' || fail 'the board would not load the rules'
board 'limits list' "0 +boot_delay_ms +> +1000 +2 +$ground_node +armed +noop" ||
	fail 'the rule is not in force'
board 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'cannot set the watched parameter'

invoked() {
	local expected="$1" start
	start="$(wc -c <"$work/ground.log")"
	printf '%s\n' 'param get cmd_invoked' 'param get cmd_failed' >&3
	for _ in {1..120}; do
		tail -c "+$((start + 1))" "$work/ground.log" >"$work/invoked.log"
		if grep -Eq "cmd_invoked = $expected([^0-9]|$)" "$work/invoked.log" &&
			grep -Eq 'cmd_failed = 0([^0-9]|$)' "$work/invoked.log"; then
			return 0
		fi
		sleep 0.1
	done
	cat "$work/invoked.log"
	fail "the ground node did not receive exactly $expected commands"
}
invoked 0

# A dwell of two: the first sweep past the bound counts, it does not release.
board 'param set boot_delay_ms 2000' 'boot_delay_ms = 2000' || fail 'the crossing failed'
board 'limits sweep' 'Evaluated 1 rules' || fail 'the first sweep failed'
board 'limits status' 'fires: 0 failures: 0' || fail 'it released before the dwell'
board 'limits sweep' 'Evaluated 1 rules' || fail 'the second sweep failed'
board 'limits status' 'fires: 1 failures: 0' || fail 'the crossing did not release'
board 'fbo sched list' "node=$ground_node .*noop" || fail 'the release is not in the queue'
invoked 1

# Latched: it does not fire again while the comparison stays true.
board 'limits sweep' 'Evaluated 1 rules' || fail 'the latch sweep failed'
board 'limits status' 'fires: 1 failures: 0' || fail 'a latched rule fired again'

# Re-armed when the value comes back, and it can fire once more.
board 'param set boot_delay_ms 0' 'boot_delay_ms = 0' || fail 'the return failed'
board 'limits sweep' 'Evaluated 1 rules' || fail 'the rearm sweep failed'
board 'limits list' '0 +boot_delay_ms .*armed +noop' || fail 'the rule did not rearm'
sleep 3
board 'param set boot_delay_ms 2000' 'boot_delay_ms = 2000' || fail 'the second crossing failed'
board 'limits sweep' 'Evaluated 1 rules' || fail 'the sweep failed'
board 'limits sweep' 'Evaluated 1 rules' || fail 'the sweep failed'
board 'limits status' 'fires: 2 failures: 0' || fail 'the rule did not fire again'
invoked 2

tr -d '\r' <"$work/board.log" | tail -30
echo "LIMITS CAN RESULT: PASS node=$ground_node link=$interface dwell=2 latch=yes rearm=yes"
