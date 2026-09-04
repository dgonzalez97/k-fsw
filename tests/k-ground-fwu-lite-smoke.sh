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

if [[ $# -ne 0 ]]; then
	echo "ERROR: unknown argument: $1"
	exit 1
fi

work_dir="$(mktemp -d /tmp/k-ground-ftp.XXXXXX)"
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

	for _ in {1..600}; do
		grep -Fq "$expected" "$file" 2>/dev/null && return 0
		kill -0 "$process_pid" 2>/dev/null || return 1
		sleep 0.05
	done

	return 1
}

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
CONFIG_KFSW_FWU_LITE_RDP=n'

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

socat -d -d "$node16_pty,raw,echo=0" "$node19_pty,raw,echo=0" \
	>"$work_dir/socat.log" 2>&1 &
bridge_pid=$!
wait_for_output "$work_dir/socat.log" "starting data transfer loop" \
	"$bridge_pid" || fail "the ground CSP UART bridge did not become ready"

printf '%s\n' 'csp ping 19' >&3
wait_for_output "$work_dir/node16.log" "CSP ping 19: success" "$node16_pid" || \
	fail "node 16 could not ping node 19"

# A stand-in image, large enough to cross many blocks: at 192 bytes a block
# this is over a hundred of them, so ordering and the final short block are
# both exercised rather than assumed.
printf '%s\n' 'ftp generate /build/image.bin 20000' >&4
wait_for_output "$work_dir/node19.log" "FTP generate" "$node19_pid" || \
	fail "the sending node could not produce a stand-in image"

image_crc="$(sed -n 's/.*crc32=\([0-9a-f]*\).*/\1/p' "$work_dir/node19.log" | tail -1)"
[[ -n "$image_crc" ]] || fail "could not read the image checksum"

# The receiving node must start from nothing, so a stale slot cannot be
# mistaken for a successful transfer.
printf '%s\n' 'fwu abort' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: idle" "$node16_pid" || \
	fail "node 16 did not start idle"

printf '%s\n' 'fwu send 16 /kfsw/ftp/build/image.bin' >&4
wait_for_output "$work_dir/node19.log" "Image accepted and verified" \
	"$node19_pid" || fail "the image was not accepted by node 16"

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

echo "K-GROUND FWU-LITE RESULT: PASS crc32=$image_crc bytes=20000 blocks=105"
