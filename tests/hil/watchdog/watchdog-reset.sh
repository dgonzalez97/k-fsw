#!/usr/bin/env bash
# Hardware acceptance for the platform watchdog.
#
# Issue kfsw-platform#3 is done when a HIL test can intentionally trigger a
# watchdog reset and the next boot reports the watchdog reset reason. That is
# what this checks, in one uninterrupted serial capture so the reset and the
# boot that follows it are the same observation rather than two hopeful ones.
#
# The sequence is:
#
#   1. flash and wait for @READY;
#   2. confirm the watchdog is armed and being fed, and that the feed counter
#      actually advances, so a reset later cannot be credited to a watchdog
#      that was never running;
#   3. confirm the board survives longer than the timeout while fed, which is
#      what rules out a watchdog that resets regardless;
#   4. stop the feed;
#   5. observe the reset land inside a bounded window; and
#   6. observe the next boot name the watchdog as the cause.
#
# Step 3 is the one that makes the rest mean anything. Without it a board that
# resets every few seconds for an unrelated reason would pass.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
build_dir="$KFSW_ROOT/build/hil/watchdog/nucleo_l496zg"
work_dir="$(mktemp -d)"
debug_stty=""
capture_pid=""
do_flash=1

cleanup()
{
	[[ -n "$capture_pid" ]] && kill "$capture_pid" 2>/dev/null || true
	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	rm -rf "$work_dir"
}
trap cleanup EXIT

fail()
{
	printf 'WATCHDOG HIL: FAIL %s\n' "$1" >&2
	exit 1
}

banner()
{
	printf '\n=== %s ===\n' "$1"
}

send()
{
	printf '%s\r' "$1" >"$debug_serial"
	sleep 0.5
}

field()
{
	grep -a "^$1: " "$work_dir/nucleo.log" | tail -1 | sed "s/^$1: //" | tr -d '\r'
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-flash)
		do_flash=0
		;;
	*)
		printf 'unknown argument: %s\n' "$1" >&2
		exit 2
		;;
	esac
	shift
done

[[ -n "$debug_serial" ]] || fail "KFSW_DEBUG_SERIAL is not set"
[[ "$debug_serial" == /dev/serial/by-id/* ]] || \
	fail "the serial device must be a stable /dev/serial/by-id path"
[[ -e "$debug_serial" ]] || fail "$debug_serial is not present"

if [[ "$do_flash" -eq 1 ]]; then
	banner "Build"
	KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-watchdog.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-watchdog.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 || \
		fail "the watchdog profile did not build"
fi

build_sha="$(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD)"
timeout_ms="$(sed -n 's/^CONFIG_KFSW_WATCHDOG_TIMEOUT_MS=\(.*\)$/\1/p' \
	"$build_dir/zephyr/.config")"
[[ -n "$timeout_ms" ]] || fail "the built image has no watchdog timeout configured"
printf 'build: %s  timeout: %s ms\n' "$build_sha" "$timeout_ms"

debug_stty="$(stty -F "$debug_serial" -g)" || fail "cannot read the serial settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo

timeout 240s cat "$debug_serial" >"$work_dir/nucleo.log" &
capture_pid=$!

if [[ "$do_flash" -eq 1 ]]; then
	banner "Flash"
	west flash -d "$build_dir" --runner openocd >"$work_dir/flash.log" 2>&1 || \
		fail "NUCLEO flash failed"
fi

for _ in $(seq 1 60); do
	grep -q "@READY " "$work_dir/nucleo.log" && break
	sleep 0.5
done
grep -q "@READY " "$work_dir/nucleo.log" || fail "NUCLEO did not report readiness"

banner "Armed and feeding"
send ""
send "watchdog status"
sleep 1

state="$(field state)"
device="$(field device)"
[[ "$device" == "bound" ]] || fail "no watchdog device is bound (device=$device)"
[[ "$state" == "running" ]] || fail "the watchdog is not running (state=$state)"

feeds_first="$(field feeds)"
printf 'state=%s device=%s feeds=%s timeout_ms=%s\n' \
	"$state" "$device" "$feeds_first" "$(field timeout_ms)"

# Survive longer than one timeout while being fed. Without this the reset
# observed later would prove nothing: a board resetting on its own would look
# identical.
survive_s=$(( (timeout_ms / 1000) * 2 + 2 ))
banner "Survives ${survive_s}s of normal operation"
boots_before="$(grep -ac "@BOOT " "$work_dir/nucleo.log")"
sleep "$survive_s"
boots_after="$(grep -ac "@BOOT " "$work_dir/nucleo.log")"
[[ "$boots_before" == "$boots_after" ]] || \
	fail "the board reset while the watchdog was being fed"

send "watchdog status"
sleep 1
feeds_second="$(field feeds)"
[[ "$feeds_second" -gt "$feeds_first" ]] || \
	fail "the feed counter did not advance ($feeds_first -> $feeds_second)"
printf 'survived %s s, feeds %s -> %s, no reset\n' \
	"$survive_s" "$feeds_first" "$feeds_second"

banner "Deliberate starvation"
reset_deadline=$(( (timeout_ms / 1000) + 5 ))
send "watchdog starve confirm"

reset_seen=0
for _ in $(seq 1 $(( reset_deadline * 2 ))); do
	if [[ "$(grep -ac "@BOOT " "$work_dir/nucleo.log")" -gt "$boots_after" ]]; then
		reset_seen=1
		break
	fi
	sleep 0.5
done
[[ "$reset_seen" == 1 ]] || \
	fail "no reset within ${reset_deadline}s of stopping the feed"

sleep 2
boot_line="$(grep -a "@BOOT " "$work_dir/nucleo.log" | tail -1)"
printf 'reset observed within %s s\n%s\n' "$reset_deadline" "$boot_line"

banner "Reset cause on the next boot"
grep -q "reset_cause=watchdog" <<<"$boot_line" || \
	fail "the boot after the reset did not name the watchdog: $boot_line"

if grep -aq "event " "$work_dir/nucleo.log"; then
	send "event list"
	sleep 1
fi

banner "Recovered and rearmed"
send "watchdog status"
sleep 1
[[ "$(field state)" == "running" ]] || \
	fail "the watchdog did not rearm after the reset"

printf '\nWATCHDOG HIL: PASS build=%s timeout=%sms survived=%ss cause=watchdog\n' \
	"$build_sha" "$timeout_ms" "$survive_s"
printf '%s\n' "$boot_line"
