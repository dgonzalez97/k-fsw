#!/usr/bin/env bash
set -Eeuo pipefail

# Software round trip of one file between the two configured ground roles.
# kfsw-ops (node 19) uploads test.txt to kfsw-gnd-uhf (node 16), reads its
# metadata back, downloads it again, and compares the two local copies.
#
# --interrupted does something else: it cuts the link in the middle of a
# download and brings it back, which is what the end of a pass looks like from
# the software. The download has to continue from what arrived, not start the
# file again.

KGROUND_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KGROUND_TESTS_DIR="$(dirname "$KGROUND_TEST")"
KGROUND_REPO_DIR="$(dirname "$KGROUND_TESTS_DIR")"
KGROUND_WORKSPACE_ROOT="$(dirname "$KGROUND_REPO_DIR")"
KGROUND_BUILD_ROOT="${KGROUND_BUILD_ROOT:-${KFSW_OUTPUT_ROOT:-$KGROUND_WORKSPACE_ROOT/build}/k-ground}"

lossy_link=0
interrupted=0
bytes=256
if [[ "${1:-}" == --lossy ]]; then
	lossy_link=1
	bytes=32768
	shift
elif [[ "${1:-}" == --interrupted ]]; then
	interrupted=1
	bytes=32768
	shift
fi
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
	echo "K-GROUND FTP RESULT: FAIL"
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

	for _ in {1..2400}; do
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
printf '%s\n' "KFSW_CSP_ROUTES='19/14 KISS'" \
	>>"$station_dir/nodes/kfsw-gnd-uhf.env"
printf '%s\n' "KFSW_CSP_ROUTES='16/14 KISS'" \
	>>"$station_dir/nodes/kfsw-ops.env"

KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-gnd-uhf
KGROUND_STATION_DIR="$station_dir" \
	"$KGROUND_REPO_DIR/tools/k-ground" build kfsw-ops

node16_executable="$KGROUND_BUILD_ROOT/kfsw-gnd-uhf-node-16/zephyr/zephyr.exe"
node19_executable="$KGROUND_BUILD_ROOT/kfsw-ops-node-19/zephyr/zephyr.exe"
[[ -x "$node16_executable" ]] || fail "node 16 executable is missing"
[[ -x "$node19_executable" ]] || fail "node 19 executable is missing"

for node_config in kfsw-gnd-uhf-node-16 kfsw-ops-node-19; do
	grep -Fq 'CONFIG_KFSW_FTP=y' \
		"$KGROUND_BUILD_ROOT/$node_config/zephyr/.config" || \
		fail "$node_config did not compose the file-transfer service"
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

if [[ "$interrupted" -eq 1 ]]; then
	# Forward everything, then go deaf long enough for both ends to give up
	# on the transfer. --cut-for must outlast the file transfer timeout.
	python3 "$KGROUND_REPO_DIR/tests/support/lossy-link.py" \
		--left "$node16_pty" --right "$node19_pty" \
		--drop-every 1000000000 --drop-bytes 0 \
		--cut-after-get-data 4 --cut-for 20 \
		--ready-file "$work_dir/bridge.ready" \
		>"$work_dir/socat.log" 2>&1 &
	bridge_pid=$!
	wait_for_output "$work_dir/bridge.ready" "lossy link ready" "$bridge_pid" || \
		fail "the cutting bridge did not become ready"
elif [[ "$lossy_link" -eq 1 ]]; then
	# Drop bytes in runs, like lost packets.
	python3 "$KGROUND_REPO_DIR/tests/support/lossy-link.py" \
		--left "$node16_pty" --right "$node19_pty" \
		--drop-every 9000 --drop-bytes 6 \
		--ftp-stats "$work_dir/ftp-stats.json" \
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

