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
compiled_log_level_default=""

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

wait_for_output_count()
{
	local file="$1"
	local expected="$2"
	local required_count="$3"
	local process_pid="$4"
	local observed

	for _ in {1..1200}; do
		observed="$(grep -Fc "$expected" "$file" 2>/dev/null || true)"
		((observed >= required_count)) && return 0
		kill -0 "$process_pid" 2>/dev/null || return 1
		sleep 0.05
	done
	return 1
}

validate_log_callback()
{
	local active_level="$1"
	local callback_output

	callback_output="$(sed -n '/kfsw:~\$ log test/,$p' "$work_dir/nucleo.log")"
	grep -Fq '[ERROR] K-FSW shell log test: error' <<<"$callback_output" || return 1
	if [[ "$active_level" == 2 ]]; then
		grep -Fq '[WARNING] K-FSW shell log test: warning' \
			<<<"$callback_output" || return 1
	else
		grep -Fq 'K-FSW shell log test: warning' <<<"$callback_output" && return 1
	fi
	grep -Fq 'K-FSW shell log test: info' <<<"$callback_output" && return 1
	grep -Fq 'K-FSW shell log test: debug' <<<"$callback_output" && return 1
	return 0
}

wait_for_clean_transport_stats()
{
	local file="$1"
	local process_pid="$2"
	local last_interface
	local last_uart

	for _ in {1..1200}; do
		last_uart="$(grep -F 'KISS tx=' "$file" | tail -1 || true)"
		last_interface="$(grep -F 'KISS addr=' "$file" | tail -1 || true)"
		if grep -Eq 'KISS tx=[1-9][0-9]* rx=[1-9][0-9]* txerr=0 rxerr=0 drop=0 frame=0' \
			<<<"$last_uart" && \
			grep -Eq 'KISS addr=[0-9]+/0 default=no tx=[1-9][0-9]* rx=[1-9][0-9]* txerr=0 rxerr=0 drop=0' \
			<<<"$last_interface"; then
			return 0
		fi
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
KFSW_PRISTINE=always \
	KGROUND_BUILD_ROOT="$ground_build_root" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/k-ground.overlay" \
	"$KFSW_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf --peer 2

echo "HOLYBRO CSP/KISS: building NUCLEO node 2 with peer 16"
KFSW_PRISTINE=always \
	KFSW_BUILD_DIR="$nucleo_build_dir" \
	KFSW_EXTRA_CONF_FILE="$HOLYBRO_DIR/nucleo_l496zg.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$HOLYBRO_DIR/nucleo_l496zg.overlay" \
	"$KFSW_REPO_DIR/tools/build.sh" nucleo_l496zg

[[ -x "$ground_executable" ]] || fail "ground executable was not produced"
compiled_log_level_default="$(sed -n \
	's/^CONFIG_KFSW_LOG_MIN_LEVEL=\([0-4]\)$/\1/p' \
	"$nucleo_build_dir/zephyr/.config")"
[[ -n "$compiled_log_level_default" ]] || \
	fail "cannot determine the NUCLEO compiled log_level default"

debug_stty="$(stty -F "$debug_serial" -g)" || \
	fail "cannot read NUCLEO debug serial settings"
radio_stty="$(stty -F "$radio_device" -g)" || \
	fail "cannot read Holybro serial settings"

stty -F "$debug_serial" "$KFSW_SERIAL_BAUD" cs8 -cstopb -parenb -crtscts raw -echo
stty -F "$radio_device" "$radio_baud" cs8 -cstopb -parenb -crtscts raw -echo

timeout 480s cat "$debug_serial" >"$work_dir/nucleo.log" &
debug_capture_pid=$!

west flash -d "$nucleo_build_dir" --runner openocd || \
	fail "NUCLEO flash failed"
wait_for_output "$work_dir/nucleo.log" "@READY " "$debug_capture_pid" || \
	fail "NUCLEO did not report readiness"

printf '%s\r\n' 'status' 'uhf status' 'uart info' 'csp interfaces' 'csp routes' \
	>"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "CSP node: 2" "$debug_capture_pid" || \
	fail "NUCLEO did not report CSP node 2"
wait_for_output "$work_dir/nucleo.log" "CSP peer: 16" "$debug_capture_pid" || \
	fail "NUCLEO did not report ground peer 16"
wait_for_output "$work_dir/nucleo.log" "baudrate: 57600" "$debug_capture_pid" || \
	fail "NUCLEO did not report the Holybro serial rate"
wait_for_output "$work_dir/nucleo.log" "implementation: holybro-sik" \
	"$debug_capture_pid" || fail "NUCLEO did not report the Holybro module"
wait_for_output "$work_dir/nucleo.log" "expected serial: 57600 8N1" \
	"$debug_capture_pid" || fail "NUCLEO UHF expectation does not match the UART profile"
wait_for_output "$work_dir/nucleo.log" "RF link: unknown" "$debug_capture_pid" || \
	fail "NUCLEO did not preserve unknown RF state semantics"
wait_for_output "$work_dir/nucleo.log" "KISS addr=2/0" "$debug_capture_pid" || \
	fail "NUCLEO did not report its KISS interface"
wait_for_output "$work_dir/nucleo.log" "0/0 -> KISS direct" "$debug_capture_pid" || \
	fail "NUCLEO did not report its default KISS route"

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

printf '%s\n' 'status' 'uhf status' 'uart info' 'csp interfaces' 'csp routes' \
	'csp ping 2' >&3
wait_for_output "$work_dir/ground.log" "Role: kfsw-gnd-uhf" "$ground_pid" || \
	fail "the UHF gateway did not report its role"
wait_for_output "$work_dir/ground.log" "baudrate: 57600" "$ground_pid" || \
	fail "k-ground did not report the Holybro serial rate"
wait_for_output "$work_dir/ground.log" "implementation: holybro-sik" "$ground_pid" || \
	fail "k-ground did not report the Holybro module"
wait_for_output "$work_dir/ground.log" "expected serial: 57600 8N1" "$ground_pid" || \
	fail "k-ground UHF expectation does not match the UART profile"
wait_for_output "$work_dir/ground.log" "RF link: unknown" "$ground_pid" || \
	fail "k-ground did not preserve unknown RF state semantics"
wait_for_output "$work_dir/ground.log" "KISS addr=16/0" "$ground_pid" || \
	fail "k-ground did not report its KISS interface"
wait_for_output "$work_dir/ground.log" "0/0 -> KISS direct" "$ground_pid" || \
	fail "k-ground did not report its default KISS route"
wait_for_output "$work_dir/ground.log" "CSP ping 2: success" "$ground_pid" || \
	fail "k-ground node 16 could not ping NUCLEO node 2 over Holybro"

printf '%s\r\n' 'csp ping 16' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "CSP ping 16: success" \
	"$debug_capture_pid" || \
	fail "NUCLEO node 2 could not ping k-ground node 16 over Holybro"

printf '%s\n' 'param list 2' 'param get 2 log_level' >&3
# The listing addresses each parameter by table and offset. A remote node's
# table names are not on the wire, so the number stands in for them: node_id is
# offset 0 of the core board table, log_level offset 0 of the log service's.
wait_for_output "$work_dir/ground.log" "1           0x00  node_id" "$ground_pid" || \
	fail "the production NUCLEO parameter list is missing node_id"
wait_for_output "$work_dir/ground.log" "25          0x00  log_level" "$ground_pid" || \
	fail "the production NUCLEO parameter list is missing log_level"
# Identity as text, which needs the whole string path to survive the radio.
wait_for_output "$work_dir/ground.log" "1           0x10  uid" "$ground_pid" || \
	fail "the production NUCLEO parameter list is missing uid"
wait_for_output "$work_dir/ground.log" "4           0x20  route_table" "$ground_pid" || \
	fail "the production NUCLEO parameter list is missing route_table"
# A string read across the radio. This is the whole string path end to end:
# sampled from the running CSP identity on the NUCLEO, packed into a libparam
# transfer, carried over RF, and rendered quoted on the ground.
printf '%s\n' 'param get 2 uid' >&3
wait_for_output "$work_dir/ground.log" '2:uid = "kfsw-2"' "$ground_pid" || \
	fail "the NUCLEO identity did not survive the radio as text"

wait_for_output "$work_dir/ground.log" "2:log_level = " "$ground_pid" || \
	fail "k-ground could not read the NUCLEO log_level parameter"
if grep -Eq '2:[0-9]+ +test_(u32|i32|float)' "$work_dir/ground.log"; then
	fail "the production NUCLEO parameter list contains test-only definitions"
fi
original_log_level="$(tr -d '\r' <"$work_dir/ground.log" | \
	sed -n 's/.*2:log_level = \([0-4]\)$/\1/p' | head -1)"
[[ -n "$original_log_level" ]] || fail "the initial remote log_level is invalid"

alternate_log_level=""
for candidate in 3 2 4 0; do
	if [[ "$candidate" != "$original_log_level" && \
		"$candidate" != "$compiled_log_level_default" ]]; then
		alternate_log_level="$candidate"
		break
	fi
done
[[ -n "$alternate_log_level" ]] || fail "cannot select a non-default log_level"
printf '%s\n' \
	"param set 2 log_level $alternate_log_level" \
	'param get 2 log_level' >&3
wait_for_output_count "$work_dir/ground.log" \
	"2:log_level = $alternate_log_level" 2 "$ground_pid" || \
	fail "valid remote log_level set/readback did not pass"

nucleo_status_count_before="$(grep -Fc 'K-FSW status' "$work_dir/nucleo.log" || true)"
printf '%s\r\n' 'log test' 'status' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "K-FSW shell log test: error" \
	"$debug_capture_pid" || fail "the NUCLEO log callback test did not run"
wait_for_output_count "$work_dir/nucleo.log" "K-FSW status" \
	"$((nucleo_status_count_before + 1))" "$debug_capture_pid" || \
	fail "the NUCLEO log callback output did not reach its status barrier"
validate_log_callback "$alternate_log_level" || \
	fail "the owner callback did not apply the remote log_level"

default_count_before="$(grep -Fc \
	"2:log_level = $compiled_log_level_default" "$work_dir/ground.log" || true)"
ground_status_count_before="$(grep -Fc 'K-FSW status' "$work_dir/ground.log" || true)"
negative_start_line="$(($(wc -l <"$work_dir/ground.log") + 1))"
printf '%s\n' 'param set 2 log_level 5' 'param get 2 log_level' \
	'param get 2 missing' 'csp ping 3' 'status' >&3
wait_for_output "$work_dir/ground.log" "2:log_level = 5" "$ground_pid" || \
	fail "the invalid remote log_level request was not transmitted"
wait_for_output_count "$work_dir/ground.log" \
	"2:log_level = $compiled_log_level_default" \
	"$((default_count_before + 1))" "$ground_pid" || \
	fail "invalid remote log_level did not restore the compiled default"
wait_for_output "$work_dir/ground.log" "get: parameter 'missing' not found" \
	"$ground_pid" || fail "missing remote parameter was not rejected"
wait_for_output "$work_dir/ground.log" "CSP ping 3: failed" "$ground_pid" || \
	fail "a nonexistent CSP node did not fail cleanly"
wait_for_output_count "$work_dir/ground.log" "K-FSW status" \
	"$((ground_status_count_before + 1))" "$ground_pid" || \
	fail "the ground shell was not responsive after negative tests"
negative_output="$(tail -n +"$negative_start_line" "$work_dir/ground.log")"
grep -Fq "2:log_level = $compiled_log_level_default" \
	<<<"$negative_output" || fail "the negative-test window lacks default readback"
if grep -Fq "2:log_level = $alternate_log_level" <<<"$negative_output"; then
	fail "invalid remote log_level retained the prior non-default value"
fi

# Commanding and the event record across the radio, and a node reaching itself.
printf '%s\n' \
	'csp ping 16' \
	'cmd 16 noop' \
	'cmd list' \
	'cmd 2 noop' \
	'cmd 2 info' \
	'cmd 2 event_stats' \
	'cmd 2 event_tail 0' \
	'cmd 2 bogus' >&3

# The ground node is 16, so this is addressed to itself. There is no link to
# traverse and reaching the shell at all is the answer; reporting a round-trip
# time would be a measurement of nothing.
wait_for_output "$work_dir/ground.log" "CSP ping 16: this node, no link traversed" \
	"$ground_pid" || fail "the ground node did not answer for itself"
# Addressed to this node, so the command runs here rather than being sent into
# the network and back. Source node 0 is the marker for a command that did not
# arrive over CSP, which is now the truth for it.
wait_for_output "$work_dir/ground.log" "noop node=16: OK noop from node 0" \
	"$ground_pid" || fail "a self-addressed command was not run locally"
wait_for_output "$work_dir/ground.log" "noop node=2: OK noop from node 16" \
	"$ground_pid" || fail "NUCLEO node 2 did not answer a command over Holybro"
wait_for_output "$work_dir/ground.log" "info node=2: OK uptime_ms=" "$ground_pid" || \
	fail "NUCLEO node 2 did not report info over Holybro"
wait_for_output "$work_dir/ground.log" "event_stats node=2: OK held=" "$ground_pid" || \
	fail "NUCLEO node 2 did not report event counters over Holybro"
wait_for_output "$work_dir/ground.log" "event_tail node=2: OK seq=" "$ground_pid" || \
	fail "NUCLEO node 2 did not return a recorded event over Holybro"
wait_for_output "$work_dir/ground.log" "unknown command 'bogus'" "$ground_pid" || \
	fail "an unknown command was not rejected"

# The flight node records the commands it served.
printf '%s\r\n' 'event stats' 'cmd 2 event_stats' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" "recorded: " "$debug_capture_pid" || \
	fail "NUCLEO did not report its event counters"

# File transfer across the radio. The NUCLEO flash persists between runs, so
# the directory may already exist and the file may already be present; the
# upload must overwrite it atomically either way.
printf '%s\n' \
	'ftp generate /build/test.txt 256' \
	'ftp 2 mkdir /uplink' \
	'ftp put 2 /build/test.txt /uplink/test.txt' \
	'ftp stat 2 /uplink/test.txt' \
	'ftp 2 ls /uplink' \
	'ftp get 2 /uplink/test.txt /build/test-returned.txt' \
	'ftp verify /build/test.txt /build/test-returned.txt' \
	'ftp get 2 /uplink/missing.txt /build/missing.txt' >&3

wait_for_output "$work_dir/ground.log" \
	"FTP generate path=/build/test.txt: PASS bytes=256" "$ground_pid" || \
	fail "the ground fixture file was not generated"
uploaded_crc="$(tr -d '\r' <"$work_dir/ground.log" | \
	sed -n 's/^FTP generate path=\/build\/test\.txt: PASS bytes=256 crc32=\([0-9a-f]*\)$/\1/p' |
	head -1)"
