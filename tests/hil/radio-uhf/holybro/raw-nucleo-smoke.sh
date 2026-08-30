#!/usr/bin/env bash
set -Eeuo pipefail

HOLYBRO_RAW_NUCLEO_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
HOLYBRO_DIR="$(dirname "$HOLYBRO_RAW_NUCLEO_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$(dirname "$(dirname "$HOLYBRO_DIR")")")")"

source "$KFSW_REPO_DIR/tools/_common.sh" nucleo_l496zg

radio_device="${KGROUND_HOLYBRO_DEVICE:-}"
debug_serial="${KFSW_DEBUG_SERIAL:-$KFSW_SERIAL}"
build_dir="$KFSW_ROOT/build/hil/holybro/raw-peer"
work_dir="$(mktemp -d /tmp/k-ground-holybro-raw.XXXXXX)"
debug_capture_pid=""
debug_stty=""

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
		echo "Usage: raw-nucleo-smoke.sh [--radio PATH] [--serial STLINK_PATH]"
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
	[[ -n "$debug_capture_pid" ]] && kill "$debug_capture_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	rm -rf -- "$work_dir"
}

fail()
{
	echo "HOLYBRO RAW/NUCLEO RESULT: FAIL"
	echo "  $1"
	[[ -s "$work_dir/raw.log" ]] && sed -n '1,100p' "$work_dir/raw.log"
	[[ -s "$work_dir/nucleo.log" ]] && sed -n '1,200p' "$work_dir/nucleo.log"
	exit 1
}

wait_for_output()
{
	local expected="$1"

	for _ in {1..600}; do
		grep -Fq "$expected" "$work_dir/nucleo.log" 2>/dev/null && return 0
		kill -0 "$debug_capture_pid" 2>/dev/null || return 1
		sleep 0.05
	done
	return 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ -z "$radio_device" ]]; then
	echo "HOLYBRO RAW/NUCLEO RESULT: PENDING"
	echo "  Set KGROUND_HOLYBRO_DEVICE after both radio ends are connected."
	exit 77
fi
if [[ ! -e "$radio_device" || ! -e "$debug_serial" ]]; then
	echo "HOLYBRO RAW/NUCLEO RESULT: PENDING"
	echo "  Required radio or NUCLEO debug serial device is not present."
	exit 77
fi
if [[ "$(readlink -f "$radio_device")" == "$(readlink -f "$debug_serial")" ]]; then
	fail "radio and debug serial paths resolve to the same device"
fi

echo "HOLYBRO RAW/NUCLEO: building the temporary USART3 raw peer"
west build -p always -b "$ZEPHYR_BOARD" -d "$build_dir" \
	"$HOLYBRO_DIR/raw-peer"

debug_stty="$(stty -F "$debug_serial" -g)" || \
	fail "cannot read NUCLEO debug serial settings"
stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo

timeout 120s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!
west flash -d "$build_dir" --runner openocd || fail "NUCLEO raw-peer flash failed"
wait_for_output "HOLYBRO RAW PEER: READY" || \
	fail "the NUCLEO raw peer did not report readiness"

if ! "$HOLYBRO_DIR/raw-smoke.sh" --device "$radio_device" \
	--sequence 0001 --timeout 15 >"$work_dir/raw.log" 2>&1; then
	fail "the USB radio did not receive the expected raw response"
fi
wait_for_output "HOLYBRO RAW PEER: EXCHANGE PASS" || \
	fail "the NUCLEO did not confirm the raw exchange"

cat "$work_dir/raw.log"
cat "$work_dir/nucleo.log"
echo "HOLYBRO RAW/NUCLEO RESULT: PASS"
