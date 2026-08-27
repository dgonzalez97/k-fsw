#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-storage-smoke.XXXXXX)"
flash_image="$work_dir/flash.bin"
write_log="$work_dir/write.log"
read_log="$work_dir/read.log"
persistent_value="kfsw-cross-process-value"

cleanup()
{
	rm -rf -- "$work_dir"
}

fail()
{
	echo "STORAGE RESULT: FAIL"
	echo "  $1"

	for log_file in "$write_log" "$read_log"; do
		if [[ -s "$log_file" ]]; then
			echo "--- $(basename "$log_file") ---"
			cat "$log_file"
		fi
	done
	exit 1
}

trap cleanup EXIT

if [[ ! -x "$executable" ]]; then
	echo "STORAGE: building KFSW-Linux"
	"$KFSW_ROOT/k-fsw/tools/build.sh" linux
fi

printf '%s\n' \
	'storage info' \
	'storage test' \
	"storage test write $persistent_value" |
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		-flash="$flash_image" >"$write_log" 2>&1

grep -Fq 'ready: yes' "$write_log" || fail "storage did not mount"
grep -Eq 'total_bytes: [1-9][0-9]*' "$write_log" || \
	fail "total capacity was not reported"
grep -Eq 'free_bytes: [1-9][0-9]*' "$write_log" || \
	fail "free capacity was not reported"
grep -Fq 'Storage test: PASS' "$write_log" || \
	fail "create/write/read/overwrite/delete test failed"
grep -Fq 'Storage persistence write: PASS' "$write_log" || \
	fail "persistence write failed"

printf '%s\n' \
	'storage info' \
	"storage test read $persistent_value" |
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		-flash="$flash_image" >"$read_log" 2>&1

grep -Fq 'ready: yes' "$read_log" || \
	fail "storage did not remount in execution B"
grep -Fq 'Storage persistence read: PASS' "$read_log" || \
	fail "value did not survive a separate execution"

cat "$write_log"
cat "$read_log"
echo "STORAGE RESULT: PASS"
