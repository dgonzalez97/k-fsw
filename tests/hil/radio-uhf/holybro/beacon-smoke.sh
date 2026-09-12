#!/usr/bin/env bash
# Does a node beacon over a real radio, and does the ground hear it?
#
# Every other beacon test runs hosted, where "the link" is a pseudo-terminal
# that never drops a byte. This is the claim that only a radio can settle: the
# node transmits on its own, over the air, and something on the ground that
# never asks for anything picks the frames up.
#
# The ground side is hk-bridge.py in --listen, which binds no port and sends no
# packet. If a frame reaches it, the node sent it unprompted.
#
# Hardware: NUCLEO-L496ZG on ST-LINK, one Holybro on the board's UART and its
# pair on host USB. Source tests/hil/radio-uhf/holybro/bench.env first.
set -Eeuo pipefail

HOLYBRO_BEACON_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
HOLYBRO_DIR="$(dirname "$HOLYBRO_BEACON_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$HOLYBRO_DIR")")")")"

source "$KFSW_REPO_DIR/tools/_common.sh" nucleo_l496zg

radio_device="${KGROUND_HOLYBRO_DEVICE:-}"
debug_serial="${KFSW_DEBUG_SERIAL:-$KFSW_SERIAL}"
radio_baud="${KGROUND_HOLYBRO_BAUD:-57600}"
nucleo_build_dir="$KFSW_ROOT/build/hil/holybro/nucleo_l496zg"
python="$KFSW_ROOT/.venv/bin/python"
work_dir="$(mktemp -d /tmp/k-fsw-holybro-beacon.XXXXXX)"
debug_capture_pid=""
debug_stty=""
radio_stty=""
failures=0
build_image=yes

# Ports outlive this script if they are not given back, and a held port looks
# exactly like a broken board on the next run.
cleanup()
{
	[[ -n "$debug_capture_pid" ]] && kill "$debug_capture_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	[[ -n "$debug_stty" ]] && stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	[[ -n "$radio_stty" ]] && stty -F "$radio_device" "$radio_stty" 2>/dev/null || true
	rm -rf -- "$work_dir"
}
trap cleanup EXIT

fail()
{
	printf 'HOLYBRO BEACON: %s\n' "$1" >&2
	failures=$((failures + 1))
}

check()
{
	if [[ "$2" == "$3" ]]; then
		printf '  [ok]   %s\n' "$1"
	else
		printf '  [FAIL] %s\n         wanted %s\n         got    %s\n' "$1" "$3" "$2" >&2
		failures=$((failures + 1))
	fi
}

wait_for_output()
{
	local file="$1" needle="$2" deadline=$((SECONDS + ${3:-30}))

	while ((SECONDS < deadline)); do
		grep -qa -- "$needle" "$file" && return 0
		sleep 0.5
	done
	return 1
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--radio)
		radio_device="${2:?--radio requires a device path}"
		shift 2
		;;
	--serial)
		debug_serial="${2:?--serial requires a device path}"
		shift 2
		;;
	--no-build)
		build_image=no
		shift
		;;
	-h|--help)
		echo "Usage: beacon-smoke.sh [--radio PATH] [--serial PATH] [--no-build]"
		exit 0
		;;
	*)
		echo "ERROR: unknown argument: $1" >&2
		exit 2
		;;
	esac
done

[[ -n "$radio_device" ]] || { echo "ERROR: set KGROUND_HOLYBRO_DEVICE" >&2; exit 2; }
[[ -e "$radio_device" ]] || { echo "ERROR: no radio at $radio_device" >&2; exit 2; }
[[ -e "$debug_serial" ]] || { echo "ERROR: no board at $debug_serial" >&2; exit 2; }
[[ -x "$python" ]] || python="python3"

if [[ "$build_image" == yes ]]; then
echo "HOLYBRO BEACON: building NUCLEO node 2 with peer 16"
KFSW_PRISTINE=always \
	KFSW_BUILD_DIR="$nucleo_build_dir" \
	KFSW_EXTRA_CONF_FILE="$HOLYBRO_DIR/nucleo_l496zg.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/nucleo_l496zg.overlay" \
	"$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 ||
	{ cat "$work_dir/build.log" >&2; exit 1; }
fi

