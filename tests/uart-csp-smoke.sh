#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/support/csp-debug-checks.sh"

target=nucleo_l496zg
ftdi_device=""
debug_serial=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--target) target="${2:?--target requires a board}"; shift 2 ;;
		--ftdi) ftdi_device="${2:?--ftdi requires a device path}"; shift 2 ;;
		--serial) debug_serial="${2:?--serial requires a device path}"; shift 2 ;;
		*) echo "ERROR: unknown argument: $1"; exit 1 ;;
	esac
done

case "$target" in
	nucleo_l496zg) uart_device=usart3 ;;
	nucleo_f746zg) uart_device=usart6 ;;
	*) echo "ERROR: unsupported UART HIL target: $target"; exit 1 ;;
esac

source "$(dirname "$0")/../tools/_common.sh" "$target"

# One FTDI cable serves one board. Which board it is wired to is a fact about
# the bench that nothing can read from software, so the bench states it.
ftdi_board="${KFSW_FTDI_BOARD:-nucleo_l496zg}"
if [[ "$target" != "$ftdi_board" ]]; then
	echo "UART CSP HIL RESULT: NOT RUN - the FTDI cable is on $ftdi_board, not $target"
	exit 0
fi

debug_serial="${debug_serial:-$KFSW_SERIAL}"
debug_baud="$KFSW_SERIAL_BAUD"
nucleo_build_dir="$KFSW_BUILD_DIR"
linux_build_dir="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/linux"
linux_executable="$linux_build_dir/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-uart-csp.XXXXXX)"
linux_pid=""
debug_capture_pid=""
bridge_pid=""
debug_stty=""
ftdi_stty=""
original_rdp=""
original_packet_trace=""

send_debug()
{
	printf '%s\r\n' "$@" >"$debug_serial"
}

send_linux()
{
	printf '%s\n' "$@" >&3
}

