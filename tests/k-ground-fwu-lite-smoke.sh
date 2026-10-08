#!/usr/bin/env bash
set -Eeuo pipefail

# Upload a firmware image from node 19 to node 16, verify it, then request a swap.

KGROUND_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KGROUND_TESTS_DIR="$(dirname "$KGROUND_TEST")"
KGROUND_REPO_DIR="$(dirname "$KGROUND_TESTS_DIR")"
KGROUND_WORKSPACE_ROOT="$(dirname "$KGROUND_REPO_DIR")"
KGROUND_BUILD_ROOT="${KGROUND_BUILD_ROOT:-${KFSW_OUTPUT_ROOT:-$KGROUND_WORKSPACE_ROOT/build}/k-ground}"

csp_version="${KFSW_CSP_VERSION:-2}"
case "$csp_version" in
1) csp_suffix="-csp1" ;;
2) csp_suffix="" ;;
*) echo "ERROR: unsupported CSP version: $csp_version"; exit 1 ;;
esac

lossy_link=0
while [[ $# -gt 0 ]]; do
	case "$1" in
	--lossy)
		# Drop bytes in the bridge so the transfer has to recover.
		lossy_link=1
		;;
	*)
		echo "ERROR: unknown argument: $1"
		exit 1
		;;
	esac
	shift
done

work_dir="$(mktemp -d /tmp/k-ground-fwu-lite.XXXXXX)"
station_dir="$work_dir/ground-station"
node16_pid=""
node19_pid=""
bridge_pid=""

cleanup()
{
	[[ -n "$bridge_pid" ]] && kill "$bridge_pid" 2>/dev/null || true
	[[ -n "$node16_pid" ]] && kill "$node16_pid" 2>/dev/null || true
	[[ -n "$node19_pid" ]] && kill "$node19_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 4>&-
	rm -rf -- "$work_dir"
}

fail()
{
	echo "K-GROUND FWU-LITE RESULT: FAIL"
	echo "  $1"

	for log_file in node16.log node19.log socat.log; do
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
	# Seconds. A transfer of a hundred blocks takes longer than a shell command.
	local limit="${4:-30}"
	local waited=0

	while true; do
		grep -Fq "$expected" "$file" 2>/dev/null && return 0
		kill -0 "$process_pid" 2>/dev/null || return 1
		awk -v w="$waited" -v l="$limit" 'BEGIN{exit !(w>=l)}' && return 1
		sleep 0.05
		waited="$(awk -v w="$waited" 'BEGIN{printf "%.2f", w+0.05}')"
	done
}

# Time limit for a whole image transfer, generous for slow machines.
readonly TRANSFER_LIMIT_S=600

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

command -v socat >/dev/null 2>&1 || fail "socat is required"

mkdir -p "$station_dir/nodes"
cp "$KGROUND_REPO_DIR/ground-station/nodes/kfsw-gnd-uhf.env" \
	"$station_dir/nodes/kfsw-gnd-uhf.env"
cp "$KGROUND_REPO_DIR/ground-station/nodes/kfsw-ops.env" \
	"$station_dir/nodes/kfsw-ops.env"
# Both nodes have the update service. This test uses the direct upload path.
fwu_kconfig='CONFIG_KFSW_FWU=y
CONFIG_KFSW_FWU_MCUBOOT=n
CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS=1
CONFIG_KFSW_FWU_LITE=y
CONFIG_KFSW_FWU_LITE_CSP=y
CONFIG_KFSW_FWU_LITE_BLOCK_SIZE=192
CONFIG_KFSW_FWU_LITE_RDP=n
# Both nodes run on one machine, so a short timeout keeps the lossy run quick.
CONFIG_KFSW_FWU_LITE_TIMEOUT_MS=1500
CONFIG_KFSW_FWU_LITE_BLOCK_RETRIES=12'

