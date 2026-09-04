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

work_dir="$(mktemp -d /tmp/k-ground-fwu-ftp.XXXXXX)"
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
	echo "K-GROUND FWU-FTP RESULT: FAIL"
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
		--drop-every 3000 --drop-bytes 32 \
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

# The file transfer route needs the image as a file on the sending node, so it
# is generated there rather than read from the host. That is the difference
# between the two routes: this one moves a file that already exists on a node.
printf '%s\n' 'ftp generate /build/image.bin 20000' >&4
wait_for_output "$work_dir/node19.log" "FTP generate" "$node19_pid" || \
	fail "the sending node could not produce a stand-in image"

image_crc="$(sed -n 's/.*crc32=\([0-9a-f]*\).*/\1/p' "$work_dir/node19.log" | tail -1)"
[[ -n "$image_crc" ]] || fail "could not read the image checksum"

printf '%s\n' 'fwu abort' 'fwu status' >&3
wait_for_output "$work_dir/node16.log" "state: idle" "$node16_pid" || \
	fail "node 16 did not start idle"

# An ordinary put, addressed to the reserved name. Nothing about the wire
# protocol changes; only where the bytes land.
printf '%s\n' 'ftp put 16 /build/image.bin firmware.bin' >&4
wait_for_output "$work_dir/node19.log" "FTP put" "$node19_pid" 600 || \
	fail "the put did not complete"

# The image must be in the update service, not in the filesystem. A file of
# that name appearing in the transfer root would mean the reserved path was not
# recognised and the image was quietly stored as data.
printf '%s\n' 'fwu status' 'ftp 16 ls /' >&3
wait_for_output "$work_dir/node16.log" "received: 20000" "$node16_pid" || \
	fail "the update service did not receive the image"
wait_for_output "$work_dir/node16.log" "actual_crc32: $image_crc" "$node16_pid" || \
	fail "the received image does not match what was sent"

if sed -n '/ftp 16 ls \//,$p' "$work_dir/node16.log" | grep -aq "firmware.bin"; then
	fail "the image was stored as a file instead of reaching the update service"
fi

if [[ "$lossy_link" -eq 1 ]]; then
	# The point of the lossy run. A clean result here would mean the losses
	# were not reaching the transfer, and the recovery path would still be
	# untested.
	echo "K-GROUND FWU-FTP RESULT: PASS crc32=$image_crc bytes=20000 lossy=yes"
else
	echo "K-GROUND FWU-FTP RESULT: PASS crc32=$image_crc bytes=20000"
fi
