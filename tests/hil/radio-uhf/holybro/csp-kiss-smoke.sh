#!/usr/bin/env bash
set -Eeuo pipefail

HOLYBRO_CSP_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
HOLYBRO_DIR="$(dirname "$HOLYBRO_CSP_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$HOLYBRO_DIR")")")")"

source "$KFSW_REPO_DIR/tools/_common.sh" nucleo_l496zg

radio_device="${KGROUND_HOLYBRO_DEVICE:-}"
debug_serial="${KFSW_DEBUG_SERIAL:-$KFSW_SERIAL}"
radio_baud="${KGROUND_HOLYBRO_BAUD:-57600}"
ground_build_root="$KFSW_ROOT/build/hil/holybro/k-ground"
nucleo_build_dir="$KFSW_ROOT/build/hil/holybro/nucleo_l496zg"
ground_executable="$ground_build_root/node-16/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/k-ground-holybro-csp.XXXXXX)"
ground_pid=""
debug_capture_pid=""
bridge_pid=""
debug_stty=""
radio_stty=""

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
	-h|--help)
		echo "Usage: csp-kiss-smoke.sh [--radio PATH] [--serial STLINK_PATH]"
		exit 0
		;;
	*)
		echo "ERROR: unknown argument: $1"
		exit 2
		;;
	esac
done

cleanup()
{
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$ground_pid" ]] && kill "$ground_pid" 2>/dev/null || true
	[[ -n "$debug_capture_pid" ]] && kill "$debug_capture_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 2>/dev/null || true

	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	[[ -n "$radio_stty" && -e "$radio_device" ]] && \
		stty -F "$radio_device" "$radio_stty" 2>/dev/null || true
	rm -rf -- "$work_dir"
}

fail()
{
	echo "HOLYBRO CSP/KISS RESULT: FAIL"
	echo "  $1"

	for log_file in ground.log nucleo.log socat.log; do
		if [[ -s "$work_dir/$log_file" ]]; then
			echo "--- $log_file ---"
			sed -n '1,260p' "$work_dir/$log_file"
		fi
	done
	exit 1
}

wait_for_output()
{
	local file="$1"
	local expected="$2"
	local process_pid="$3"

	for _ in {1..1200}; do
		grep -Fq "$expected" "$file" 2>/dev/null && return 0
		kill -0 "$process_pid" 2>/dev/null || return 1
		sleep 0.05
	done
	return 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ -z "$radio_device" ]]; then
	echo "HOLYBRO CSP/KISS RESULT: PENDING"
	echo "  Set KGROUND_HOLYBRO_DEVICE after both radio ends are connected."
	exit 77
fi
if [[ ! -e "$radio_device" || ! -e "$debug_serial" ]]; then
	echo "HOLYBRO CSP/KISS RESULT: PENDING"
	echo "  Required radio or NUCLEO debug serial device is not present."
	exit 77
fi
if [[ "$(readlink -f "$radio_device")" == "$(readlink -f "$debug_serial")" ]]; then
	fail "radio and debug serial paths resolve to the same device"
fi
[[ "$radio_baud" == 57600 ]] || \
	fail "the current Holybro CSP overlays require 57600 baud"
command -v socat >/dev/null 2>&1 || fail "socat is required"

echo "HOLYBRO CSP/KISS: building ground node 16 at ${radio_baud} baud"
KGROUND_BUILD_ROOT="$ground_build_root" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/k-ground.overlay" \
	"$KFSW_REPO_DIR/tools/k-ground" build --node 16 --name main --peer 2

echo "HOLYBRO CSP/KISS: building NUCLEO node 2 with peer 16"
KFSW_BUILD_DIR="$nucleo_build_dir" \
	KFSW_EXTRA_CONF_FILE="$HOLYBRO_DIR/nucleo_l496zg.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/nucleo_l496zg.overlay" \
	"$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg

[[ -x "$ground_executable" ]] || fail "ground executable was not produced"

debug_stty="$(stty -F "$debug_serial" -g)" || \
	fail "cannot read NUCLEO debug serial settings"
radio_stty="$(stty -F "$radio_device" -g)" || \
	fail "cannot read Holybro serial settings"

stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
stty -F "$radio_device" "$radio_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 180s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!

west flash -d "$nucleo_build_dir" --runner openocd || \
	fail "NUCLEO flash failed"
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" || \
	fail "NUCLEO did not report readiness"

printf '%s\r\n' 'status' 'uart info' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "CSP node: 2" "$debug_capture_pid" || \
	fail "NUCLEO did not report CSP node 2"
wait_for_output "$work_dir/nucleo.log" "CSP peer: 16" "$debug_capture_pid" || \
	fail "NUCLEO did not report ground peer 16"
wait_for_output "$work_dir/nucleo.log" "baudrate: 57600" "$debug_capture_pid" || \
	fail "NUCLEO did not report the Holybro serial rate"

mkfifo "$work_dir/ground.in"
exec 3<>"$work_dir/ground.in"
"$ground_executable" --uart_stdinout --device_id=16 --no-color \
	-flash="$work_dir/ground-flash.bin" \
	<&3 >"$work_dir/ground.log" 2>&1 &
ground_pid=$!

wait_for_output "$work_dir/ground.log" "@READY " "$ground_pid" || \
	fail "k-ground did not report readiness"
wait_for_output "$work_dir/ground.log" \
	"uart_1 connected to pseudotty: " "$ground_pid" || \
	fail "k-ground did not expose its KISS PTY"
ground_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/ground.log" | head -1)"

socat -d -d \
	"$ground_pty,raw,echo=0,b${radio_baud}" \
	"$radio_device,raw,echo=0,b${radio_baud}" \
	>"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
	"$bridge_pid" || fail "the PTY-to-Holybro bridge did not become ready"

printf '%s\n' 'status' 'uart info' 'csp ping 2' >&3
wait_for_output "$work_dir/ground.log" "Role: ground" "$ground_pid" || \
	fail "k-ground did not report its role"
wait_for_output "$work_dir/ground.log" "baudrate: 57600" "$ground_pid" || \
	fail "k-ground did not report the Holybro serial rate"
wait_for_output "$work_dir/ground.log" "CSP ping 2: success" "$ground_pid" || \
	fail "k-ground node 16 could not ping NUCLEO node 2 over Holybro"

cat "$work_dir/ground.log"
cat "$work_dir/nucleo.log"
echo "HOLYBRO CSP/KISS RESULT: PASS"
