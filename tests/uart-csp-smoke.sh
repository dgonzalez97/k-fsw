#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" nucleo_l496zg

ftdi_device=""
debug_serial="$KFSW_SERIAL"
debug_baud="$KFSW_SERIAL_BAUD"
nucleo_build_dir="$KFSW_BUILD_DIR"
linux_build_dir="$KFSW_ROOT/build/linux"
linux_executable="$linux_build_dir/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-uart-csp.XXXXXX)"
linux_pid=""
debug_capture_pid=""
bridge_pid=""
debug_stty=""
ftdi_stty=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--ftdi)
			ftdi_device="${2:?--ftdi requires a device path}"
			shift 2
			;;
		--serial)
			debug_serial="${2:?--serial requires a device path}"
			shift 2
			;;
		*)
			echo "ERROR: unknown argument: $1"
			exit 1
			;;
	esac
done

cleanup()
{
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$linux_pid" ]] && kill "$linux_pid" 2>/dev/null || true
	[[ -n "$debug_capture_pid" ]] && \
		kill "$debug_capture_pid" 2>/dev/null || true

	[[ -n "$bridge_pid" ]] && wait "$bridge_pid" 2>/dev/null || true
	[[ -n "$linux_pid" ]] && wait "$linux_pid" 2>/dev/null || true
	[[ -n "$debug_capture_pid" ]] && \
		wait "$debug_capture_pid" 2>/dev/null || true

	exec 3>&- 2>/dev/null || true

	[[ -n "$debug_stty" && -e "$debug_serial" ]] && \
		stty -F "$debug_serial" "$debug_stty" 2>/dev/null || true
	[[ -n "$ftdi_stty" && -e "$ftdi_device" ]] && \
		stty -F "$ftdi_device" "$ftdi_stty" 2>/dev/null || true

	rm -rf -- "$work_dir"
}

