#!/usr/bin/env bash
# Checks what the board can tell you about a reset after it happened: the note
# saying why, and the log messages from before it.
#
# Neither can be checked in simulation. native_sim's sys_reboot re-executes the
# process, so its address space is new and nothing in .noinit ever survives. On
# the board the reset is NVIC_SystemReset, which leaves SRAM alone; whether that
# holds in practice is exactly what this fixture is for.
#
# Hardware: NUCLEO-L496ZG on ST-LINK. No radio, no CAN.
set -Eeuo pipefail

DIAG_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
DIAG_DIR="$(dirname "$DIAG_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$DIAG_DIR")")")")"

source "$KFSW_REPO_DIR/tools/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-$KFSW_SERIAL}"
work_dir="$(mktemp -d /tmp/kfsw-diagnostics-reset.XXXXXX)"
capture_pid=""
serial_stty=""
failures=0
build_image=yes

cleanup() {
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
	-h | --help)
		echo "Usage: diagnostics-reset-smoke.sh [--serial PATH] [--no-build]"
		exit 0
		;;
	*)
		echo "ERROR: unknown argument: $1" >&2
		exit 2
		;;
	esac
done

fail() {
	printf '  [FAIL] %s\n' "$1" >&2
	failures=$((failures + 1))
}

ok() { printf '  [ok]   %s\n' "$1"; }

clean_log() { tr -d '\r' <"$work_dir/node.log" | sed 's/\x1b\[[0-9;]*m//g'; }

wait_for() {
	local needle="$1" deadline=$((SECONDS + ${2:-30}))

	while ((SECONDS < deadline)); do
		grep -qa -- "$needle" "$work_dir/node.log" && return 0
		sleep 0.5
	done
	return 1
}

say() {
	printf '%s\r\n' "$1" >"$debug_serial"
	sleep "${2:-1}"
}

# The last value the shell printed for a parameter.
param_value() {
	clean_log | sed -n "s/^$1 = //p" | tail -1
}

# The end of the log window, which is the next sequence the ring will use.
history_end() {
	say 'log history' 3
	clean_log | sed -n 's/^Log history: first=\([0-9]*\) end=\([0-9]*\).*/\2/p' | tail -1
}

history_first() {
	clean_log | sed -n 's/^Log history: first=\([0-9]*\) end=\([0-9]*\).*/\1/p' | tail -1
}

[[ -e "$debug_serial" ]] || {
	echo "ERROR: no board at $debug_serial" >&2
	exit 2
}

if [[ "$build_image" == yes ]]; then
	KFSW_PRISTINE=always "$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg \
		>"$work_dir/build.log" 2>&1 || {
		cat "$work_dir/build.log" >&2
		exit 1
	}
fi

config="$KFSW_ROOT/build/nucleo_l496zg/zephyr/.config"
for option in CONFIG_KFSW_LASTWORDS CONFIG_KFSW_LOG_HISTORY CONFIG_KFSW_LOG_HISTORY_RETAINED; do
	grep -q "^$option=y\$" "$config" || {
		echo "ERROR: this image has no $option; there is nothing to check" >&2
		exit 2
	}
done

serial_stty="$(stty -F "$debug_serial" -g)"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
timeout 300s cat "$debug_serial" >"$work_dir/node.log" &
capture_pid=$!

west flash -d "$KFSW_ROOT/build/nucleo_l496zg" --runner openocd >"$work_dir/flash.log" 2>&1 ||
	{
		cat "$work_dir/flash.log" >&2
		exit 1
	}
wait_for "@READY " 60 || {
	echo "ERROR: the board never reported readiness" >&2
	exit 1
}

echo "DIAGNOSTICS RESET SMOKE"

say 'param set echo_enabled 1'

# Push the ring well past one boot's worth of messages, so a ring that did not
# survive cannot be mistaken for one that did.
for _ in 1 2 3 4 5 6; do
	say 'log test' 0.5
done

end_before="$(history_end)"
if [[ -z "$end_before" ]]; then
	fail "the log history reported nothing before the reset"
	end_before=0
elif [[ "$end_before" -lt 20 ]]; then
	fail "only $end_before records before the reset; too few to tell retention apart"
else
	ok "the ring holds $((end_before - 1)) records before the reset"
fi

say 'param get boot_count'
count_before="$(param_value boot_count)"

say 'csp reboot 2 0000' 2
wait_for "reset_cause=software" 60 || fail "the board did not reset on command"
wait_for "@READY " 60 || fail "the board did not come back"
sleep 2

# Why it restarted.
say 'param get last_reason'
reason="$(param_value last_reason)"
if [[ "$reason" == "1" ]]; then
	ok "the note says the reset was commanded"
else
	fail "last_reason is '${reason:-nothing}', expected 1 for commanded"
fi

say 'param get last_uptime_ms'
uptime_ms="$(param_value last_uptime_ms)"
if [[ -n "$uptime_ms" && "$uptime_ms" -gt 0 ]]; then
	ok "the note carries how long the previous run had been up (${uptime_ms} ms)"
else
	fail "last_uptime_ms is '${uptime_ms:-nothing}', expected the previous run's uptime"
fi

say 'param get boot_count'
count_after="$(param_value boot_count)"
if [[ -n "$count_before" && -n "$count_after" && "$count_after" -gt "$count_before" ]]; then
	ok "the restart was counted ($count_before to $count_after)"
else
	fail "boot_count went '${count_before:-nothing}' to '${count_after:-nothing}'"
fi

# The note is reported once, so a second read must not repeat it.
say 'param get last_reason'
reason_again="$(param_value last_reason)"
if [[ "$reason_again" == "1" ]]; then
	ok "the note is still readable while this run lasts"
else
	fail "last_reason changed to '${reason_again:-nothing}' on a second read"
fi

# What it was saying before it restarted.
end_after="$(history_end)"
first_after="$(history_first)"
if [[ -z "$end_after" ]]; then
	fail "the log history reported nothing after the reset"
elif [[ "$end_after" -le "$end_before" ]]; then
	fail "the ring restarted: end went $end_before to $end_after, so it did not survive"
else
	ok "the ring continued its numbering across the reset ($end_before to $end_after)"
fi

if [[ -n "$first_after" && -n "$end_before" && "$first_after" -lt "$end_before" ]]; then
	ok "records from before the reset are still readable (from $first_after)"
else
	fail "the oldest readable record is $first_after, all of it from after the reset"
fi

if [[ "$failures" -eq 0 ]]; then
	echo "DIAGNOSTICS RESET SMOKE RESULT: PASS reason=$reason end=$end_before..$end_after"
else
	echo "DIAGNOSTICS RESET SMOKE RESULT: FAIL ($failures)"
	exit 1
fi
