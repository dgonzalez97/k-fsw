#!/usr/bin/env bash
# Does the board's clock outlive a reset?
#
# It has to. Everything gated on a valid clock stays quiet without one:
# scheduled collection produces nothing, and a beacon says nothing. A node that
# reboots on a watchdog between passes would otherwise sit silent until a
# ground station came into view to tell it the time — which is exactly when
# somebody would rather hear from it.
#
# The reset here is commanded rather than a power cycle, and that is the case
# under test: the RTC's counter lives in a backup domain a reset does not
# clear. A power cycle is a board question, because the domain only survives
# one where VBAT is backed.
#
# Hardware: NUCLEO-L496ZG on ST-LINK. No radio.
set -Eeuo pipefail

CLOCK_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
CLOCK_DIR="$(dirname "$CLOCK_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$CLOCK_DIR")")")")"

source "$KFSW_REPO_DIR/tools/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-$KFSW_SERIAL}"
work_dir="$(mktemp -d /tmp/kfsw-clock-smoke.XXXXXX)"
capture_pid=""
serial_stty=""
failures=0
build_image=yes

cleanup()
{
	[[ -n "$capture_pid" ]] && kill "$capture_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	[[ -n "$serial_stty" ]] && stty -F "$debug_serial" "$serial_stty" 2>/dev/null || true
	rm -rf -- "$work_dir"
}
trap cleanup EXIT

while [[ $# -gt 0 ]]; do
	case "$1" in
	--serial)
		debug_serial="${2:?--serial requires a device path}"
		shift 2
		;;
	--no-build)
		build_image=no
		shift
		;;
	-h|--help)
		echo "Usage: clock-smoke.sh [--serial PATH] [--no-build]"
		exit 0
		;;
	*)
		echo "ERROR: unknown argument: $1" >&2
		exit 2
		;;
	esac
done

fail()
{
	printf '  [FAIL] %s\n' "$1" >&2
	failures=$((failures + 1))
}

wait_for()
{
	local needle="$1" deadline=$((SECONDS + ${2:-30}))

	while ((SECONDS < deadline)); do
		grep -qa -- "$needle" "$work_dir/node.log" && return 0
		sleep 0.5
	done
	return 1
}

[[ -e "$debug_serial" ]] || { echo "ERROR: no board at $debug_serial" >&2; exit 2; }

if [[ "$build_image" == yes ]]; then
	KFSW_PRISTINE=always "$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg \
		>"$work_dir/build.log" 2>&1 || { cat "$work_dir/build.log" >&2; exit 1; }
fi

grep -q '^CONFIG_RTC=y$' "$KFSW_ROOT/build/nucleo_l496zg/zephyr/.config" ||
	{ echo "ERROR: this image has no RTC; the clock cannot outlive a reset" >&2; exit 2; }

serial_stty="$(stty -F "$debug_serial" -g)"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
timeout 240s cat "$debug_serial" >"$work_dir/node.log" &
capture_pid=$!

west flash -d "$KFSW_ROOT/build/nucleo_l496zg" --runner openocd >"$work_dir/flash.log" 2>&1 ||
	{ cat "$work_dir/flash.log" >&2; exit 1; }
wait_for "@READY " 60 || { echo "ERROR: the board never reported readiness" >&2; exit 1; }

echo "CLOCK SMOKE"

printf '%s\r\n' 'param set echo_enabled 1' >"$debug_serial"
sleep 1

# A time far enough above the floor that it cannot be mistaken for an unset
# clock, and recent enough to be a plausible date in a log.
set_seconds=1788400000
printf '%s\r\n' "csp clock set $set_seconds" >"$debug_serial"
wait_for "clock: " 20 || fail "the clock could not be set"
sleep 2

printf '%s\r\n' 'csp reboot 2 0000' >"$debug_serial"
wait_for "reset_cause=software" 60 || fail "the board did not reset on command"
wait_for "@READY " 60 || fail "the board did not come back"
sleep 2

# The claim: not merely preserved, but still counting. A value copied before
# the reboot and restored after would read the same as it was set; a running
# clock reads later.
printf '%s\r\n' 'csp clock' >"$debug_serial"
sleep 3
after="$(tr -d '\r' <"$work_dir/node.log" | sed 's/\x1b\[[0-9;]*m//g' |
	sed -n 's/^clock: .*(\([0-9]\{10\}\)\..*/\1/p' | tail -1)"

if [[ -z "$after" ]]; then
	fail "the clock reported nothing after the reset"
elif [[ "$after" -lt "$set_seconds" ]]; then
	fail "the clock went backwards across the reset ($after < $set_seconds)"
elif [[ "$after" -eq "$set_seconds" ]]; then
	fail "the clock was restored but is not running ($after)"
else
	printf '  [ok]   the clock kept running across the reset (+%ss)\n' \
		"$((after - set_seconds))"
fi

# And the service that depends on it says so without being asked.
if tr -d '\r' <"$work_dir/node.log" | grep -qa 'clock is set, collecting on schedule'; then
	printf '  [ok]   housekeeping resumed on its own\n'
else
	fail "housekeeping did not resume after the reset"
fi

if [[ "$failures" -eq 0 ]]; then
	echo "CLOCK SMOKE RESULT: PASS after=$after set=$set_seconds"
else
	echo "CLOCK SMOKE RESULT: FAIL ($failures)"
	exit 1
fi
