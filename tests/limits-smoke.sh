#!/usr/bin/env bash
# A limit is crossed, dwells, fires once, and re-arms when the value comes back.
#
# The unit suite checks the parser line by line. This drives the operator's
# path: a rule file in the node's filesystem, read at boot, crossed on purpose,
# and the released command waiting in the time-tagged queue. The watched
# parameter is boot_delay_ms because it only takes effect on the next boot, so
# moving it proves the rule without changing what the node is doing.
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
tests_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
python="$KFSW_ROOT/.venv/bin/python"
work_dir="$(mktemp -d /tmp/kfsw-limits.XXXXXX)"
flash_image="$work_dir/limits-flash.bin"

cleanup() {
	rm -rf -- "$work_dir"
}

fail() {
	echo "LIMITS RESULT: FAIL"
	echo "  $1"

	for log_file in "$work_dir"/*.log; do
		[[ -s "$log_file" ]] || continue
		echo "--- $(basename "$log_file") ---"
		cat "$log_file"
	done
	exit 1
}

expect() {
	grep -Fq "$2" "$1" || fail "$3"
}

count_of() {
	grep -Fc "$2" "$1" || true
}

trap cleanup EXIT

[[ -x "$python" ]] || python="python3"
if [[ ! -x "$executable" ]]; then
	echo "LIMITS: building KFSW-Linux"
	"$KFSW_ROOT/k-fsw/tools/build.sh" linux
fi
grep -Fq 'CONFIG_KFSW_LIMITS=y' "$KFSW_BUILD_DIR/zephyr/.config" ||
	{ echo "LIMITS RESULT: NOT RUN - this image has no limits service"; exit 0; }

# Both files go in before the node boots, so the rule set is the one the
# service found on its own rather than one a test loaded for it.
"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$flash_image" --offset 0xfc000 --size 0x40000 \
	"$tests_dir/limits/boot-delay.txt" /ftp/limits/limits.txt >/dev/null
"$python" "$KFSW_REPO_DIR/tools/ground/stage-file.py" \
	--flash "$flash_image" --offset 0xfc000 --size 0x40000 \
	"$tests_dir/limits/broken.txt" /ftp/limits/broken.txt >/dev/null

# Real time has to pass between the two crossings: a release that is still
# queued stands for the next one, which is how the service avoids queueing the
# same action twice, so a second fire needs the first entry to have run.
{
	printf '%s\n' \
		'limits status' \
		'limits list' \
		'limits sweep' \
		'param set boot_delay_ms 2000' \
		'limits sweep' \
		'limits status' \
		'limits sweep' \
		'limits status' \
		'fbo sched list' \
		'limits sweep' \
		'limits status' \
		'param set boot_delay_ms 0' \
		'limits sweep' \
		'limits list'
	sleep 4
	printf '%s\n' \
		'param set boot_delay_ms 2000' \
		'limits sweep' \
		'limits sweep' \
		'limits status' \
		'limits load broken.txt' \
		'limits status' \
		'limits list'
	sleep 2
} | "$executable" --uart_stdinout --stop_at=20.0 --no-color \
	-flash="$flash_image" >"$work_dir/out.log" 2>&1 || true
sed -i 's/\x1b\[[0-9;]*m//g; s/\r//g' "$work_dir/out.log"

expect "$work_dir/out.log" 'rules loaded from limits.txt' \
	"the rule file was not read at boot"
grep -Eq '^ *0 +boot_delay_ms +> +1000 +2 +0 +armed +noop' "$work_dir/out.log" ||
	fail "the rule in force was not listed as armed"

# Dwell: the first sweep past the bound only counts, it does not release.
[[ "$(count_of "$work_dir/out.log" 'last_fired: none')" -ge 2 ]] ||
	fail "a single sweep past the bound released the action"

expect "$work_dir/out.log" 'last_fired: rule 0' "the crossed rule never fired"
expect "$work_dir/out.log" 'noop' "the released command is not in the queue"

# Latch and re-arm: two crossings, two fires, and nothing in between.
[[ "$(count_of "$work_dir/out.log" 'fires: 1')" -ge 2 ]] ||
	fail "the rule did not latch after firing"
expect "$work_dir/out.log" 'fires: 2' "the rule did not re-arm when the value came back"
[[ "$(count_of "$work_dir/out.log" 'fires: 3')" -eq 0 ]] ||
	fail "the rule fired more than once per crossing"
[[ "$(grep -Ec '^ *0 +boot_delay_ms .* armed +noop' "$work_dir/out.log")" -ge 2 ]] ||
	fail "the rule never went back to armed"
grep -Eq '^ *0 +boot_delay_ms .* fired +noop' "$work_dir/out.log" ||
	fail "a latched rule is not listed as fired"

# A file with one bad line is refused whole, and the rules in force stay.
expect "$work_dir/out.log" 'parse_errors: 1' "the broken file was accepted"
expect "$work_dir/out.log" 'last_error: line 3' "the refusal did not name the bad line"
[[ "$(count_of "$work_dir/out.log" 'rules: 1')" -ge 3 ]] ||
	fail "a refused file changed the rules in force"

echo "LIMITS RESULT: PASS one crossing, one release, re-armed and the broken file refused"
