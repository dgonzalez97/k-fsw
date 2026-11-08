#!/usr/bin/env bash
# Hardware test for health monitoring: the board must stay up past the watchdog
# timeout while healthy, reset through the watchdog when a registered component
# stops reporting, name the watchdog on the next boot, and publish each
# component this composition names as a slot of table 31.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

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

# `param get <name>` answers "<name> = <value>", so one read is a send, a wait
# and a match against that shape.
param_get()
{
	mark
	send "param get $1"
	sleep 1.5
	since_mark | sed -n "s/.*$1 = \\([^[:space:]]*\\).*/\\1/p" | tail -1
}


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
	# The watchdog and health are in the NUCLEO profile; nothing to layer.
	# Pristine because a build directory left by an earlier version of this
	# case keeps its own cached profile, and a stale one fails for a reason
	# this case cannot see.
	KFSW_BUILD_DIR="$build_dir" KFSW_PRISTINE=always \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 || \
		{ cat "$work_dir/build.log" >&2; abort "the health composition did not build"; }
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
	kfsw_west_flash -d "$build_dir" --runner openocd >"$work_dir/flash.log" 2>&1 || \
		abort "NUCLEO flash failed"
fi

for _ in $(seq 1 60); do
	grep -q "@READY " "$work_dir/nucleo.log" && break
	sleep 0.5
done
grep -q "@READY " "$work_dir/nucleo.log" || abort "the node did not report readiness"

# Two boards answer on this bench, and not with the same composition.
answered="$(sed -n 's/.*@BOOT .*board=\([^ ]*\).*/\1/p' "$work_dir/nucleo.log" | head -1)"
[[ "$answered" == "$ZEPHYR_BOARD"* ]] ||
	abort "${answered:-nothing} answered on ${debug_serial}, expected $ZEPHYR_BOARD"

banner "Health feeds the watchdog"
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

banner "Each named slot is published in table 31"
# The slots are compiled from the composition's header, so the names here are
# the names that header lists. A slot reads 0 while its component reports
# inside its deadline, 1 once it is overdue and 2 when nothing of that name is
# registered at all.
for slot in app csp; do
	state="$(param_get "health_${slot}_state")"
	case "$state" in
		0) pass "health_${slot}_state reads ok on hardware" ;;
		1) fail "health_${slot}_state reads overdue while the board is healthy" ;;
		2) fail "health_${slot}_state reads absent; nothing registered ${slot}" ;;
		*) fail "health_${slot}_state answered '${state:-nothing}'" ;;
	esac
done

# The age has to be a real sample rather than a constant, and it has to sit
# inside the deadline the component registered with.
first_age="$(param_get health_app_since_ms)"
sleep 2
second_age="$(param_get health_app_since_ms)"
if [[ "$first_age" =~ ^[0-9]+$ && "$second_age" =~ ^[0-9]+$ ]]; then
	if [[ "$first_age" -lt "$deadline_ms" ]]; then
		pass "health_app_since_ms is ${first_age} ms, inside its ${deadline_ms} ms deadline"
	else
		fail "health_app_since_ms is ${first_age} ms, past its ${deadline_ms} ms deadline"
	fi
	if [[ "$second_age" != "$first_age" ]]; then
		pass "health_app_since_ms is sampled, not a constant"
	else
		fail "health_app_since_ms did not change across two seconds"
	fi
else
	fail "health_app_since_ms answered '${first_age:-nothing}' and '${second_age:-nothing}'"
fi

banner "A component that stops reporting resets the board"
printf 'this takes about %s s\n' "$(( (deadline_ms + timeout_ms) / 1000 + 5 ))"

# There is no command to stall the application thread, so register a second
# component that never reports.
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

for slot in app csp; do
	state="$(param_get "health_${slot}_state")"
	if [[ "$state" == 0 ]]; then
		pass "health_${slot}_state reads ok again after the reset"
	else
		fail "health_${slot}_state reads '${state:-nothing}' after the reset"
	fi
done

# `health watch` registered a component at run time. Slots are compiled, so no
# parameter may appear for it: a table that grew at run time would break the
# document and the wire offsets with it.
mark
send "param get health_stuck_state"
sleep 1.5
if since_mark | grep -aq "health_stuck_state = "; then
	fail "a component registered at run time created a parameter"
else
	pass "a run time registration created no parameter"
fi

banner "Result"
if [[ "$failures" -eq 0 ]]; then
	printf 'HEALTH HIL: PASS build=%s survived=%ss watchdog=%sms deadline=%sms\n' \
		"$build_sha" "$survive_s" "$timeout_ms" "$deadline_ms"
else
	printf 'HEALTH HIL: FAIL %d check(s) failed\n' "$failures"
	exit 1
fi
