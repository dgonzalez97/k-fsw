#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_BOTON_TEST_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_BOTON_TEST_TOOL")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
disabled_build_dir="${KFSW_BOTON_TEST_DISABLED_BUILD_DIR:-$KFSW_WORKSPACE_ROOT/build/tests/boton-test-disabled}"
enabled_build_dir="${KFSW_BOTON_TEST_ENABLED_BUILD_DIR:-$KFSW_WORKSPACE_ROOT/build/tests/boton-test-enabled}"
work_dir="$(mktemp -d /tmp/kfsw-boton-test.XXXXXX)"
disabled_log="$work_dir/disabled.log"
enabled_log="$work_dir/enabled.log"

cleanup()
{
	rm -rf -- "$work_dir"
}

fail()
{
	echo "BOTON TEST RESULT: FAIL"
	echo "  $1"

	for log_file in "$disabled_log" "$enabled_log"; do
		if [[ -s "$log_file" ]]; then
			echo "--- $(basename "$log_file") ---"
			cat "$log_file"
		fi
	done
	exit 1
}

expect()
{
	local log_file="$1"
	local expected="$2"
	local message="$3"

	grep -Fq "$expected" "$log_file" || fail "$message"
}

run_image()
{
	local executable="$1"
	local flash_image="$2"
	local output_log="$3"

	printf '%s\n' \
		'param list' \
		'param get boton_test_press_count' \
		'param get boton_test_last_press_s' \
		'param set boton_test_press_count 100' \
		'param set boton_test_last_press_s 100' \
		'param get boton_test_press_count' \
		'param get boton_test_last_press_s' |
		"$executable" --uart_stdinout --stop_at=1.0 --no-color \
			-flash="$flash_image" -flash_erase -flash_rm \
			>"$output_log" 2>&1
}

trap cleanup EXIT

KFSW_BUILD_DIR="$disabled_build_dir" \
	KFSW_PRISTINE=always \
	"$KFSW_REPO_DIR/tools/build.sh" linux

if grep -q '^CONFIG_KFSW_BOTON_TEST=' "$disabled_build_dir/zephyr/.config"; then
	fail "the default Linux composition enabled boton_test"
fi

run_image "$disabled_build_dir/zephyr/zephyr.exe" \
	"$work_dir/disabled-flash.bin" "$disabled_log"

expect "$disabled_log" \
	"get: parameter 'boton_test_press_count' not found" \
	"the disabled composition unexpectedly exposed press_count"
expect "$disabled_log" \
	"get: parameter 'boton_test_last_press_s' not found" \
	"the disabled composition unexpectedly exposed last_press_s"
if grep -Eq '^[0-9]+:[0-9]+ +boton_test_' "$disabled_log"; then
	fail "the disabled parameter list contains boton_test definitions"
fi

KFSW_BUILD_DIR="$enabled_build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/boton-test-linux.conf" \
	KFSW_PRISTINE=always \
	"$KFSW_REPO_DIR/tools/build.sh" linux

grep -qx 'CONFIG_KFSW_BOTON_TEST=y' "$enabled_build_dir/zephyr/.config" || \
	fail "the test composition did not enable boton_test"
if grep -q '^CONFIG_KFSW_BOTON_TEST_GPIO=' "$enabled_build_dir/zephyr/.config"; then
	fail "the software composition enabled physical GPIO handling"
fi

run_image "$enabled_build_dir/zephyr/zephyr.exe" \
	"$work_dir/enabled-flash.bin" "$enabled_log"

expect "$enabled_log" 'boton_test initialized' \
	"the enabled module did not initialize"
if ! grep -Eq '^0:[0-9]+ +boton_test_press_count +u32 +ro' "$enabled_log"; then
	fail "the enabled parameter list omitted read-only press_count"
fi
if ! grep -Eq '^0:[0-9]+ +boton_test_last_press_s +u32 +ro' "$enabled_log"; then
	fail "the enabled parameter list omitted read-only last_press_s"
fi
expect "$enabled_log" 'boton_test_press_count = 0' \
	"the live press_count did not start at zero"
expect "$enabled_log" 'boton_test_last_press_s = 0' \
	"the live last_press_s did not start at zero"
expect "$enabled_log" \
	"set: parameter 'boton_test_press_count' is read-only or service is not ready" \
	"press_count was not read-only"
expect "$enabled_log" \
	"set: parameter 'boton_test_last_press_s' is read-only or service is not ready" \
	"last_press_s was not read-only"

if [[ "$(grep -Fc 'boton_test_press_count = 0' "$enabled_log")" -lt 2 ]]; then
	fail "the rejected press_count write changed owner state"
fi
if [[ "$(grep -Fc 'boton_test_last_press_s = 0' "$enabled_log")" -lt 2 ]]; then
	fail "the rejected last_press_s write changed owner state"
fi

cat "$disabled_log"
cat "$enabled_log"
echo "BOTON TEST RESULT: PASS"