{
	printf '%s\n' "KFSW_CSP_ROUTES='19 KISS'"
	printf 'KFSW_CSP_VERSION=%s\n' "$csp_version"
	printf "KFSW_EXTRA_KCONFIG='%s'\n" "$fwu_kconfig"
} >>"$station_dir/nodes/kfsw-gnd-uhf.env"
{
	printf '%s\n' "KFSW_CSP_ROUTES='16 KISS'"
	printf 'KFSW_CSP_VERSION=%s\n' "$csp_version"
	printf "KFSW_EXTRA_KCONFIG='%s'\n" "$fwu_kconfig"
} >>"$station_dir/nodes/kfsw-ops.env"

KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf
KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-ops

node16_executable="$KGROUND_BUILD_ROOT/kfsw-gnd-uhf-node-16$csp_suffix/zephyr/zephyr.exe"
node19_executable="$KGROUND_BUILD_ROOT/kfsw-ops-node-19$csp_suffix/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node19_executable" ]] || fail "node 19 executable is missing"

for node_config in kfsw-gnd-uhf-node-16$csp_suffix kfsw-ops-node-19$csp_suffix; do
	grep -Fq "CONFIG_KFSW_CSP_VERSION_$csp_version=y" \
		"$KGROUND_BUILD_ROOT/$node_config/zephyr/.config" || \
		fail "$node_config did not compose CSP $csp_version"
	grep -Fq 'CONFIG_KFSW_FWU_LITE_CSP=y' \
		"$KGROUND_BUILD_ROOT/$node_config/zephyr/.config" || \
		fail "$node_config did not compose the direct upload path"
done

mkfifo "$work_dir/node16.in" "$work_dir/node19.in"
exec 3<>"$work_dir/node16.in"
exec 4<>"$work_dir/node19.in"

"$node16_executable" --uart_stdinout --device_id=16 --no-color \
	-flash="$work_dir/node16-flash.bin" \
	<&3 >"$work_dir/node16.log" 2>&1 &
node16_pid=$!

"$node19_executable" --uart_stdinout --device_id=19 --no-color \
	-flash="$work_dir/node19-flash.bin" \
	<&4 >"$work_dir/node19.log" 2>&1 &
node19_pid=$!

wait_for_output "$work_dir/node16.log" "@READY " "$node16_pid" || \
	fail "node 16 did not report readiness"
wait_for_output "$work_dir/node19.log" "@READY " "$node19_pid" || \
	fail "node 19 did not report readiness"
for node in 16 19; do
	grep -Fq '@SERVICES degraded' "$work_dir/node$node.log" && \
		fail "node $node started with a failed service"
done
wait_for_output "$work_dir/node16.log" \
	"uart_1 connected to pseudotty: " "$node16_pid" || \
	fail "node 16 did not expose its CSP UART"
wait_for_output "$work_dir/node19.log" \
	"uart_1 connected to pseudotty: " "$node19_pid" || \
	fail "node 19 did not expose its CSP UART"

node16_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node16.log" | head -1)"
node19_pty="$(sed -n 's/^uart_1 connected to pseudotty: //p' \
	"$work_dir/node19.log" | head -1)"

drop_every=1000000000
[[ "$lossy_link" -eq 0 ]] || drop_every=9000
python3 "$KGROUND_REPO_DIR/tests/support/lossy-link.py" \
	--left "$node16_pty" --right "$node19_pty" \
	--drop-every "$drop_every" --drop-bytes 6 \
	--csp-version "$csp_version" --lite-stats "$work_dir/lite-stats.json" \
	--ready-file "$work_dir/bridge.ready" >"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/bridge.ready" "lossy link ready" "$bridge_pid" || \
	fail "the PTY bridge did not become ready"

printf '%s\n' 'csp ping 19' >&3
wait_for_output "$work_dir/node16.log" "CSP ping 19: success" "$node16_pid" || \
	fail "node 16 could not ping node 19"

