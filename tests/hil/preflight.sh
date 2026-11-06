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
STLINK_USB_ID="${KFSW_STLINK_USB_ID:-$(sed -n 's/^KFSW_STLINK_USB_ID=//p' \
	"$KFSW_REPO_DIR/config/targets/nucleo_l496zg.env" 2>/dev/null || true)}"
# The reference board's own ST-Link serial. More than one board can answer on
# this bench, and they are not the same composition, so the board the suite is
# written against is named rather than taken from enumeration order.
STLINK_SERIAL="${KFSW_L496_STLINK:-${KFSW_STLINK_SERIAL:-$(sed -n 's/^KFSW_STLINK_SERIAL=//p' \
	"$KFSW_REPO_DIR/config/targets/nucleo_l496zg.env" 2>/dev/null || true)}}"

F746_STLINK_SERIAL="$(sed -n 's/^KFSW_STLINK_SERIAL=//p' \
	"$KFSW_REPO_DIR/config/targets/nucleo_f746zg.env")"
found_f746_debug=""
found_ftdi=""
found_debug=""
found_other_debug=""
debug_is_reference=""
found_second_serial=""
found_radio=""
found_can=""
can_down=""
serial_without_driver=""
have_robot=""
have_terminal=""
have_probe=""

# USB identities that carry a serial bridge for the CSP UART or a radio. A board
# running K-FSW also appears as a serial device, and taking one for the other
# sends a fixture at the wrong end of the bench.
SERIAL_BRIDGE_IDS=("0403" "10c4" "1a86" "067b")

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

validate_override() {
	local name="$1" device="${!1}"
	if [[ ! -c "$device" ]]; then
		echo "ERROR: $name=$device must resolve to a live character device; check the USB connection and device path." >&2
		return 1
	fi
}

discover_probe() {
	local properties
	properties="$(lsusb -v -d "$STLINK_USB_ID" 2>/dev/null || true)"
	if [[ -n "$STLINK_SERIAL" ]] &&
		awk -v serial="$STLINK_SERIAL" '$1 == "iSerial" && $3 == serial { found = 1 } END { exit !found }' <<<"$properties"; then
		have_probe="yes"
	fi
}

