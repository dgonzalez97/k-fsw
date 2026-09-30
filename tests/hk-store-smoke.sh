#!/usr/bin/env bash
# Checks the housekeeping sample file store: a too-short interval is refused,
# an accepted one creates a file, the samples can be read back and extracted as
# a dataset, the file can be read but not written over file transfer, and
# redefining the report removes it.

set -euo pipefail

KFSW_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_ROOT="$(dirname "$KFSW_REPO_DIR")"

executable="$KFSW_ROOT/build/linux/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-hk-store.XXXXXX)"
failures=0

trap 'rm -rf -- "$work_dir"' EXIT

expect()
{
	if grep -aqF "$1" "$work_dir/out.log"; then
		printf '  [ok]   %s\n' "${2:-$1}"
	else
		printf '  [FAIL] %s\n' "${2:-$1}" >&2
		failures=$((failures + 1))
	fi
}

[[ -x "$executable" ]] || {
	printf 'no hosted image; build it with CONFIG_KFSW_HK_STORE=y\n' >&2
	exit 2
}

# The period is short and the interval is the floor, so the run stays short
# while still crossing a write boundary.
{
	printf 'hk define 0 1:0 3:0\n'
	printf 'hk period 0 1000\n'
	printf 'hk store 0 500\n'
	printf 'hk store 0 10000\n'
	sleep 22
	printf 'hk stored 0\n'
	printf 'hk extract 0 /kfsw/hk/dataset.bin\n'
	printf 'hk stored 0 from=1 to=1\n'
	printf 'hk stored 0 from=900\n'
	printf 'ftp ls 1 /hk\n'
	printf 'ftp mkdir 1 /hk/evil\n'
	printf 'hk define 0 1:0\n'
	printf 'ftp ls 1 /hk\n'
	sleep 4
} | timeout 60 "$executable" --uart_stdinout --no-color \
	-flash="$work_dir/flash.bin" -flash_erase -flash_rm --stop_at=40 \
	>"$work_dir/out.log" 2>&1 || true

sed -i 's/\x1b\[[0-9;]*m//g' "$work_dir/out.log"
tr -d '\r' <"$work_dir/out.log" >"$work_dir/clean.log"
mv "$work_dir/clean.log" "$work_dir/out.log"

echo "HK STORE SMOKE"

# -34 is -ERANGE: below CONFIG_KFSW_HK_STORE_FLOOR_MS.
expect 'store for report 0: -34' 'an interval below the floor is refused'
expect 'report 0 stores every 10000 ms' 'an interval at the floor is accepted'
expect 'report0.bin' 'the samples reach a file'
expect 'held: seq' 'the stored window can be read back'
expect 'Extracted' 'the selected samples become a dataset file'
expect 'dataset.bin' 'the dataset is where file transfer can fetch it'
expect 'selected: 1' 'a one-sequence window selects one sample'
expect 'selected: 0' 'a window past the end selects nothing'
# -30 is -EROFS: /hk is served read-only.
expect 'path=/hk/evil: FAIL (-30)' 'the ground cannot write under /hk'

if [[ "$(grep -ac 'report0.bin' "$work_dir/out.log")" -ge 2 ]]; then
	printf '  [FAIL] the file outlived its definition\n' >&2
	failures=$((failures + 1))
else
	printf '  [ok]   a redefinition takes the file away\n'
fi

if [[ "$failures" -eq 0 ]]; then
	echo "HK STORE SMOKE RESULT: PASS"
else
	echo "HK STORE SMOKE RESULT: FAIL ($failures)"
	sed -n '1,60p' "$work_dir/out.log" >&2
	exit 1
fi