fail()
{
	echo "UART CSP HIL RESULT: FAIL"
	echo "  $1"

	for log_file in linux.log nucleo.log socat.log; do
		if [[ -s "$work_dir/$log_file" ]]; then
			echo "--- $log_file ---"
			sed -n '1,320p' "$work_dir/$log_file"
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

is_ft232rl()
{
	local device="$1"
	local properties

	command -v udevadm >/dev/null 2>&1 || return 1
	properties="$(udevadm info --query=property --name "$device" 2>/dev/null)" || \
		return 1

	grep -Fqx 'ID_VENDOR_ID=0403' <<<"$properties" &&
		grep -Fqx 'ID_MODEL_ID=6001' <<<"$properties"
}

is_stlink_vcp()
{
	local device="$1"
	local properties

	command -v udevadm >/dev/null 2>&1 || return 1
	properties="$(udevadm info --query=property --name "$device" 2>/dev/null)" || \
		return 1

	grep -Fqx 'ID_VENDOR_ID=0483' <<<"$properties" &&
		grep -Eq '^ID_MODEL=.*STLink' <<<"$properties"
}

find_stable_ftdi()
{
	local requested="$1"
	local requested_real
	local candidate

	requested_real="$(readlink -f "$requested")"
	for candidate in /dev/serial/by-id/*; do
		[[ -e "$candidate" ]] || continue
		if [[ "$(readlink -f "$candidate")" == "$requested_real" ]]; then
			echo "$candidate"
			return 0
		fi
	done

	return 1
}

discover_ftdi()
{
	local candidate
	local -a matches=()

	for candidate in /dev/serial/by-id/* /dev/ttyUSB*; do
		[[ -e "$candidate" ]] || continue
		is_ft232rl "$candidate" && matches+=("$candidate")
	done

	if [[ ${#matches[@]} -eq 0 ]]; then
		return 1
	fi

	if [[ ${#matches[@]} -gt 1 ]]; then
		local first_real
		local all_same=true

		first_real="$(readlink -f "${matches[0]}")"
		for candidate in "${matches[@]:1}"; do
			[[ "$(readlink -f "$candidate")" == "$first_real" ]] || \
				all_same=false
		done

		$all_same || return 2
	fi

	find_stable_ftdi "${matches[0]}" || echo "${matches[0]}"
}

trap cleanup EXIT

command -v socat >/dev/null 2>&1 || fail "socat is required"
command -v lsusb >/dev/null 2>&1 || fail "lsusb is required"

if [[ -z "$ftdi_device" ]]; then
	if ! ftdi_device="$(discover_ftdi)"; then
		fail "no unique FT232RL device found; pass --ftdi /dev/serial/by-id/..."
	fi
fi

[[ -e "$ftdi_device" ]] || fail "FTDI device is missing: $ftdi_device"
[[ -e "$debug_serial" ]] || fail "ST-LINK debug UART is missing: $debug_serial"

if [[ "$(readlink -f "$ftdi_device")" == "$(readlink -f "$debug_serial")" ]]; then
	fail "CSP UART and debug UART resolve to the same device"
fi

[[ "$debug_serial" == /dev/ttyACM* ]] || \
	fail "the debug shell must use an ST-LINK /dev/ttyACM* device"
[[ "$(readlink -f "$ftdi_device")" == /dev/ttyUSB* ]] || \
	fail "the CSP UART must resolve to an FTDI /dev/ttyUSB* device"
is_ft232rl "$ftdi_device" || \
	fail "$ftdi_device is not identified by udev as an FT232R device"
is_stlink_vcp "$debug_serial" || \
	fail "$debug_serial is not identified by udev as an ST-LINK VCP"

stable_ftdi="$(find_stable_ftdi "$ftdi_device" || true)"
if [[ -n "$stable_ftdi" ]]; then
	ftdi_device="$stable_ftdi"
fi

echo "FTDI device: $ftdi_device"
udevadm info --query=property --name "$ftdi_device" |
	grep -E '^(ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL|ID_PATH)=' || true
lsusb | grep -Ei '0403:6001|FT232|FTDI' || true
echo "Debug shell: $debug_serial"
echo "YP-05 logic selection: 3.3 V required"

"$KFSW_ROOT/k-fsw/tools/build.sh" linux
"$KFSW_ROOT/k-fsw/tools/build.sh" nucleo_l496zg

[[ -x "$linux_executable" ]] || \
	fail "the supported Linux target executable was not produced"

debug_stty="$(stty -F "$debug_serial" -g)" || \
	fail "cannot read ST-LINK UART settings: $debug_serial"
ftdi_stty="$(stty -F "$ftdi_device" -g)" || \
	fail "cannot read FTDI UART settings: $ftdi_device"

stty -F "$debug_serial" "$debug_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 180s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!

west flash -d "$nucleo_build_dir" --runner openocd || \
	fail "the supported NUCLEO target flash failed"

wait_for_output "$work_dir/nucleo.log" "@BOOT " "$debug_capture_pid" || \
	fail "NUCLEO did not report @BOOT on the ST-LINK UART"
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" || \
	fail "NUCLEO did not report @READY on the ST-LINK UART"

printf '%s\r\n' \
	'status' \
	'storage info' \
	'storage test' \
	'uart info' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "K-FSW status" \
	"$debug_capture_pid" || fail "NUCLEO debug shell did not answer status"
wait_for_output "$work_dir/nucleo.log" "board: nucleo_l496zg/stm32l496xx" \
	"$debug_capture_pid" || fail "NUCLEO status reported an unexpected board"
wait_for_output "$work_dir/nucleo.log" "mount_point: /kfsw" \
	"$debug_capture_pid" || fail "NUCLEO storage mount point was not reported"
wait_for_output "$work_dir/nucleo.log" "Storage test: PASS" \
	"$debug_capture_pid" || fail "NUCLEO storage test did not pass"
wait_for_output "$work_dir/nucleo.log" "device: usart3" \
	"$debug_capture_pid" || fail "NUCLEO did not report USART3 UART status"
wait_for_output "$work_dir/nucleo.log" "ready: yes" \
	"$debug_capture_pid" || fail "NUCLEO did not report a ready CSP UART"
wait_for_output "$work_dir/nucleo.log" "CSP interface: KISS" \
	"$debug_capture_pid" || fail "NUCLEO did not report its KISS interface"
wait_for_output "$work_dir/nucleo.log" "CSP node: 2" \
	"$debug_capture_pid" || fail "NUCLEO did not report CSP node 2"
wait_for_output "$work_dir/nucleo.log" "CSP peer: 1" \
	"$debug_capture_pid" || fail "NUCLEO did not report CSP peer 1"

mkfifo "$work_dir/linux.in"
exec 3<>"$work_dir/linux.in"

"$linux_executable" --uart_stdinout --device_id=1 --no-color \
	-flash="$work_dir/linux-flash.bin" \
	<&3 >"$work_dir/linux.log" 2>&1 &
linux_pid=$!

wait_for_output "$work_dir/linux.log" "@READY " "$linux_pid" || \
	fail "KFSW-Linux did not report readiness"
wait_for_output "$work_dir/linux.log" "uart_1 connected to pseudotty: " \
	"$linux_pid" || fail "KFSW-Linux did not expose its KISS PTY"

linux_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/linux.log" | head -1)"
[[ -n "$linux_pty" && -e "$linux_pty" ]] || \
	fail "the discovered KFSW-Linux PTY is invalid: $linux_pty"

bridge_command=(
	socat -d -d
	"$linux_pty,raw,echo=0,b115200"
	"$ftdi_device,raw,echo=0,b115200"
)

printf 'PTY -> FTDI bridge:'
printf ' %q' "${bridge_command[@]}"
printf '\n'

"${bridge_command[@]}" >"$work_dir/socat.log" 2>&1 &
bridge_pid=$!

wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
	"$bridge_pid" || fail "PTY to FTDI bridge did not become ready"

printf '%s\n' 'csp ping 2' >&3
wait_for_output "$work_dir/linux.log" "CSP ping 2: success" "$linux_pid" || \
	fail "KFSW-Linux could not ping NUCLEO CSP node 2"

printf '%s\r\n' \
	'csp ping 1' \
	'uart test' \
	'uart info' >"$debug_serial"

wait_for_output "$work_dir/nucleo.log" "CSP ping 1: success" \
	"$debug_capture_pid" || fail "NUCLEO could not ping KFSW-Linux CSP node 1"
wait_for_output "$work_dir/nucleo.log" "UART CSP test: PASS" \
	"$debug_capture_pid" || fail "NUCLEO UART transport test did not pass"

printf '%s\n' 'uart test' 'uart info' >&3
wait_for_output "$work_dir/linux.log" "UART CSP test: PASS" "$linux_pid" || \
	fail "KFSW-Linux UART transport test did not pass"

printf '%s\n' \
	'ftp generate /build/hil-4k.bin 4096' \
	'ftp 2 mkdir /hil' \
	'ftp put 2 /build/hil-4k.bin /hil/hil-4k.bin' \
	'ftp stat 2 /hil/hil-4k.bin' \
	'ftp 2 ls /hil' \
	'ftp get 2 /hil/hil-4k.bin /build/hil-4k-returned.bin' \
	'ftp verify /build/hil-4k.bin /build/hil-4k-returned.bin' \
	'ftp generate /build/hil-16k.bin 16384' \
	'ftp put 2 /build/hil-16k.bin /hil/hil-16k.bin' \
	'ftp get 2 /hil/hil-16k.bin /build/hil-16k-returned.bin' \
	'ftp verify /build/hil-16k.bin /build/hil-16k-returned.bin' \
	'csp ping 2' \
	'param get 2 test_u32' \
	'uart info' >&3

wait_for_output "$work_dir/linux.log" \
	"FTP put node=2 source=/build/hil-4k.bin destination=/hil/hil-4k.bin: PASS bytes=4096" \
	"$linux_pid" || fail "4 KiB physical FTP upload did not pass"
wait_for_output "$work_dir/linux.log" \
	"FTP verify first=/build/hil-4k.bin second=/build/hil-4k-returned.bin: PASS" \
	"$linux_pid" || fail "4 KiB physical FTP round trip did not match"
wait_for_output "$work_dir/linux.log" \
	"FTP put node=2 source=/build/hil-16k.bin destination=/hil/hil-16k.bin: PASS bytes=16384" \
	"$linux_pid" || fail "16 KiB physical FTP upload did not pass"
wait_for_output "$work_dir/linux.log" \
	"FTP verify first=/build/hil-16k.bin second=/build/hil-16k-returned.bin: PASS" \
	"$linux_pid" || fail "16 KiB physical FTP round trip did not match"
wait_for_output "$work_dir/linux.log" "2:test_u32 = 42" "$linux_pid" || \
	fail "PARAM did not work after physical FTP transfers"

for log_file in linux.log nucleo.log; do
	last_stats="$(grep -F 'KISS tx=' "$work_dir/$log_file" | tail -1 || true)"
	grep -Eq 'KISS tx=[1-9][0-9]* rx=[1-9][0-9]*' <<<"$last_stats" || \
		fail "$log_file does not contain nonzero KISS traffic statistics"
	grep -Fq 'txerr=0 rxerr=0 drop=0 frame=0' <<<"$last_stats" || \
		fail "$log_file does not contain clean KISS statistics"
done

echo "--- linux.log ---"
sed -n '1,320p' "$work_dir/linux.log"
echo "--- nucleo.log ---"
sed -n '1,320p' "$work_dir/nucleo.log"
echo "--- socat.log ---"
sed -n '1,120p' "$work_dir/socat.log"
echo "UART CSP HIL RESULT: PASS"