cleanup()
{
	# Local shell restoration also works when the CSP link has failed.
	if [[ -n "$original_rdp" || -n "$original_packet_trace" ]]; then
		timeout 2s bash -c 'printf "%s\r\n" "$@" >"$0"' "$debug_serial" \
			${original_rdp:+"csp rdp $original_rdp"} \
			${original_packet_trace:+"param set packet_trace $original_packet_trace"} || true
	fi
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

skip()
{
	# A bench part that is not plugged in is not a defect in the software. A
	# suite that reports it as one teaches a reader to ignore red.
	echo "UART CSP HIL RESULT: NOT RUN"
	echo "  $1"
	exit 0
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

# The single-channel FTDI UART bridges this bench uses. Named rather than
# accepting any 0403 device, because a multi-channel or JTAG part on the same
# vendor ID is not a plain UART and would fail further in.
KFSW_FTDI_UART_MODELS=(6001 6015 6014)

is_ftdi_uart()
{
	local device="$1"
	local properties model

	command -v udevadm >/dev/null 2>&1 || return 1
	properties="$(udevadm info --query=property --name "$device" 2>/dev/null)" || \
		return 1

	grep -Fqx 'ID_VENDOR_ID=0403' <<<"$properties" || return 1
	for model in "${KFSW_FTDI_UART_MODELS[@]}"; do
		grep -Fqx "ID_MODEL_ID=$model" <<<"$properties" && return 0
	done
	return 1
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
		is_ftdi_uart "$candidate" && matches+=("$candidate")
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
		skip "no FT232RL on this bench; pass --ftdi /dev/serial/by-id/... to name one"
	fi
fi

[[ -e "$ftdi_device" ]] || fail "FTDI device is missing: $ftdi_device"
[[ -e "$debug_serial" ]] || skip "ST-LINK debug UART is missing for $target: $debug_serial"

if [[ "$(readlink -f "$ftdi_device")" == "$(readlink -f "$debug_serial")" ]]; then
	fail "CSP UART and debug UART resolve to the same device"
fi

# Resolved, not literal: a /dev/serial/by-id path is the one the project asks
# for, and it is a link to the ttyACM the ST-LINK presents.
[[ "$(readlink -f "$debug_serial")" == /dev/ttyACM* ]] || \
	fail "the debug shell must resolve to an ST-LINK /dev/ttyACM* device"
[[ "$(readlink -f "$ftdi_device")" == /dev/ttyUSB* ]] || \
	fail "the CSP UART must resolve to an FTDI /dev/ttyUSB* device"
is_ftdi_uart "$ftdi_device" || \
	fail "$ftdi_device is not identified by udev as an FTDI UART bridge"
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

KFSW_BUILD_DIR="$linux_build_dir" "$KFSW_ROOT/k-fsw/tools/build.sh" linux
if [[ "$target" == nucleo_f746zg ]]; then
	profiles="$KFSW_REPO_DIR/config/profiles"
	KFSW_EXTRA_CONF_FILE="$profiles/f746-uart.conf" \
		KFSW_EXTRA_DTC_OVERLAY_FILE="$profiles/f746-uart.overlay" \
		"$KFSW_REPO_DIR/tools/build.sh" "$target"
else
	"$KFSW_REPO_DIR/tools/build.sh" "$target"
fi

config="$nucleo_build_dir/zephyr/.config"
[[ -r "$config" ]] || fail "built configuration is missing: $config"
grep -qx 'CONFIG_KFSW_DEBUG_SHELL=y' "$config" || \
	skip "CSP debug shell is disabled for $target"
node="$(sed -n 's/^CONFIG_KFSW_CSP_ADDRESS=//p' "$config")"
board="$(sed -n 's/^CONFIG_BOARD_TARGET="\(.*\)"/\1/p' "$config")"
[[ "$node" =~ ^[0-9]+$ && -n "$board" ]] || fail "built node or board identity is missing"
have_storage=false
grep -qx 'CONFIG_KFSW_STORAGE=y' "$config" && have_storage=true
if ! $have_storage; then
	echo "SKIP: storage info, storage test, mount point and FTP transfers: CONFIG_KFSW_STORAGE is disabled for $target"
fi

[[ -x "$linux_executable" ]] || \
	fail "the supported Linux target executable was not produced"

debug_stty="$(stty -F "$debug_serial" -g)" || \
	fail "cannot read ST-LINK UART settings: $debug_serial"
ftdi_stty="$(stty -F "$ftdi_device" -g)" || \
	fail "cannot read FTDI UART settings: $ftdi_device"

stty -F "$debug_serial" "$debug_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 180s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!

kfsw_west_flash -d "$nucleo_build_dir" --runner openocd || \
	fail "the supported NUCLEO target flash failed"

wait_for_output "$work_dir/nucleo.log" "@BOOT " "$debug_capture_pid" || \
	fail "NUCLEO did not report @BOOT on the ST-LINK UART"
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" || \
	fail "NUCLEO did not report @READY on the ST-LINK UART"

printf '%s\r\n' 'status' 'comms uart info' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "K-FSW status" \
	"$debug_capture_pid" || fail "NUCLEO debug shell did not answer status"
wait_for_output "$work_dir/nucleo.log" "board: $board" \
	"$debug_capture_pid" || fail "NUCLEO status reported an unexpected board"
if $have_storage; then
	printf '%s\r\n' 'storage info' 'storage test' >"$debug_serial"
	wait_for_output "$work_dir/nucleo.log" "mount_point: /kfsw" \
		"$debug_capture_pid" || fail "NUCLEO storage mount point was not reported"
	wait_for_output "$work_dir/nucleo.log" "Storage test: PASS" \
		"$debug_capture_pid" || fail "NUCLEO storage test did not pass"
fi
wait_for_output "$work_dir/nucleo.log" "device: $uart_device" \
	"$debug_capture_pid" || fail "NUCLEO did not report $uart_device UART status"
wait_for_output "$work_dir/nucleo.log" "ready: yes" \
	"$debug_capture_pid" || fail "NUCLEO did not report a ready CSP UART"
wait_for_output "$work_dir/nucleo.log" "CSP interface: KISS" \
	"$debug_capture_pid" || fail "NUCLEO did not report its KISS interface"
wait_for_output "$work_dir/nucleo.log" "CSP node: $node" \
	"$debug_capture_pid" || fail "NUCLEO did not report CSP node $node"
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

printf '%s\n' "csp ping $node" >&3
wait_for_output "$work_dir/linux.log" "CSP ping $node: success" "$linux_pid" || \
	fail "KFSW-Linux could not ping NUCLEO CSP node $node"

printf '%s\r\n' \
	'csp ping 1' \
	'comms uart test' \
	'comms uart info' >"$debug_serial"

wait_for_output "$work_dir/nucleo.log" "CSP ping 1: success" \
	"$debug_capture_pid" || fail "NUCLEO could not ping KFSW-Linux CSP node 1"
wait_for_output "$work_dir/nucleo.log" "UART CSP test: PASS" \
	"$debug_capture_pid" || fail "NUCLEO UART transport test did not pass"

# Keep route and interface evidence on both sides of the physical link.
csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
	'csp routes' || fail "board routes did not complete"
csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
	'csp routes' || fail "host routes did not complete"
for command in 'csp ident' 'csp interfaces' 'comms uart info'; do
	csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
		"$command" || fail "board evidence did not complete: $command"
	csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
		"$command" || fail "host evidence did not complete: $command"
done

csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
	'csp rdp' || fail "cannot read original RDP level"
original_rdp="$(sed -n 's/^CSP RDP trace: \([0-2]\)$/\1/p' <<<"$CSP_REPLY")"
[[ "$original_rdp" =~ ^[0-2]$ ]] || fail "invalid original RDP level"
for level in 0 1 2; do
	csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
		"csp rdp $level" && csp_reply_has "CSP RDP trace: $level" || \
		fail "RDP level $level was not accepted"
	csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
		'csp rdp' && csp_reply_has "CSP RDP trace: $level" || \
		fail "RDP level $level did not read back"
done
for invalid in 256 -1 text; do
	csp_refusal_check "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
		"csp rdp $invalid" 'csp rdp' \
		'level is 0 off, 1 errors, 2 with the protocol' \
		"CSP RDP trace: $invalid" 'CSP RDP trace: 2' || \
		fail "RDP refusal/preservation failed for $invalid"
done
csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
	"csp rdp $original_rdp" && csp_reply_has "CSP RDP trace: $original_rdp" || \
	fail "RDP restoration failed"
csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
	'csp rdp' && csp_reply_has "CSP RDP trace: $original_rdp" || \
	fail "restored RDP level did not read back"
original_rdp=""
echo 'CSP DEBUG RDP: PASS levels=0,1,2 refused=256,-1,text restored=yes'

csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
	"param get $node packet_trace" || fail "cannot read original packet trace"
original_packet_trace="$(sed -n "s/^$node:packet_trace = \([01]\)$/\1/p" <<<"$CSP_REPLY")"
[[ "$original_packet_trace" =~ ^[01]$ ]] || fail "invalid original packet trace"
for value in 0 1; do
	csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
		"param set $node packet_trace $value" && \
		csp_reply_has "$node:packet_trace = $value" || fail "packet trace set failed"
	csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
		"param get $node packet_trace" && \
		csp_reply_has "$node:packet_trace = $value" || fail "packet trace readback failed"
done
csp_refusal_check "$work_dir/linux.log" "$linux_pid" send_linux \
	"param set $node packet_trace 2" "param get $node packet_trace" \
	"param set: node $node did not answer" "$node:packet_trace = 2" \
	"$node:packet_trace = 1" || fail "packet trace refusal/preservation failed"
csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
	"param set $node packet_trace $original_packet_trace" && \
	csp_reply_has "$node:packet_trace = $original_packet_trace" || fail "packet trace restore failed"
csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
	"param get $node packet_trace" && \
	csp_reply_has "$node:packet_trace = $original_packet_trace" || fail "packet trace restore readback failed"
original_packet_trace=""
echo 'CSP DEBUG PACKET: PASS values=0,1 refused=2 restored=yes'

# The hosted node's compiled peer is the L496; name the node under test so the
# same check works for either board.
printf '%s\n' "comms uart test $node" 'comms uart info' >&3
wait_for_output "$work_dir/linux.log" "UART CSP test: PASS" "$linux_pid" || \
	fail "KFSW-Linux UART transport test did not pass"

if $have_storage; then
	printf '%s\n' \
		'ftp generate /build/hil-4k.bin 4096' \
		'ftp mkdir 2 /hil' \
		'ftp put 2 /build/hil-4k.bin /hil/hil-4k.bin' \
		'ftp stat 2 /hil/hil-4k.bin' \
		'ftp ls 2 /hil' \
		'ftp get 2 /hil/hil-4k.bin /build/hil-4k-returned.bin' \
		'ftp verify /build/hil-4k.bin /build/hil-4k-returned.bin' \
		'ftp generate /build/hil-16k.bin 16384' \
		'ftp put 2 /build/hil-16k.bin /hil/hil-16k.bin' \
		'ftp get 2 /hil/hil-16k.bin /build/hil-16k-returned.bin' \
		'ftp verify /build/hil-16k.bin /build/hil-16k-returned.bin' \
		'csp ping 2' \
		'param get 2 test_u32' \
		'comms uart info' >&3

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
fi

csp_exchange "$work_dir/nucleo.log" "$debug_capture_pid" send_debug \
	'comms uart info' || fail "final board counters did not complete"
csp_exchange "$work_dir/linux.log" "$linux_pid" send_linux \
	'comms uart info' || fail "final host counters did not complete"
printf '%s\n' "param get $node node_id" >&3
wait_for_output "$work_dir/linux.log" "$node:node_id = $node" "$linux_pid" || \
	fail "remote node_id did not match the built CSP node"

for log_file in linux.log nucleo.log; do
	last_stats="$(grep -F 'KISS tx=' "$work_dir/$log_file" | tail -1 || true)"
	grep -Eq 'KISS tx=[1-9][0-9]* rx=[1-9][0-9]*' <<<"$last_stats" || \
		fail "$log_file does not contain nonzero KISS traffic statistics"
	grep -Fq 'txerr=0 rxerr=0 drop=0 frame=0' <<<"$last_stats" || \
		fail "$log_file does not contain clean KISS statistics"
done

echo "--- linux.log ---"
cat "$work_dir/linux.log"
echo "--- nucleo.log ---"
cat "$work_dir/nucleo.log"
echo "--- socat.log ---"
sed -n '1,120p' "$work_dir/socat.log"
echo "UART CSP HIL RESULT: PASS"