[[ -n "$uploaded_crc" ]] || fail "the generated fixture did not report a CRC"

wait_for_output "$work_dir/ground.log" \
	"FTP put node=2 source=/build/test.txt destination=/uplink/test.txt: PASS bytes=256 crc32=$uploaded_crc" \
	"$ground_pid" || fail "the file upload to NUCLEO node 2 over Holybro failed"
wait_for_output "$work_dir/ground.log" \
	"FTP stat node=2 path=/uplink/test.txt type=file bytes=256 crc32=$uploaded_crc" \
	"$ground_pid" || fail "NUCLEO reports different metadata for the uploaded file"
wait_for_output "$work_dir/ground.log" "FTP list: PASS entries=" "$ground_pid" || \
	fail "the remote uplink directory could not be listed over Holybro"
wait_for_output "$work_dir/ground.log" \
	"FTP get node=2 source=/uplink/test.txt destination=/build/test-returned.txt: PASS bytes=256 crc32=$uploaded_crc" \
	"$ground_pid" || fail "the file download from NUCLEO node 2 over Holybro failed"
wait_for_output "$work_dir/ground.log" \
	"FTP verify first=/build/test.txt second=/build/test-returned.txt: PASS" \
	"$ground_pid" || fail "the uploaded and downloaded copies differ"
