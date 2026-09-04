#!/usr/bin/env bash
set -Eeuo pipefail

# Software round trip of one file between the two configured ground roles.
# kfsw-ops (node 19) uploads test.txt to kfsw-gnd-uhf (node 16), reads its
# metadata back, downloads it again, and compares the two local copies.

KGROUND_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KGROUND_TESTS_DIR="$(dirname "$KGROUND_TEST")"
KGROUND_REPO_DIR="$(dirname "$KGROUND_TESTS_DIR")"
KGROUND_WORKSPACE_ROOT="$(dirname "$KGROUND_REPO_DIR")"
KGROUND_BUILD_ROOT="${KGROUND_BUILD_ROOT:-$KGROUND_WORKSPACE_ROOT/build/k-ground}"

lossy_link=0
while [[ $# -gt 0 ]]; do
	case "$1" in
	--lossy)
		# Drop bytes in the bridge, so the transfer has to recover rather
		# than merely complete.
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
	# Seconds. A shell command answers in well under the default; a transfer
	# of a hundred blocks does not, and the machine running this may be much
	# slower than the one it was written on.
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

# How long a whole-image transfer may take. Generous on purpose: an unhelpful
# timeout here reports a stall that never happened.
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
# Both nodes carry the update service: one to receive an image, the other to
# send it. The direct upload path is exercised here rather than the file
# transfer route, so blocks and their individual checksums are what crosses the
# link.
fwu_kconfig='CONFIG_KFSW_FWU=y
CONFIG_KFSW_FWU_MCUBOOT=n
CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS=1
CONFIG_KFSW_FWU_LITE=y
CONFIG_KFSW_FWU_LITE_CSP=y
CONFIG_KFSW_FWU_LITE_BLOCK_SIZE=192
CONFIG_KFSW_FWU_LITE_RDP=n
# Both nodes are processes on one machine, so a reply that is coming arrives in
# milliseconds. A short timeout keeps a deliberately lossy run tractable
# instead of spending seconds waiting for packets that were discarded.
CONFIG_KFSW_FWU_LITE_TIMEOUT_MS=1500
CONFIG_KFSW_FWU_LITE_BLOCK_RETRIES=12'

{
	printf '%s\n' "KFSW_CSP_ROUTES='19/14 KISS'"
	printf "KFSW_EXTRA_KCONFIG='%s'\n" "$fwu_kconfig"
} >>"$station_dir/nodes/kfsw-gnd-uhf.env"
{
	printf '%s\n' "KFSW_CSP_ROUTES='16/14 KISS'"
	printf "KFSW_EXTRA_KCONFIG='%s'\n" "$fwu_kconfig"
} >>"$station_dir/nodes/kfsw-ops.env"

KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf
KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-ops

node16_executable="$KGROUND_BUILD_ROOT/node-16/zephyr/zephyr.exe"
node19_executable="$KGROUND_BUILD_ROOT/node-19/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node19_executable" ]] || fail "node 19 executable is missing"

for node_config in node-16 node-19; do
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

if [[ "$lossy_link" -eq 1 ]]; then
	# A transfer that only ever runs over a clean link has never exercised
	# the part of it that recovers. Bytes are dropped in runs, which is what
	# a lost packet looks like from either end.
	python3 "$KGROUND_REPO_DIR/tests/support/lossy-link.py" \
		--left "$node16_pty" --right "$node19_pty" \
		--drop-every 9000 --drop-bytes 6 \
		--ready-file "$work_dir/bridge.ready" \
		>"$work_dir/socat.log" 2>&1 &
	bridge_pid=$!
	wait_for_output "$work_dir/bridge.ready" "lossy link ready" "$bridge_pid" || \
		fail "the lossy bridge did not become ready"
else
	socat -d -d "$node16_pty,raw,echo=0" "$node19_pty,raw,echo=0" \
		>"$work_dir/socat.log" 2>&1 &
	bridge_pid=$!
	wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
		"$bridge_pid" || fail "the ground CSP UART bridge did not become ready"
fi

printf '%s\n' 'csp ping 19' >&3
wait_for_output "$work_dir/node16.log" "CSP ping 19: success" "$node16_pid" || \
	fail "node 16 could not ping node 19"

# The image lives on the host, which is where a ground station's images
# actually are. A node running as a process reads it directly rather than
# needing it copied into a simulated flash partition first, which would add a
# step and a size limit a real firmware image would exceed.
#
# Large enough to cross many blocks: at 192 bytes a block this is over a
# hundred, so ordering and the final short block are both exercised.
image_path="$work_dir/image.bin"
head -c 20000 /dev/urandom >"$image_path" || fail "could not create a stand-in image"

image_crc="$(python3 -c "
import zlib, pathlib, sys
print(f'{zlib.crc32(pathlib.Path(sys.argv[1]).read_bytes()) & 0xFFFFFFFF:08x}')
" "$image_path")"
[[ -n "$image_crc" ]] || fail "could not compute the image checksum"
echo "K-GROUND FWU-LITE: host image $image_path crc32=$image_crc"

# The receiving node must start from nothing, so a stale slot cannot be
# mistaken for a successful transfer.
printf '%s\n' 'fwu abort' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: idle" "$node16_pid" || \
	fail "node 16 did not start idle"

printf '%s\n' "fwu send 16 $image_path" >&4
wait_for_output "$work_dir/node19.log" "Image accepted and verified" \
	"$node19_pid" "$TRANSFER_LIMIT_S" || fail "the image was not accepted by node 16"

# What the receiver holds must match what the sender computed, byte count and
# checksum both. Either alone would pass on a transfer that lost a whole block
# and gained a duplicate.
printf '%s\n' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "received: 20000" "$node16_pid" || \
	fail "node 16 did not receive the whole image"
wait_for_output "$work_dir/node16.log" "actual_crc32: $image_crc" "$node16_pid" || \
	fail "the received image does not match what was sent"
wait_for_output "$work_dir/node16.log" "expected_crc32: $image_crc" \
	"$node16_pid" || fail "node 16 recorded the wrong expected checksum"

# Sending stops at a verified image. Committing it is a separate command, so a
# node is never left booting something merely because it arrived.
wait_for_output "$work_dir/node16.log" "state: receiving" "$node16_pid" || \
	fail "node 16 should still hold the transfer until it is told to flash"

printf '%s\n' 'fwu flash 16' >&4
wait_for_output "$work_dir/node19.log" "scheduled a swap" "$node19_pid" || \
	fail "node 16 did not accept the instruction to flash"

printf '%s\n' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: ready" "$node16_pid" || \
	fail "node 16 did not reach the ready state after being told to flash"

resent="$(sed -n 's/.*verified; \([0-9]*\) block(s) resent.*/\1/p' \
	"$work_dir/node19.log" | tail -1)"
resent="${resent:-0}"

if [[ "$lossy_link" -eq 1 ]]; then
	# The point of the lossy run. A clean result here would mean the losses
	# were not reaching the transfer, and the recovery path would still be
	# untested.
	[[ "$resent" -gt 0 ]] || \
		fail "the link dropped bytes but no block was resent; the loss never reached the transfer"
	echo "K-GROUND FWU-LITE RESULT: PASS crc32=$image_crc bytes=20000 blocks=105 lossy=yes resent=$resent"
else
	echo "K-GROUND FWU-LITE RESULT: PASS crc32=$image_crc bytes=20000 blocks=105 resent=$resent"
fi
