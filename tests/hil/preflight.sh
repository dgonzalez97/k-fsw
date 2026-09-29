#!/usr/bin/env bash
# What this bench can run, before anything is flashed or reset.
#
# Reports rather than fails, except when asked about one shape: then the exit
# status says whether that shape can run, so a script can gate on it.
set -Eeuo pipefail

KFSW_PREFLIGHT="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_HIL_DIR="$(dirname "$KFSW_PREFLIGHT")"
KFSW_TESTS_DIR="$(dirname "$KFSW_HIL_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

# shellcheck source=tests/hil/shapes.sh
source "$KFSW_HIL_DIR/shapes.sh"

# The ST-LINK's USB identity, from the target profile.
STLINK_USB_ID="$(sed -n 's/^KFSW_STLINK_USB_ID=//p' \
	"$KFSW_REPO_DIR/config/targets/nucleo_l496zg.env" 2>/dev/null || true)"

found_debug=""
found_second_serial=""
found_radio=""
found_can=""
have_robot=""
have_terminal=""

usb_id_of() {
	local device="$1" properties
	properties="$(udevadm info --query=property --name="$device" 2>/dev/null || true)"
	printf '%s:%s' \
		"$(sed -n 's/^ID_VENDOR_ID=//p' <<<"$properties")" \
		"$(sed -n 's/^ID_MODEL_ID=//p' <<<"$properties")"
}

