#!/usr/bin/env bash
# Procedures, the time-tagged queue and remote execution on a board, over CAN.
#
# The software smoke runs procedures on a hosted node, where a file is already
# on disk and the link is a pipe. None of that is the question here. Here the
# procedure arrives over a real bus, runs on silicon, and the queue releases a
# command after the ground node is gone, which is the whole reason the queue
# exists.
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$here/../../../tools/_common.sh" nucleo_l496zg
profiles="$KFSW_REPO_DIR/config/profiles"
debug="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
interface="${KFSW_CAN_INTERFACE:-can0}"
board_node=2
ground_node=17
procedure=pass-plan.txt
work="$(mktemp -d /tmp/kfsw-fbo-can.XXXXXX)"
build_root="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/hil/fbo"
pids=()
settings=""

cleanup() {
	for pid in "${pids[@]:-}"; do kill "$pid" 2>/dev/null || true; done
	wait 2>/dev/null || true
	[[ -z "$settings" ]] || stty -F "$debug" "$settings" 2>/dev/null || true
}
trap cleanup EXIT
fail() { echo "FBO CAN RESULT: FAIL - $*"; exit 1; }
skip() { echo "FBO CAN RESULT: NOT RUN - $*"; exit 0; }

echo "FBO CAN: logs in $work"
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
build nucleo_l496zg board "$profiles/nucleo-can.conf;$profiles/nucleo-fbo-can.conf" \
	"$profiles/nucleo-can.overlay"

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

# The procedure reaches the board the way an operator sends it: it starts on the
# ground node and crosses the bus.
"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$work/ground.bin" --offset 0xfc000 --size 0x40000 \
	"$KFSW_REPO_DIR/tests/procedures/examples/$procedure" "/ftp/procedures/$procedure" \
	>"$work/stage.log" 2>&1 || fail 'the procedure could not be staged'

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

# What the ground may and may not run on the board. The allowlist is the whole
# mechanism, so the refusal matters as much as the success.
printf '%s\n' "remexec $board_node get" >&3
wait_for "$work/ground.log" 'storage info' || fail 'the board did not list what it offers'
printf '%s\n' "remexec $board_node run \"storage info\"" >&3
wait_for "$work/ground.log" 'mount_point: /kfsw' || fail 'an offered command returned nothing'
wait_for "$work/ground.log" 'status: ok' || fail 'an offered command did not report ok'
printf '%s\n' "remexec $board_node run \"reboot 0000\"" >&3
wait_for "$work/ground.log" 'not offered' ||
	fail 'a command outside the allowlist was not refused'

# The service makes its own directory at startup, so an upload has somewhere to
# land without an operator being told to create it first.
printf '%s\n' "ftp ls $board_node /" >&3
wait_for "$work/ground.log" '^dir +0 procedures' 30 ||
	fail 'the board did not create its procedures directory'
printf '%s\n' "ftp put $board_node /procedures/$procedure /procedures/$procedure" >&3
wait_for "$work/ground.log" "FTP put $board_node .*: PASS" 60 ||
	fail 'the procedure did not cross the bus'

board "fbo run $procedure" "$procedure finished at line 7" ||
	fail 'the procedure did not run to its end on the board'
board 'fbo status' 'lines failed: 0' || fail 'a line of the procedure failed'
since_mark | grep -Eq 'runs: 1' || fail 'the board did not count the run'
# The procedure's own last line leaves a check-in behind.
board 'fbo sched list' '\+600s .*node=0 scheduled noop' ||
	fail 'the procedure did not queue its check-in'
board 'fbo sched clear' 'fbo_sched_clear: ok' || fail 'the queue would not clear'

# An entry the ground leaves behind, released after the ground is gone.
board 'fbo sched add +12 0 "noop"' 'fbo_sched_add: ok \(index=0' ||
	fail 'the queue refused an entry'
board 'fbo sched add +12 0 "noop"' 'repeated=yes' ||
	fail 'the same entry was queued twice'
# The ground node, not the serial capture: the board's console has to stay
# open or the release that follows is simply not seen.
kill "$ground_pid" 2>/dev/null || true
sleep 1
mark
echo 'FBO CAN: the ground node is gone; waiting for the release'
wait_for "$work/board.log" 'entry 0 released' 40 || fail 'the entry never released'
board 'fbo sched status' 'completed: 1' || fail 'the release was not counted'
since_mark | grep -Eq '^failed: 0$' || fail 'the scheduler reported a failure'

tr -d '\r' <"$work/board.log" | tail -40
echo "FBO CAN RESULT: PASS procedure=$procedure link=$interface released_with_nobody_listening=yes"