# The image is a host file, read directly by the native node. It is large
# enough for over a hundred 192-byte blocks, including a short last block.
image_path="$work_dir/image.bin"
head -c 20000 /dev/urandom >"$image_path" || fail "could not create a stand-in image"

image_crc="$(python3 -c "
import zlib, pathlib, sys
print(f'{zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes()) & 0xFFFFFFFF:08x}')
" "$image_path")"
[[ -n "$image_crc" ]] || fail "could not compute the image checksum"
echo "K-GROUND FWU-LITE: host image $image_path crc32=$image_crc"

# Start from an empty slot so an old image can't pass as this transfer.
printf '%s\n' 'fwu abort' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: idle" "$node16_pid" || \
	fail "node 16 did not start idle"

printf '%s\n' "fwu send 16 $image_path" >&4
wait_for_output "$work_dir/node19.log" "Image accepted and verified" \
	"$node19_pid" "$TRANSFER_LIMIT_S" || fail "the image was not accepted by node 16"

# Byte count and checksum must both match.
printf '%s\n' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "received: 20000" "$node16_pid" || \
	fail "node 16 did not receive the whole image"
wait_for_output "$work_dir/node16.log" "actual_crc32: $image_crc" "$node16_pid" || \
	fail "the received image does not match what was sent"
wait_for_output "$work_dir/node16.log" "expected_crc32: $image_crc" \
	"$node16_pid" || fail "node 16 recorded the wrong expected checksum"

# Sending stops at a verified image; flashing is a separate command.
wait_for_output "$work_dir/node16.log" "state: verified" "$node16_pid" || \
	fail "node 16 should hold a verified image until it is told to flash"

printf '%s\n' 'fwu flash 16' >&4
wait_for_output "$work_dir/node19.log" "scheduled a swap" "$node19_pid" || \
	fail "node 16 did not accept the instruction to flash"

printf '%s\n' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: ready" "$node16_pid" || \
	fail "node 16 did not reach the ready state after being told to flash"

read -r blocks resent dropped < <(python3 - "$work_dir/lite-stats.json" <<'PYSTATS'
import json, sys
stats = json.load(open(sys.argv[1]))
print(stats['blocks'], stats['resent'], stats['dropped'])
PYSTATS
)
[[ "$blocks" -eq 105 ]] || fail "the bridge did not observe all 105 image blocks"

# Read the receiver's state through PARAM on the sending ground node. PARAM has
# no --retry, unlike the command, csp and hk paths, so a dropped reply on the
# lossy run is simply lost: ask again rather than call the update a failure,
# since what this case proves is the upload under loss, not PARAM under loss.
read_remote_field() # field, expected text
{
	local attempt
	for attempt in 1 2 3; do
		printf 'param get 16 %s\n' "$1" >&4
		wait_for_output "$work_dir/node19.log" "$2" "$node19_pid" && return 0
	done
	return 1
}

read_remote_field fwu_received "16:fwu_received = 20000" || \
	fail "ground did not receive the remote byte count"
for field in fwu_actual_crc fwu_expected_crc; do
	read_remote_field "$field" "16:$field = 0x$image_crc" || \
		fail "ground did not receive the matching remote checksum"
done
# Expose only validated reply rows for the Robot assertions.
tr -d '\r' <"$work_dir/node19.log" | sed -n '/^16:fwu_.* = /p'

if [[ "$lossy_link" -eq 1 ]]; then
	# The lossy run must show resent blocks.
	[[ "$resent" -gt 0 && "$dropped" -gt 0 ]] || \
		fail "the link dropped bytes but no block was resent; the loss never reached the transfer"
	echo "K-GROUND FWU-LITE RESULT: PASS crc32=$image_crc bytes=20000 blocks=$blocks lossy=yes resent=$resent"
else
	echo "K-GROUND FWU-LITE RESULT: PASS crc32=$image_crc bytes=20000 blocks=$blocks resent=$resent"
fi
