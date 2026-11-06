#!/usr/bin/env bash
# Two boards and a ground node on one CAN bus, at a chosen CSP version.
#
# Three legs, because each proves something the others do not: board to board
# shows the flight-to-flight path with no host in it, ground to board shows the
# path an operator uses, and board to ground shows the reply direction.
#
#   tests/hil/can-mesh-smoke.sh --csp-version 2
#   tests/hil/can-mesh-smoke.sh --csp-version 1
#
# Every node on a link has to speak the same version, so this reflashes both
# boards and rebuilds the ground node each time it runs.
set -Eeuo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
work="$(mktemp -d)"
# Keep the directory when something failed: the message points at a log in it,
# and deleting it is how a diagnostic becomes "see the file that is not there".
keep=0
trap '[[ "$keep" == 1 ]] && echo "CAN MESH: logs in $work" || rm -rf "$work"' EXIT

version=2
while [[ $# -gt 0 ]]; do
	case "$1" in
	--csp-version) version="${2:-}"; shift 2 ;;
	*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done
[[ "$version" == "1" || "$version" == "2" ]] || { echo "CAN MESH RESULT: FAIL - version $version"; exit 1; }

# shellcheck source=/dev/null
set -a; . "$here/stm32/nucleo-l496zg/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
bitrate="${KFSW_CAN_BITRATE:-500000}"
ground="${KFSW_CAN_GROUND_NODE:-17}"
flight="${KFSW_CAN_FLIGHT_NODE:-2}"
second="${KFSW_CAN_SECOND_NODE:-3}"

fail() { keep=1; echo "CAN MESH RESULT: FAIL - $*"; exit 1; }
skip() { echo "CAN MESH RESULT: NOT RUN - $*"; exit 0; }

probe() { ls "/dev/serial/by-id/"*"$1"*-if02 2>/dev/null | head -1; }
l4_probe="${KFSW_L496_STLINK:-0670FF3732504E3043093407}"
f7_probe="${KFSW_F746_STLINK:-066BFF353056363043044156}"
l4_console="$(probe "$l4_probe")"
f7_console="$(probe "$f7_probe")"
[[ -n "$l4_console" ]] || skip "no L496 console; is it plugged in?"
[[ -n "$f7_console" ]] || skip "no F746 console; the mesh needs both boards"
command -v ip >/dev/null || skip "ip is not available"
ip link show "$interface" >/dev/null 2>&1 || skip "no $interface; is the adapter plugged in?"
state="$(ip -details link show "$interface" | awk '/can state/ {print $3; exit}')"
[[ "$state" == "ERROR-ACTIVE" ]] || skip "$interface is $state; bring it up with can-up.sh"

cd "$root"
# shellcheck source=/dev/null
[[ -f .venv/bin/activate ]] && . .venv/bin/activate

echo "CAN MESH: CSP $version on $interface at $bitrate bps, nodes $flight, $second and ground $ground"

version_conf=""
[[ "$version" == "1" ]] && version_conf=";$root/k-fsw/config/profiles/csp-v1.conf"

build_and_flash() # target, conf, overlay, probe, build dir
{
	KFSW_EXTRA_CONF_FILE="$2$version_conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$3" \
	KFSW_BUILD_DIR="$5" KFSW_PRISTINE=always \
		"$root/k-fsw/tools/build.sh" "$1" >"$work/build-$1.log" 2>&1 ||
		fail "$1 did not build; see $work/build-$1.log"
	west flash --build-dir "$5" --runner openocd \
		--cmd-pre-init "adapter serial $4" >"$work/flash-$1.log" 2>&1 ||
		fail "could not flash $1; see $work/flash-$1.log"
}

build_and_flash nucleo_l496zg \
	"$root/k-fsw/config/profiles/nucleo-can.conf" \
	"$root/k-fsw/config/profiles/nucleo-can.overlay" \
	"$l4_probe" "$root/build/hil/mesh/l496"
build_and_flash nucleo_f746zg \
	"$root/k-fsw/config/profiles/f746-can.conf" \
	"$root/k-fsw/config/profiles/f746-can.overlay" \
	"$f7_probe" "$root/build/hil/mesh/f746"

# k-ground clears KFSW_CSP_VERSION from the environment before reading a node
# file, so the version has to be written into a copy of that file.
station="$work/station"
mkdir -p "$station/nodes"
cp "$root/k-fsw/ground-station/nodes/kfsw-gnd-can.env" "$station/nodes/kfsw-gnd-can.env"
printf 'KFSW_CSP_VERSION=%s\n' "$version" >>"$station/nodes/kfsw-gnd-can.env"
KGROUND_STATION_DIR="$station" "$root/k-fsw/tools/k-ground" build kfsw-gnd-can \
	>"$work/ground.log" 2>&1 || fail "the ground node did not build; see $work/ground.log"
suffix=""; [[ "$version" == "1" ]] && suffix="-csp1"
ground_exe="$root/build/k-ground/kfsw-gnd-can-node-$ground$suffix/zephyr/zephyr.exe"
[[ -x "$ground_exe" ]] || fail "ground node is missing: $ground_exe"
sleep 3

ping_from_board() # console, peer, label
{
	"$root/.venv/bin/python" - "$1" "$2" <<'PYTHON'
import sys, time
import serial
console, peer = sys.argv[1], sys.argv[2]
with serial.Serial(console, 115200, timeout=1) as link:
    time.sleep(1.5)
    link.write(b"\x03\n"); time.sleep(0.6); link.reset_input_buffer()
    link.write(f"csp ping {peer}\n".encode()); time.sleep(7.0)
    sys.stdout.write(link.read(link.in_waiting or 1).decode(errors="replace"))
PYTHON
}

check() # label, text, peer
{
	if grep -q "CSP ping $3: success" <<<"$2"; then
		rtt="$(sed -n 's/^rtt_ms: \([0-9.]*\).*/\1/p' <<<"$2" | head -1)"
		echo "CAN MESH: $1 ok, rtt ${rtt:-?} ms"
	else
		printf '%s\n' "$2" | sed -n '1,12p'
		fail "$1 did not answer"
	fi
}

check "node $flight -> node $second" "$(ping_from_board "$l4_console" "$second")" "$second"
check "node $second -> node $flight" "$(ping_from_board "$f7_console" "$flight")" "$flight"

# csp interfaces first: a ping can succeed over any route, and this is what
# shows the CAN interface itself was registered.
printf '%s\n' "csp interfaces" "csp ping $flight" "csp ping $second" >"$work/commands"
timeout 90 "$ground_exe" --uart_stdinout "--can-if=$interface" --stop_at=40.0 --no-color \
	-flash="$work/ground.bin" <"$work/commands" >"$work/session.log" 2>&1 || true
sed -i 's/\x1b\[[0-9;]*[A-Za-z]//g' "$work/session.log"
session="$(cat "$work/session.log")"
grep -q "^CAN addr=" <<<"$session" || fail "the ground node registered no CAN interface"
check "ground $ground -> node $flight" "$session" "$flight"
check "ground $ground -> node $second" "$session" "$second"

echo "CAN MESH RESULT: PASS csp=$version nodes=$flight,$second,$ground bitrate=$bitrate"
