#!/usr/bin/env bash
# CSP over CAN between a NUCLEO-L496ZG and a host CAN adapter.
#
# There is no transceiver on the NUCLEO: PD0 and PD1 are logic level and a
# transceiver sits between them and CAN_H/CAN_L. Wiring, the adapter and the
# bus termination are the operator's, and this script reports what it observed
# rather than assuming any of it.
#
#   sudo tests/hil/stm32/nucleo-l496zg/can-up.sh 500000 normal
#   tests/hil/stm32/nucleo-l496zg/can-smoke.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../../../../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# shellcheck source=/dev/null
set -a; . "$here/can-bench.env"; set +a
interface="${KFSW_CAN_INTERFACE:-can0}"
bitrate="${KFSW_CAN_BITRATE:-500000}"
flight="${KFSW_CAN_FLIGHT_NODE:-2}"
ground="${KFSW_CAN_GROUND_NODE:-16}"

fail() { echo "CAN SMOKE RESULT: FAIL - $*"; exit 1; }
skip() { echo "CAN SMOKE RESULT: NOT RUN - $*"; exit 0; }

command -v ip >/dev/null || skip "ip is not available"
ip link show "$interface" >/dev/null 2>&1 || skip "no $interface; is the adapter plugged in?"

# A CAN transmitter needs another node to acknowledge its frames, so a bus with
# nothing on it is not a quiet failure but an accumulating one. Refuse to start
# rather than report errors that only mean the link was never up.
state="$(ip -details link show "$interface" | awk '/can state/ {print $3; exit}')"
[[ "$state" == "ERROR-ACTIVE" ]] || skip "$interface is $state; bring it up with can-up.sh"

echo "CAN SMOKE: $interface at ${bitrate} bps, ground $ground to flight $flight"

cd "$root"
# shellcheck source=/dev/null
[[ -f .venv/bin/activate ]] && . .venv/bin/activate

profile_conf="$root/k-fsw/config/profiles/nucleo-can.conf"
profile_overlay="$root/k-fsw/config/profiles/nucleo-can.overlay"

if [[ "${1:-}" != "--no-build" ]]; then
	KFSW_EXTRA_CONF_FILE="$profile_conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$profile_overlay" \
	KFSW_BUILD_DIR="$root/build/hil/can/nucleo" \
		"$root/k-fsw/tools/build.sh" nucleo_l496zg >"$work/build.log" 2>&1 ||
		fail "the flight image did not build; see $work/build.log"
	west flash --build-dir "$root/build/hil/can/nucleo" --runner openocd \
		>"$work/flash.log" 2>&1 || fail "could not flash the NUCLEO"
	"$root/k-fsw/tools/k-ground" build kfsw-gnd-can >"$work/ground.log" 2>&1 ||
		fail "the ground node did not build; see $work/ground.log"
	sleep 3
fi

ground_exe="$root/build/k-ground/node-$ground/zephyr/zephyr.exe"
[[ -x "$ground_exe" ]] || fail "ground node $ground has not been built"

printf '%s\n' \
	"csp interfaces" \
	"csp ping $flight" \
	"csp ident $flight" \
	"param get $flight can_speed" \
	"param get $flight uid" \
	> "$work/commands"

timeout 90 "$ground_exe" --uart_stdinout "--can-if=$interface" --stop_at=45.0 --no-color \
	-flash="$work/ground.bin" < "$work/commands" > "$work/session.log" 2>&1 || true
sed -i 's/\x1b\[[0-9;]*[A-Za-z]//g' "$work/session.log"

grep -q "^CAN addr=" "$work/session.log" || fail "the ground node registered no CAN interface"
rtt="$(sed -n "s/^CSP ping $flight: success, rtt_ms=\([0-9]*\).*/\1/p" "$work/session.log" | head -1)"
[[ -n "$rtt" ]] || fail "no ping reply from node $flight over CAN"
grep -q "hostname: kfsw-$flight" "$work/session.log" ||
	fail "node $flight did not identify itself"
speed="$(sed -n "s/^$flight:can_speed = \([0-9]*\).*/\1/p" "$work/session.log" | head -1)"
[[ "$speed" == "$bitrate" ]] || fail "node $flight reports can_speed=$speed, expected $bitrate"
grep -q "^$flight:uid = " "$work/session.log" || fail "no string parameter read over CAN"

# Frames the adapter actually saw, which is the only evidence the bus carried
# anything rather than the two nodes agreeing in simulation.
read -r rx tx < <(ip -s link show "$interface" |
	awk '/RX:/{getline; r=$2} /TX:/{getline; t=$2} END{print r, t}')
errors="$(ip -details link show "$interface" |
	awk '/berr-counter/ {gsub(/[()]/, ""); print "tx" $6 "/rx" $8; exit}')"

echo "CAN SMOKE RESULT: PASS interface=$interface bitrate=$bitrate rtt_ms=$rtt" \
	"ident=yes params=yes packets=rx:$rx/tx:$tx berr=$errors"
