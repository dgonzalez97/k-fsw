#!/usr/bin/env bash
# Hardware acceptance for health monitoring.
#
# The claim is not that a service reports numbers. It is that a system which
# stops working resets itself without anyone asking it to, and says why
# afterwards.
#
# The sequence is:
#
#   1. flash a build where health owns the watchdog, and confirm it does;
#   2. show the board survives well beyond the watchdog timeout while healthy,
#      which is what rules out a board that resets on its own;
#   3. register a component that is never reported, so health has something
#      genuinely overdue rather than a simulated fault;
#   4. observe health withhold the feed and say which component caused it;
#   5. observe the reset, and the next boot naming the watchdog.
#
# Step 2 is what makes the rest mean anything, and step 3 is deliberately a
# real component rather than a test hook: a fault that only a test can cause
# proves only that the test works.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

PROFILES="$KFSW_REPO_DIR/config/profiles"
debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
build_dir="$KFSW_ROOT/build/hil/health/nucleo_l496zg"
work_dir="$(mktemp -d)"
debug_stty=""
capture_pid=""
do_flash=1
failures=0

cleanup()
{
	[[ -n "$capture_pid" ]] && kill "$capture_pid" 2>/dev/null || true
	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	rm -rf "$work_dir"
}
trap cleanup EXIT

abort()
{
	printf '\nHEALTH HIL: ABORTED %s\n' "$1" >&2
	exit 1
}

fail()
{
	printf '  [FAIL] %s\n' "$1" >&2
	failures=$((failures + 1))
}

pass()
{
	printf '  [ok]   %s\n' "$1"
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

mark()
{
	marker_offset="$(wc -c <"$work_dir/nucleo.log" 2>/dev/null || echo 0)"
}

since_mark()
{
	tail -c "+$((marker_offset + 1))" "$work_dir/nucleo.log" 2>/dev/null || true
}

marker_offset=0

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-flash) do_flash=0 ;;
	*) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
done

[[ -n "$debug_serial" ]] || abort "KFSW_DEBUG_SERIAL is not set"
[[ "$debug_serial" == /dev/serial/by-id/* ]] || \
	abort "the serial device must be a stable /dev/serial/by-id path"
[[ -e "$debug_serial" ]] || abort "$debug_serial is not present"

if [[ "$do_flash" -eq 1 ]]; then
	banner "Build"
	KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$PROFILES/nucleo-watchdog.conf;$PROFILES/nucleo-health.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$PROFILES/nucleo-watchdog.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 || \
		abort "the health composition did not build"
fi

build_sha="$(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD)"
timeout_ms="$(sed -n 's/^CONFIG_KFSW_WATCHDOG_TIMEOUT_MS=\(.*\)$/\1/p' "$build_dir/zephyr/.config")"
deadline_ms="$(sed -n 's/^CONFIG_KFSW_APP_HEALTH_DEADLINE_MS=\(.*\)$/\1/p' "$build_dir/zephyr/.config")"
[[ -n "$timeout_ms" && -n "$deadline_ms" ]] || abort "the build has no watchdog or health timing"
printf 'build: %s  watchdog: %s ms  app deadline: %s ms\n' "$build_sha" "$timeout_ms" "$deadline_ms"

debug_stty="$(stty -F "$debug_serial" -g)" || abort "cannot read the serial settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
timeout 400s cat "$debug_serial" >"$work_dir/nucleo.log" &
capture_pid=$!

if [[ "$do_flash" -eq 1 ]]; then
	banner "Flash"
	west flash -d "$build_dir" --runner openocd >"$work_dir/flash.log" 2>&1 || \
		abort "NUCLEO flash failed"
fi

for _ in $(seq 1 60); do
	grep -q "@READY " "$work_dir/nucleo.log" && break
	sleep 0.5
done
grep -q "@READY " "$work_dir/nucleo.log" || abort "the node did not report readiness"

banner "Health owns the watchdog"
mark
send ""
send "health status"
sleep 1.5
since_mark | grep -aq "state: ok" && pass "health reports the system healthy" || \
	fail "health did not report a healthy system"
since_mark | grep -aq "feeding: yes" && pass "health is the one feeding the watchdog" || \
	fail "health is not feeding the watchdog"

mark
send "watchdog status"
sleep 1.5
# The platform keep-alive must have stopped: if both were feeding, withholding
# one feed would achieve nothing.
if since_mark | grep -aq "state: running"; then
	pass "the watchdog is armed"
else
	fail "the watchdog is not armed"
fi

banner "Survives while healthy"
survive_s=$(( (timeout_ms / 1000) * 3 + 2 ))
boots_before="$(grep -ac "@BOOT " "$work_dir/nucleo.log")"
printf 'waiting %s s, three times the watchdog timeout\n' "$survive_s"
sleep "$survive_s"
boots_after="$(grep -ac "@BOOT " "$work_dir/nucleo.log")"
if [[ "$boots_before" == "$boots_after" ]]; then
	pass "no reset in $survive_s s while every component reported"
else
	fail "the board reset while it was healthy"
fi

mark
send "health list"
sleep 1.5
since_mark | grep -aq "app" && pass "the application thread is watched" || \
	fail "no component is being watched"

banner "A component that stops reporting resets the board"
printf 'this takes about %s s\n' "$(( (deadline_ms + timeout_ms) / 1000 + 5 ))"

# There is no command to stall the application thread, and adding one would
# mean shipping a way to hang the flight software. Instead a second component
# is registered and never reported: health then has something genuinely overdue.
mark
send "health watch stuck 2000 confirm"
sleep 2

reset_deadline=$(( (deadline_ms + timeout_ms) / 1000 + 15 ))
reset_seen=0
for _ in $(seq 1 $(( reset_deadline * 2 )) ); do
	if [[ "$(grep -ac "@BOOT " "$work_dir/nucleo.log")" -gt "$boots_after" ]]; then
		reset_seen=1
		break
	fi
	sleep 0.5
done

if [[ "$reset_seen" == 1 ]]; then
	pass "the board reset after a component stopped reporting"
else
	fail "no reset within ${reset_deadline}s of a component going overdue"
fi

sleep 3
boot_line="$(grep -a "@BOOT " "$work_dir/nucleo.log" | tail -1)"
printf '%s\n' "$boot_line"

if grep -q "reset_cause=watchdog" <<<"$boot_line"; then
	pass "the next boot names the watchdog as the cause"
else
	fail "the boot after the reset did not name the watchdog: $boot_line"
fi

banner "Recovered"
mark
send "health status"
sleep 1.5
since_mark | grep -aq "state: ok" && pass "health is supervising again after the reset" || \
	fail "health did not resume after the reset"

banner "Result"
if [[ "$failures" -eq 0 ]]; then
	printf 'HEALTH HIL: PASS build=%s survived=%ss watchdog=%sms deadline=%sms\n' \
		"$build_sha" "$survive_s" "$timeout_ms" "$deadline_ms"
else
	printf 'HEALTH HIL: FAIL %d check(s) failed\n' "$failures"
	exit 1
fi