[[ -f "$nucleo_build_dir/zephyr/.config" ]] ||
	{ echo "ERROR: no image at $nucleo_build_dir; run without --no-build" >&2; exit 2; }
grep -q '^CONFIG_KFSW_HK_BEACON=y$' "$nucleo_build_dir/zephyr/.config" ||
	{ echo "ERROR: this image does not carry the beacon" >&2; exit 2; }

debug_stty="$(stty -F "$debug_serial" -g)"
radio_stty="$(stty -F "$radio_device" -g)"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
stty -F "$radio_device" "$radio_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 300s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!

west flash -d "$nucleo_build_dir" --runner openocd >"$work_dir/flash.log" 2>&1 ||
	{ cat "$work_dir/flash.log" >&2; exit 1; }
wait_for_output "$work_dir/nucleo.log" "@READY " 60 || fail "the board never reported readiness"

echo "HOLYBRO BEACON"

printf '%s\r\n' 'param set echo_enabled 1' >"$debug_serial"
sleep 1

# Beacons follow the clock: a node that never learned the time does not
# announce itself without saying when.
printf '%s\r\n' "csp clock set $(date -u +%s)" >"$debug_serial"
sleep 1

printf '%s\r\n' 'hk define 0 1:0 3:0' 'hk period 0 2000' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "report 0 defines 2 values" 20 ||
	fail "the report was not defined"
sleep 3

# Under the floor first: the limit that stops an operator turning the node into
# a transmitter that swamps the link it shares.
printf '%s\r\n' 'hk beacon 0 16 250' >"$debug_serial"
sleep 2
if grep -qa 'beacon for report 0: -34' "$work_dir/nucleo.log"; then
	printf '  [ok]   an interval under the floor is refused\n'
else
	fail "an interval under the floor was accepted"
fi

printf '%s\r\n' 'hk beacon 0 16 5000' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "beacons to node 16 every 5000 ms" 20 ||
	fail "the beacon was not configured"

echo "HOLYBRO BEACON: listening, and asking for nothing"
timeout 60 "$python" "$KFSW_REPO_DIR/tools/ground/hk-bridge.py" \
	--device "$radio_device" --baud "$radio_baud" --node 2 \
	--listen --once --timeout 45 --yamcs none \
	>"$work_dir/bridge.log" 2>&1 || true

heard="$(grep -ca '^report 0 seq' "$work_dir/bridge.log" || true)"
if [[ "$heard" -ge 1 ]]; then
	printf '  [ok]   the ground heard a node it never asked (%s frames over the air)\n' "$heard"
else
	fail "nothing crossed the radio"
	sed -n '1,20p' "$work_dir/bridge.log" >&2
fi
if [[ "$heard" -ge 2 ]]; then
	printf '  [ok]   beacons keep coming\n'
else
	fail "one frame is not a period"
fi

frame="$(sed -n 's/^  \([0-9a-f]\{2,\}\)$/\1/p' "$work_dir/bridge.log" | tail -1)"
if [[ -n "$frame" ]]; then
	check "the beacon carries the protocol version" "${frame:0:2}" "01"
	check "the beacon names the report" "${frame:2:2}" "00"
fi

# The counters an operator reads when the ground goes quiet.
printf '%s\r\n' 'hk show' >"$debug_serial"
sleep 3
sent="$(tr -d '\r' <"$work_dir/nucleo.log" | sed -n 's/^beacons sent: //p' | tail -1)"
skipped="$(tr -d '\r' <"$work_dir/nucleo.log" | sed -n 's/^beacons skipped: //p' | tail -1)"
if [[ -n "$sent" ]] && [[ "$sent" -ge "$heard" ]]; then
	printf '  [ok]   the node counted what it sent (%s sent, %s skipped, %s heard)\n' \
		"$sent" "${skipped:-?}" "$heard"
else
	fail "the node reports ${sent:-no} sent against $heard heard"
fi

printf '%s\r\n' 'hk beacon 0 16 0' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "report 0 stops beaconing" 20 ||
	fail "the beacon could not be stopped"
printf '  [ok]   an operator can take it away\n'

if [[ "$failures" -eq 0 ]]; then
	echo "HOLYBRO BEACON RESULT: PASS heard=$heard sent=${sent:-?} skipped=${skipped:-?}"
else
	echo "HOLYBRO BEACON RESULT: FAIL ($failures)"
	exit 1
fi