if [[ "$interrupted" -eq 1 ]]; then
	# Node 16 holds the file; node 19 downloads it across a pass that ends.
	printf '%s\n' "ftp generate /build/down.bin $bytes" >&3
	wait_for_output "$work_dir/node16.log" "FTP generate /build/down.bin: PASS" \
		"$node16_pid" || fail "node 16 did not generate the file to download"
	source_crc="$(tr -d '\r' <"$work_dir/node16.log" |
		awk '/FTP generate \/build\/down.bin: PASS/ { found = 1 }
		     found && /^crc32: / { print $2; exit }')"
	[[ -n "$source_crc" ]] || fail "the generated file did not report a CRC"

	printf '%s\n' 'ftp get 16 /build/down.bin /build/down-copy.bin' >&4
	wait_for_output "$work_dir/socat.log" "lossy link: deaf" "$bridge_pid" || \
		fail "the bridge never cut the pass"
	if grep -Fq 'FTP get 16 /build/down.bin -> /build/down-copy.bin: PASS' \
		"$work_dir/node19.log"; then
		fail "the download finished although the link was cut"
	fi

	# What the interrupted pass left behind is the whole point.
	wait_for_output "$work_dir/socat.log" "lossy link: forwarding again" \
		"$bridge_pid" || fail "the bridge never came back"
	printf '%s\n' 'ftp ls 19 /build' >&4
	wait_for_output "$work_dir/node19.log" "down-copy.bin.part" "$node19_pid" || \
		fail "the interrupted download kept nothing to continue from"
	grep -Fq "down-copy.bin.part.map" "$work_dir/node19.log" || \
		fail "the partial was left without the note that names its file"

	printf '%s\n' 'ftp get 16 /build/down.bin /build/down-copy.bin' \
		'ftp ls 19 /build' >&4
	wait_for_output "$work_dir/node19.log" \
		'FTP get 16 /build/down.bin -> /build/down-copy.bin: PASS' \
		"$node19_pid" || fail "the second pass did not finish the download"
	grep -Fq 'FTP: continuing' "$work_dir/node19.log" || \
		fail "the second pass started the file again instead of continuing"
	resumed_at="$(tr -d '\r' <"$work_dir/node19.log" |
		sed -n 's/.*FTP: continuing .* at \([0-9]*\) of .*/\1/p' | head -1)"
	[[ -n "$resumed_at" && "$resumed_at" -gt 0 ]] || \
		fail "the second pass continued from nowhere"
	grep -Fq "crc32: $source_crc" "$work_dir/node19.log" || \
		fail "the stitched file is not the one node 16 holds"

	# Nothing of the transfer may outlive it.
	after_last_listing="$(tr -d '\r' <"$work_dir/node19.log" |
		awk '/^FTP ls 19 \/build/ { buffer = "" ; next } { buffer = buffer $0 "\n" }
		     END { printf "%s", buffer }')"
	if grep -Fq '.part' <<<"$after_last_listing"; then
		fail "the partial outlived the download it belonged to"
	fi

	cat "$work_dir/node19.log"
	echo "K-GROUND FTP RESULT: PASS crc32=$source_crc bytes=$bytes interrupted=yes resumed_at=$resumed_at"
	exit 0
fi

# The operator node drives the whole round trip.
printf '%s\n' \
	"ftp generate /build/test.txt $bytes" \
	'ftp mkdir 16 /uplink' \
	'ftp put 16 /build/test.txt /uplink/test.txt' \
	'ftp stat 16 /uplink/test.txt' \
	'ftp ls 16 /uplink' \
	'ftp get 16 /uplink/test.txt /build/test-returned.txt' \
	'ftp verify /build/test.txt /build/test-returned.txt' \
	'ftp get 16 /uplink/missing.txt /build/missing.txt' \
	>&4

wait_for_output "$work_dir/node19.log" \
	"FTP verify /build/test.txt /build/test-returned.txt: PASS" \
	"$node19_pid" || fail "the uploaded and downloaded copies did not match"
wait_for_output "$work_dir/node19.log" \
	"FTP get 16 /uplink/missing.txt: not found" \
	"$node19_pid" || fail "a missing remote file was not reported as not found"

# The receiving node sees the committed file in its own FTP root.
printf '%s\n' 'ftp ls 16 /uplink' 'ftp stat 16 /uplink/test.txt' >&3
wait_for_output "$work_dir/node16.log" "entries: 1" \
	"$node16_pid" || fail "node 16 does not list the received file locally"

node19_expected=(
	'FTP generate /build/test.txt: PASS'
	'FTP mkdir 16 /uplink: PASS'
	'FTP put 16 /build/test.txt -> /uplink/test.txt: PASS'
	'FTP stat 16 /uplink/test.txt'
	'type: file'
	"bytes: $bytes"
	"$(printf 'file %10d test.txt' "$bytes")"
	'FTP get 16 /uplink/test.txt -> /build/test-returned.txt: PASS'
)
node16_expected=(
	'FTP stat 16 /uplink/test.txt'
	"$(printf 'file %10d test.txt' "$bytes")"
)

for expected in "${node19_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node19.log" || \
		fail "node 19 output is missing: $expected"
done
for expected in "${node16_expected[@]}"; do
	grep -Fq "$expected" "$work_dir/node16.log" || \
		fail "node 16 output is missing: $expected"
done

# The same CRC must appear on both nodes and on both local copies.
uploaded_crc="$(tr -d '\r' <"$work_dir/node19.log" |
	awk '/FTP generate \/build\/test.txt: PASS/ { found = 1 }
	     found && /^crc32: / { print $2; exit }')"
[[ -n "$uploaded_crc" ]] || fail "the generated file did not report a CRC"
grep -Fq "crc32: $uploaded_crc" "$work_dir/node16.log" || \
	fail "node 16 reports a different CRC than node 19 generated"

resent=0
dropped=0
if [[ "$lossy_link" -eq 1 ]]; then
	read -r resent dropped < <(python3 - "$work_dir/ftp-stats.json" <<'PYSTATS'
import json, sys
stats = json.load(open(sys.argv[1]))
print(stats['resent'], stats['dropped'])
PYSTATS
)
	[[ "$resent" -gt 0 && "$dropped" -gt 0 ]] || fail "loss did not cause a retransmission"
fi

cat "$work_dir/node19.log"
cat "$work_dir/node16.log"
echo "K-GROUND FTP RESULT: PASS crc32=$uploaded_crc bytes=$bytes lossy=$([[ "$lossy_link" -eq 1 ]] && echo yes || echo no) dropped=$dropped resent=$resent"