wait_for_output "$work_dir/ground.log" \
	"FTP get node=2 path=/uplink/missing.txt: not found" "$ground_pid" || \
	fail "a missing remote file was not reported as not found"

# The flight node sees the committed file in its own FTP root.
printf '%s\r\n' 'ftp 2 ls /uplink' 'ftp stat 2 /uplink/test.txt' >"$debug_serial"
wait_for_output "$work_dir/nucleo.log" \
	"FTP stat node=2 path=/uplink/test.txt type=file bytes=256 crc32=$uploaded_crc" \
	"$debug_capture_pid" || \
	fail "NUCLEO does not report the received file in its own FTP root"

printf '%s\r\n' 'uart info' 'csp interfaces' >"$debug_serial"
printf '%s\n' 'uart info' 'csp interfaces' >&3
wait_for_clean_transport_stats "$work_dir/ground.log" "$ground_pid" || \
	fail "k-ground does not have clean, nonzero post-traffic KISS counters"
wait_for_clean_transport_stats "$work_dir/nucleo.log" "$debug_capture_pid" || \
	fail "NUCLEO does not have clean, nonzero post-traffic KISS counters"

cat "$work_dir/ground.log"
cat "$work_dir/nucleo.log"
echo "HOLYBRO CSP/KISS RESULT: PASS bidirectional=yes params=yes ftp=yes cmd=yes event=yes self=yes negative=yes counters=clean"
echo "HOLYBRO FTP: 256 bytes round-tripped node 16 <-> node 2, crc32=$uploaded_crc"