discover_serial() {
	local device stable id
	local stlinks=()
	for device in /dev/ttyACM* /dev/ttyUSB*; do
		[[ -e "$device" ]] || continue
		stable="$(stable_path_of "$device" || true)"
		id="$(usb_id_of "$device")"
		if [[ "$id" == 0403:6001 || "$id" == 0403:6015 || "$id" == 0403:6014 ]]; then
			[[ -n "$stable" ]] && found_ftdi="$stable"
		fi

		if [[ -n "$STLINK_USB_ID" && "$id" == "$STLINK_USB_ID" ]]; then
			stlinks+=("${stable:-$device}")
			continue
		fi
		# Only a bridge can carry the CSP UART or a radio.
		for vendor in "${SERIAL_BRIDGE_IDS[@]}"; do
			if [[ "$id" == "$vendor:"* && -z "$found_second_serial" ]]; then
				found_second_serial="${stable:-$device}"
			fi
		done
	done

	# A bridge with no driver has no port, so the loop above cannot see it.
	local usb_device vendor_file id
	for usb_device in /sys/bus/usb/devices/[0-9]*-[0-9]*; do
		vendor_file="$usb_device/idVendor"
		[[ -r "$vendor_file" ]] || continue
		id="$(cat "$vendor_file" 2>/dev/null):$(cat "$usb_device/idProduct" 2>/dev/null)"
		for vendor in "${SERIAL_BRIDGE_IDS[@]}"; do
			if [[ "$id" == "$vendor:"* ]]; then
				local interface bound=""
				for interface in "$usb_device":*; do
					[[ -e "$interface/driver" ]] && bound="yes"
				done
				if [[ -z "$bound" ]]; then
					serial_without_driver="${serial_without_driver}${serial_without_driver:+, }$id"
				fi
			fi
		done
	done

	# Name the board by its serial and list the rest, so an operator sees what
	# else is attached.
	local link
	for link in "${stlinks[@]}"; do
		if [[ -n "$F746_STLINK_SERIAL" && "$link" == /dev/serial/by-id/*"$F746_STLINK_SERIAL"* ]]; then
			found_f746_debug="$link"
		fi
		if [[ -n "$STLINK_SERIAL" && "$link" == *"$STLINK_SERIAL"* ]]; then
			found_debug="$link"
			debug_is_reference="yes"
		else
			found_other_debug="${found_other_debug}${found_other_debug:+, }$link"
		fi
	done
	if [[ -z "$found_debug" && ${#stlinks[@]} -gt 0 ]]; then
		found_debug="${stlinks[0]}"
		# A single probe stands for the board only when this target pins no
		# serial, which is how another bench runs these cases.
		[[ -z "$STLINK_SERIAL" ]] && debug_is_reference="yes"
	fi

	# An explicit setting always wins over discovery.
	if [[ -n "${KFSW_DEBUG_SERIAL:-}" ]]; then
		found_debug=""
		debug_is_reference=""
		if validate_override KFSW_DEBUG_SERIAL; then
			found_debug="$KFSW_DEBUG_SERIAL"
			debug_is_reference="yes"
		fi
	fi
	if [[ -n "${KFSW_FTDI_DEVICE:-}" ]]; then
		found_second_serial=""
		found_ftdi=""
		if validate_override KFSW_FTDI_DEVICE; then
			found_second_serial="$KFSW_FTDI_DEVICE"
			found_ftdi="$KFSW_FTDI_DEVICE"
		fi
	fi
	if [[ -n "${KFSW_F746_DEBUG_SERIAL:-}" ]]; then
		found_f746_debug=""
		if validate_override KFSW_F746_DEBUG_SERIAL; then
			found_f746_debug="$KFSW_F746_DEBUG_SERIAL"
		fi
	fi
	if [[ -n "${KFSW_RADIO_SERIAL:-}" ]]; then
		found_radio=""
		if validate_override KFSW_RADIO_SERIAL; then
			found_radio="$KFSW_RADIO_SERIAL"
		fi
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
	# A real bus first, and one that is down is worth naming as down.
	while read -r name; do
		[[ -z "$name" ]] && continue
		if ip link show "$name" 2>/dev/null | grep -q 'state UP'; then
			found_can="$name"
			return 0
		fi
		can_down="${can_down}${can_down:+, }$name"
	done < <(ip -brief link show type can 2>/dev/null | awk '{print $1}')
	if [[ -n "$can_down" ]]; then
		return 0
	fi
	while read -r name; do
		[[ -z "$name" ]] && continue
		if ip link show "$name" 2>/dev/null | grep -q 'state UP\|state UNKNOWN'; then
			found_can="$name (virtual; no board on it)"
			return 0
		fi
	done < <(ip -brief link show type vcan 2>/dev/null | awk '{print $1}')
	return 0
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
	board | board-uart | board-can | radio) [[ -n "$have_probe" ]] || return 1 ;;
	esac
	case "$1" in
	software) [[ -n "$have_robot" ]] ;;
	terminal) [[ -n "$have_robot" && -n "$have_terminal" ]] ;;
	board) [[ -n "$have_robot" && -n "$found_debug" && -n "$debug_is_reference" ]] ;;
	# The F746 scenario skips itself when that board is absent, so the shape
	# only needs the adapter the link runs on.
	board-uart) [[ -n "$have_robot" && -n "$found_debug" && -n "$debug_is_reference" && -n "$found_ftdi" && ( -z "${KFSW_F746_DEBUG_SERIAL:-}" || -n "$found_f746_debug" ) ]] ;;
	board-can) [[ -n "$have_robot" && -n "$found_debug" && -n "$debug_is_reference" && -n "$found_can" && "$found_can" != *virtual* ]] ;;
	radio) [[ -n "$have_robot" && -n "$found_debug" && -n "$debug_is_reference" && -n "$found_radio" ]] ;;
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
		[[ -z "$have_probe" ]] && missing+=("ST-LINK USB $STLINK_USB_ID serial $STLINK_SERIAL (check USB or KFSW_STLINK_SERIAL)")
		[[ -z "$found_debug" ]] && missing+=("the board's ST-LINK debug UART")
		if [[ -n "$found_debug" && -z "$debug_is_reference" ]]; then
			missing+=("KFSW_DEBUG_SERIAL, naming which of the attached boards to use")
		fi
		;;
	esac
	case "$shape" in
	board-uart)
		[[ -z "$found_ftdi" ]] && missing+=("an FTDI UART adapter")
		if [[ -n "${KFSW_F746_DEBUG_SERIAL:-}" && -z "$found_f746_debug" ]]; then
			missing+=("a live KFSW_F746_DEBUG_SERIAL character device")
		fi
		;;
	board-can)
		if [[ -n "$can_down" ]]; then
			missing+=("$can_down brought up: sudo ip link set $can_down up type can bitrate 500000")
		elif [[ -z "$found_can" || "$found_can" == *virtual* ]]; then
			missing+=("a CAN interface with a board on it")
		fi
		;;
	radio) [[ -z "$found_radio" ]] && missing+=("a live KFSW_RADIO_SERIAL character device assigned to a confirmed radio") ;;
	esac

	for item in ${missing[@]+"${missing[@]}"}; do
		joined="${joined:+$joined, }$item"
	done
	printf '%s' "$joined"
}

report() {
	echo "PREFLIGHT: what this bench has"
	echo "Device inventory only. Confirm wiring and firmware separately; no CSP link has been tested."
	local host_tools="${CARGO_TARGET_DIR:-${KFSW_CSP_TOOLS_DIR:-$KFSW_WORKSPACE_ROOT/tools/kfsw-csp-tools}/target}/release"
	for tool in csp-kiss csp-iperf; do
		if [[ -x "$host_tools/$tool" ]]; then
			echo "  $tool: built"
		else
			echo "  $tool: missing; run tools/host-tools.sh (requires Rust)"
		fi
	done
	if ip link show vcan0 >/dev/null 2>&1; then
		echo "  vcan0: available"
	else
		echo "  vcan0: unavailable; run sudo k-fsw/tests/vcan-up.sh"
	fi
	printf '  %-22s %s\n' "debug UART" "${found_debug:-not found}"
	if [[ -n "$found_other_debug" ]]; then
		printf '  %-22s %s\n' "other ST-Links" "$found_other_debug"
	fi
	printf '  %-22s %s\n' "F746 debug UART" "${found_f746_debug:-not found}"
	printf '  %-22s %s\n' "FTDI UART" "${found_ftdi:-not found}"
	# One cable, one board: the UART scenario for any other board says so and
	# does not run.
	printf '  %-22s %s\n' "FTDI board setting" "${KFSW_FTDI_BOARD:-nucleo_l496zg (set KFSW_FTDI_BOARD)}"
	printf '  %-22s %s\n' "second serial" "${found_second_serial:-not found}"
	printf '  %-22s %s\n' "radio serial" "${found_radio:-not set; say which serial port it is}"
	printf '  %-22s %s\n' "CAN interface" "${found_can:-${can_down:+$can_down (down)}}"
	printf '  %-22s %s\n' "" "${found_can:-${can_down:-none at all}}" >/dev/null
	printf '  %-22s %s\n' "signing key" \
		"$([[ -r "$signing_key" ]] && echo "$signing_key" || echo "not found, so no signed image")"
	printf '  %-22s %s\n' "Robot Framework" "${have_robot:-missing}"
	printf '  %-22s %s\n' "terminal runner" "${have_terminal:-missing}"
	echo

	if [[ -n "$serial_without_driver" ]]; then
		echo "  A serial bridge is attached but the kernel has not bound it:"
		echo "  $serial_without_driver. Without a driver it has no port."
		echo "  This kernel builds ftdi_sio as a module: sudo modprobe ftdi_sio"
		echo
	fi

	if [[ -n "$found_debug" && -z "$debug_is_reference" ]]; then
		echo "  The board these cases are written for ($STLINK_SERIAL) is not"
		echo "  attached. What is attached is not the same composition, so a"
		echo "  number read from it would be reported as this board's. Set"
		echo "  KFSW_DEBUG_SERIAL to use one of them anyway."
		echo
	fi

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

signing_key="${KFSW_MCUBOOT_KEY:-$HOME/.config/kfsw/mcuboot-signing-key.pem}"

discover_serial
discover_probe
discover_can
discover_host

# What a caller should export, so a fixture reaches the device that was found.
if [[ "${1:-}" == "--export" ]]; then
	[[ -n "$found_debug" ]] && printf 'KFSW_DEBUG_SERIAL=%s\n' "$found_debug"
	# The probe to flash through. A case that flashes without naming it gets
	# whichever of the attached boards openocd happens to open first.
	[[ -n "$STLINK_SERIAL" && -n "$debug_is_reference" ]] &&
		printf 'KFSW_STLINK_SERIAL=%s\n' "$STLINK_SERIAL"
	[[ -n "$found_f746_debug" ]] && printf 'KFSW_F746_DEBUG_SERIAL=%s\n' "$found_f746_debug"
	[[ -n "$found_ftdi" ]] && found_second_serial="$found_ftdi"
	[[ -n "$found_second_serial" ]] && printf 'KFSW_FTDI_DEVICE=%s\n' "$found_second_serial"
	[[ -n "$found_radio" ]] && printf 'KFSW_RADIO_SERIAL=%s\n' "$found_radio"
	[[ -n "$found_can" && "$found_can" != *virtual* ]] &&
		printf 'KFSW_CAN_INTERFACE=%s\n' "$found_can"
	# The signing key is a bench input like a port is. The scenarios that build
	# a signed image default to this path, and the boot smoke refuses to guess
	# which key to sign with, so the bench declares it here once. A board
	# carrying MCUboot otherwise fails the first case of the board shape.
	[[ -r "$signing_key" ]] && printf 'KFSW_MCUBOOT_KEY=%s\n' "$signing_key"
	exit 0
fi

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