# A stable path, never a /dev/ttyACM number that moves between boots.
stable_path_of() {
	local device="$1" link
	for link in /dev/serial/by-id/*; do
		[[ -e "$link" ]] || continue
		if [[ "$(readlink -f "$link")" == "$(readlink -f "$device")" ]]; then
			printf '%s' "$link"
			return 0
		fi
	done
	return 1
}

discover_serial() {
	local device stable id
	for device in /dev/ttyACM* /dev/ttyUSB*; do
		[[ -e "$device" ]] || continue
		stable="$(stable_path_of "$device" || true)"
		id="$(usb_id_of "$device")"

		if [[ -n "$STLINK_USB_ID" && "$id" == "$STLINK_USB_ID" ]]; then
			[[ -z "$found_debug" ]] && found_debug="${stable:-$device}"
			continue
		fi
		# Anything else that speaks serial can carry the CSP UART or a radio.
		# They are the same kind of device, so the shape depends on wiring and
		# a person has to say which is which.
		if [[ -z "$found_second_serial" ]]; then
			found_second_serial="${stable:-$device}"
		fi
	done

	# An explicit setting always wins over discovery.
	if [[ -n "${KFSW_DEBUG_SERIAL:-}" ]]; then
		found_debug="$KFSW_DEBUG_SERIAL"
	fi
	if [[ -n "${KFSW_FTDI_DEVICE:-}" ]]; then
		found_second_serial="$KFSW_FTDI_DEVICE"
	fi
	if [[ -n "${KFSW_RADIO_SERIAL:-}" ]]; then
		found_radio="$KFSW_RADIO_SERIAL"
	fi
	return 0
}

discover_can() {
	local iface="${KFSW_CAN_INTERFACE:-}"

	if [[ -n "$iface" ]]; then
		if ip link show "$iface" 2>/dev/null | grep -q 'state UP'; then
			found_can="$iface"
		fi
		return 0
	fi
	while read -r name; do
		[[ -z "$name" ]] && continue
		if ip link show "$name" 2>/dev/null | grep -q 'state UP'; then
			found_can="$name"
			return 0
		fi
	done < <(ip -brief link show type can 2>/dev/null | awk '{print $1}')
	while read -r name; do
		[[ -z "$name" ]] && continue
		if ip link show "$name" 2>/dev/null | grep -q 'state UP\|state UNKNOWN'; then
			found_can="$name"
			return 0
		fi
	done < <(ip -brief link show type vcan 2>/dev/null | awk '{print $1}')
}

discover_host() {
	if [[ -x "$KFSW_WORKSPACE_ROOT/.venv/bin/robot" ]] ||
		command -v robot >/dev/null 2>&1; then
		have_robot="yes"
	fi
	if command -v tmux >/dev/null 2>&1 &&
		[[ -f "$KFSW_TESTS_DIR/platform/robot-terminal-runner/src/tmux_interaction_lib.py" ]]; then
		have_terminal="yes"
	fi
	return 0
}

shape_is_possible() {
	case "$1" in
	software) [[ -n "$have_robot" ]] ;;
	terminal) [[ -n "$have_robot" && -n "$have_terminal" ]] ;;
	board) [[ -n "$have_robot" && -n "$found_debug" ]] ;;
	board-uart) [[ -n "$have_robot" && -n "$found_debug" && -n "$found_second_serial" ]] ;;
	board-can) [[ -n "$have_robot" && -n "$found_debug" && -n "$found_can" ]] ;;
	radio) [[ -n "$have_robot" && -n "$found_debug" && -n "${found_radio:-$found_second_serial}" ]] ;;
	*) return 1 ;;
	esac
}

missing_for() {
	local shape="$1" joined="" item
	local missing=()

	[[ -z "$have_robot" ]] && missing+=("Robot Framework")
	case "$shape" in
	terminal)
		[[ -z "$have_terminal" ]] && missing+=("tmux or the robot-terminal-runner submodule")
		;;
	board | board-uart | board-can | radio)
		[[ -z "$found_debug" ]] && missing+=("the board's ST-LINK debug UART")
		;;
	esac
	case "$shape" in
	board-uart) [[ -z "$found_second_serial" ]] && missing+=("a second serial adapter") ;;
	board-can) [[ -z "$found_can" ]] && missing+=("a CAN interface that is up") ;;
	radio) [[ -z "${found_radio:-$found_second_serial}" ]] && missing+=("a radio on a serial port") ;;
	esac

	for item in ${missing[@]+"${missing[@]}"}; do
		joined="${joined:+$joined, }$item"
	done
	printf '%s' "$joined"
}

report() {
	echo "PREFLIGHT: what this bench has"
	printf '  %-22s %s\n' "debug UART" "${found_debug:-not found}"
	printf '  %-22s %s\n' "second serial" "${found_second_serial:-not found}"
	printf '  %-22s %s\n' "radio serial" "${found_radio:-not set; say which serial port it is}"
	printf '  %-22s %s\n' "CAN interface" "${found_can:-none up}"
	printf '  %-22s %s\n' "Robot Framework" "${have_robot:-missing}"
	printf '  %-22s %s\n' "terminal runner" "${have_terminal:-missing}"
	echo

	if [[ -n "$found_debug" && "$found_debug" != /dev/serial/by-id/* ]]; then
		echo "  Note: $found_debug is not a /dev/serial/by-id path. Those move"
		echo "  between boots; set KFSW_DEBUG_SERIAL to the stable one."
		echo
	fi

	echo "PREFLIGHT: shapes"
	local shape
	for shape in "${KFSW_SHAPES[@]}"; do
		if shape_is_possible "$shape"; then
			printf '  %-12s can run   %s\n' "$shape" "$(kfsw_shape_covers "$shape")"
		else
			printf '  %-12s no        needs %s\n' "$shape" "$(missing_for "$shape")"
		fi
	done
	echo
	echo "Run one with: tests/hil/run.sh <shape>"
}

discover_serial
discover_can
discover_host

if [[ $# -gt 0 ]]; then
	kfsw_shape_needs "$1" >/dev/null || {
		echo "ERROR: $1 is not a shape. Known: ${KFSW_SHAPES[*]}" >&2
		exit 2
	}
	if shape_is_possible "$1"; then
		echo "PREFLIGHT: $1 can run"
		exit 0
	fi
	echo "PREFLIGHT: $1 cannot run; needs $(missing_for "$1")" >&2
	exit 1
fi

report
