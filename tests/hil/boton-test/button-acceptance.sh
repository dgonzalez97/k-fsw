#!/usr/bin/env bash
# Manual hardware acceptance for the boton_test reference module.
#
# Debounce, one-count-per-press and hold behaviour cannot be proven by an
# automated test: they need a finger on the physical USER button. This fixture
# flashes the opt-in NUCLEO profile, records a timeline of every press the
# module reports, and drives the developer LEDs through both the shell and the
# parameter table so an operator can confirm them by eye.
#
# It deliberately does not try to keep step with the operator. An earlier
# version opened a window per gesture and advanced when the counter moved,
# which straddled gestures whenever the operator worked ahead of it and
# produced per-step deltas that were wrong even though the total was right.
# This records one line per observed press with its device-side timestamp, and
# the gestures are read back out of that timeline afterwards.
#
# It reports what it recorded. It never decides that the acceptance passed.
#
# Required environment:
#   KFSW_DEBUG_SERIAL  NUCLEO ST-LINK virtual COM port, by-id path only.

set -euo pipefail

KFSW_TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../tools" && pwd)"
source "$KFSW_TOOLS_DIR/_common.sh" nucleo_l496zg

debug_serial="${KFSW_DEBUG_SERIAL:-${KFSW_SERIAL:-}}"
build_dir="$KFSW_ROOT/build/hil/boton/nucleo_l496zg"
work_dir="$(mktemp -d)"
debug_stty=""
capture_pid=""
do_flash=1
record_s="${KFSW_BOTON_RECORD_S:-120}"

# How long to sit still at the start, proving the input invents nothing.
readonly BASELINE_S=10
# Counter poll interval. Fast enough to separate a deliberate double-press.
readonly POLL_S=0.3

usage()
{
	cat <<'EOF'
Usage: button-acceptance.sh [--no-flash] [--record-seconds N]

  --no-flash          Use the image already on the board.
  --record-seconds N  Length of the press recording window (default 120).
EOF
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	--no-flash)
		do_flash=0
		;;
	--record-seconds)
		record_s="${2:?--record-seconds requires a value}"
		shift
		;;
	-h | --help)
		usage
		exit 0
		;;
	*)
		usage >&2
		exit 2
		;;
	esac
	shift
done

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
	printf 'BOTON ACCEPTANCE: ABORTED %s\n' "$1" >&2
	exit 1
}

banner()
{
	printf '\n=== %s ===\n' "$1"
}

send()
{
	printf '%s\r' "$1" >"$debug_serial"
	sleep 0.4
}

latest_field()
{
	grep -a "^$1: " "$work_dir/nucleo.log" | tail -1 | sed "s/^$1: //" | tr -d '\r'
}

if [[ -z "$debug_serial" ]]; then
	fail "KFSW_DEBUG_SERIAL is not set"
