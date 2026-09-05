#!/usr/bin/env bash
set -Eeuo pipefail

if [[ $# -ne 1 ]]; then
	echo "Usage: $0 <target>"
	exit 2
fi

KFSW_TARGET="$1"
KFSW_FLASH="${KFSW_FLASH:-1}"

if [[ "$KFSW_FLASH" != 0 && "$KFSW_FLASH" != 1 ]]; then
	echo "ERROR: KFSW_FLASH must be 0 or 1"
	exit 2
fi

source "$(dirname "$0")/../../tools/_common.sh" "$KFSW_TARGET"

capture_file="$(mktemp)"
capture_pid=""
serial_device=""

cleanup()
{
	if [[ -n "$capture_pid" ]]; then
		kill "$capture_pid" 2>/dev/null || true
		wait "$capture_pid" 2>/dev/null || true
	fi

	rm -f "$capture_file"
}

trap cleanup EXIT

serial_by_id()
{
	local device="$1"
	local candidate

	for candidate in /dev/serial/by-id/*; do
		if [[ -e "$candidate" && "$(readlink -f "$candidate")" == "$(readlink -f "$device")" ]]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done

	printf '%s\n' "$device"
}

discover_app_serial()
{
	local device
	local properties
	local usb_id

	if [[ -n "${KFSW_SERIAL:-}" && -e "$KFSW_SERIAL" ]]; then
		printf '%s\n' "$KFSW_SERIAL"
		return 0
	fi

	if [[ -z "${KFSW_USB_APP_ID:-}" ]]; then
		return 1
	fi

	for device in /dev/ttyACM* /dev/ttyUSB*; do
		if [[ ! -e "$device" ]]; then
			continue
		fi

		properties="$(udevadm info --query=property --name="$device")"
		usb_id="$(sed -n 's/^ID_VENDOR_ID=//p' <<<"$properties"):$(sed -n 's/^ID_MODEL_ID=//p' <<<"$properties")"
		if [[ "$usb_id" == "$KFSW_USB_APP_ID" ]]; then
			serial_by_id "$device"
			return 0
		fi
	done

	return 1
}

wait_for_app_serial()
{
	local attempts=100

	while (( attempts > 0 )); do
		if serial_device="$(discover_app_serial)"; then
			return 0
		fi
		sleep 0.1
		((attempts--))
	done

	return 1
}

start_capture()
{
	stty -F "$serial_device" \
		"$KFSW_SERIAL_BAUD" \
		cs8 \
		-cstopb \
		-parenb \
		raw \
		-echo

	timeout 45s cat "$serial_device" >"$capture_file" &
	capture_pid=$!
}

wait_for_output()
{
	local offset="$1"
	local expected="$2"
	local attempts=150
	local output

	while (( attempts > 0 )); do
		output="$(tail -c "+$((offset + 1))" "$capture_file")"
		if [[ "$output" == *"$expected"* ]]; then
			return 0
		fi
		sleep 0.1
		((attempts--))
	done

	echo "FAIL: timed out waiting for: $expected"
	return 1
}

run_shell_command()
{
	local command="$1"
	shift
	local offset
	local expected

	offset="$(wc -c <"$capture_file")"
	printf '%s\r' "$command" >"$serial_device"

	for expected in "$@"; do
		wait_for_output "$offset" "$expected"
	done

	wait_for_output "$offset" "$KFSW_EXPECTED_PROMPT"
	echo "PASS: $command"
}

echo "K-FSW physical shell smoke"
echo "Target: $KFSW_TARGET"
echo "Board: $ZEPHYR_BOARD"
echo "Flash USB: $KFSW_USB_FLASH_ID"
echo "Flash runner: $KFSW_FLASH_RUNNER"
echo "Flash enabled: $KFSW_FLASH"

if [[ "$KFSW_FLASH" == 1 ]]; then
	usb_devices="$(lsusb)"
	if [[ "$usb_devices" != *"$KFSW_USB_FLASH_ID"* ]]; then
		echo "FAIL: flash USB device $KFSW_USB_FLASH_ID is not visible"
		exit 1
	fi
fi

"$KFSW_ROOT/k-fsw/tools/build.sh" "$KFSW_TARGET"

if serial_device="$(discover_app_serial)"; then
	echo "Console: $serial_device"
	start_capture
fi

if [[ "$KFSW_FLASH" == 1 ]]; then
	"$KFSW_ROOT/k-fsw/tools/flash.sh" "$KFSW_TARGET"
fi

if [[ -z "$serial_device" ]]; then
	if ! wait_for_app_serial; then
		echo "FAIL: application serial device ${KFSW_USB_APP_ID:-unknown} is not visible"
		echo "Current USB devices:"
		lsusb
		exit 1
	fi

	echo "Console: $serial_device"
	start_capture
fi

printf '\r' >"$serial_device"
wait_for_output 0 "$KFSW_EXPECTED_PROMPT"
echo "PASS: prompt"

run_shell_command status \
	"K-FSW status" \
	"board: $ZEPHYR_BOARD"
# "K-FSW:" without its value: the version comes from the build, so a literal
# would pin one commit.
run_shell_command version \
	"K-FSW:" \
	"Zephyr: 4.4.0" \
	"Board: $ZEPHYR_BOARD"
run_shell_command help \
	"Available commands:" \
	"Show basic K-FSW runtime status." \
	"Show K-FSW build information."

cat "$capture_file"
echo "PHYSICAL SHELL RESULT: PASS"