fi
if [[ "$debug_serial" != /dev/serial/by-id/* ]]; then
	fail "the serial device must be a stable /dev/serial/by-id path"
fi
if [[ ! -e "$debug_serial" ]]; then
	fail "$debug_serial is not present"
fi

if [[ "$do_flash" -eq 1 ]]; then
	banner "Build"
	KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-boton-test.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$KFSW_REPO_DIR/config/profiles/nucleo-boton-test.overlay" \
		"$KFSW_TOOLS_DIR/build.sh" nucleo_l496zg >"$work_dir/build.log" 2>&1 || \
		fail "the boton_test profile did not build"
fi

build_sha="$(git -C "$KFSW_REPO_DIR" rev-parse --short HEAD)"
debounce_ms="$(sed -n 's/^CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS=\(.*\)$/\1/p' \
	"$build_dir/zephyr/.config")"
printf 'build: %s  debounce: %s ms  recording: %s s\n' \
	"$build_sha" "$debounce_ms" "$record_s"

debug_stty="$(stty -F "$debug_serial" -g)" || fail "cannot read the serial settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo

timeout $((record_s + 300))s cat "$debug_serial" >"$work_dir/nucleo.log" &
capture_pid=$!

if [[ "$do_flash" -eq 1 ]]; then
	banner "Flash"
	west flash -d "$build_dir" --runner openocd >"$work_dir/flash.log" 2>&1 || \
		fail "NUCLEO flash failed"

	for _ in $(seq 1 40); do
		grep -q "@READY " "$work_dir/nucleo.log" && break
		sleep 0.5
	done
	grep -q "@READY " "$work_dir/nucleo.log" || \
		fail "NUCLEO did not report readiness"
fi

send ""
send "boton_test status"
sleep 1
grep -aq "press_count:" "$work_dir/nucleo.log" || \
	fail "boton_test is not present in the running image"

banner "Baseline"
printf 'Leave the board untouched for %s s.\n' "$BASELINE_S"
baseline_before="$(latest_field press_count)"
sleep "$BASELINE_S"
send "boton_test status"
sleep 0.8
baseline_after="$(latest_field press_count)"
if [[ "$baseline_before" == "$baseline_after" ]]; then
	printf 'baseline: press_count held at %s, no spurious counts\n' "$baseline_after"
else
	printf 'baseline: press_count moved %s -> %s WITHOUT A PRESS\n' \
		"$baseline_before" "$baseline_after"
fi

banner "Press recording"
cat <<EOF
Perform these gestures in order, at any pace. Every press is timestamped as the
module reports it, so nothing has to line up with a clock:

  1. one press
  2. three presses, about a second apart
  3. one press held for five seconds, then released
  4. two presses as fast as you can

That is eight presses. Recording for ${record_s} s.
EOF

start="$(date +%s.%N)"
last_count="$baseline_after"
printf '\n%9s %7s %10s %6s\n' HOST_S COUNT DEVICE_S DELTA
while true; do
	now="$(date +%s.%N)"
	elapsed="$(awk -v a="$now" -v b="$start" 'BEGIN{printf "%.1f", a-b}')"
	awk -v e="$elapsed" -v r="$record_s" 'BEGIN{exit !(e>=r)}' && break

	printf 'boton_test status\r' >"$debug_serial"
	sleep "$POLL_S"
	count="$(latest_field press_count)"
	[[ -z "$count" ]] && continue

	if [[ "$count" != "$last_count" ]]; then
		printf '%9s %7s %10s %6s\n' "$elapsed" "$count" \
			"$(latest_field last_press_s)" "+$(( count - last_count ))"
		last_count="$count"
	fi
done

send "boton_test status"
sleep 0.8
final_count="$(latest_field press_count)"
printf '\ntotal presses recorded: %s (expected 8)\n' \
	$(( final_count - baseline_after ))

banner "LEDs through the shell"
for led in green blue red; do
	printf 'shell: %s on for 3 s\n' "$led"
	send "test led $led on"
	sleep 3
	send "boton_test status"
	sleep 0.6
	printf '  green=%s blue=%s red=%s\n' "$(latest_field led_green)" \
		"$(latest_field led_blue)" "$(latest_field led_red)"
	send "test led $led off"
	sleep 1
done

banner "LEDs through the parameter table"
# A parameter write must reach the same hardware the shell command drives.
for led in green blue red; do
	printf 'param: hw_test_led_%s = 1 for 3 s\n' "$led"
	send "param set hw_test_led_$led 1"
	sleep 3
	send "boton_test status"
	sleep 0.6
	printf '  green=%s blue=%s red=%s\n' "$(latest_field led_green)" \
		"$(latest_field led_blue)" "$(latest_field led_red)"
	send "param set hw_test_led_$led 0"
	sleep 1
done

banner "Parameter agreement"
send "param get press_count"
send "param get last_press_s"
sleep 1
grep -a "press_count = \|last_press_s = " \
	"$work_dir/nucleo.log" | tail -2
printf 'module status press_count: %s\n' "$final_count"

transcript="${KFSW_BOTON_LOG:-$KFSW_ROOT/build/hil/boton/acceptance.log}"
mkdir -p "$(dirname "$transcript")"
cp "$work_dir/nucleo.log" "$transcript"

printf '\nBOTON ACCEPTANCE: RECORDED build=%s debounce=%sms presses=%s\n' \
	"$build_sha" "$debounce_ms" $(( final_count - baseline_after ))
printf 'transcript: %s\n' "$transcript"
printf 'The press timeline and the LED observation are judged by the operator.\n'
