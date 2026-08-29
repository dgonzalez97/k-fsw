#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PARAM_LOCAL_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_PARAM_LOCAL_TOOL")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
param_local_build_dir="${KFSW_PARAM_LOCAL_BUILD_DIR:-$KFSW_WORKSPACE_ROOT/build/tests/param-local}"
executable="$param_local_build_dir/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-param-local.XXXXXX)"
capture_file="$work_dir/output.log"

cleanup()
{
	rm -rf -- "$work_dir"
}

fail()
{
	echo "PARAM LOCAL RESULT: FAIL"
	echo "  $1"
	[[ -s "$capture_file" ]] && cat "$capture_file"
	exit 1
}

expect()
{
	local expected="$1"
	local message="$2"

	grep -Fq "$expected" "$capture_file" || fail "$message"
}

trap cleanup EXIT

KFSW_BUILD_DIR="$param_local_build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/linux-param-local.conf" \
	KFSW_PRISTINE=always \
	"$KFSW_REPO_DIR/tools/build.sh" linux

grep -qx 'CONFIG_KFSW_PARAM=y' "$param_local_build_dir/zephyr/.config" || \
	fail "CONFIG_KFSW_PARAM is not enabled"
if grep -q '^CONFIG_KFSW_PARAM_CSP=' "$param_local_build_dir/zephyr/.config"; then
	fail "CONFIG_KFSW_PARAM_CSP is enabled"
fi
if grep -q '^CONFIG_KFSW_CSP=' "$param_local_build_dir/zephyr/.config"; then
	fail "CONFIG_KFSW_CSP is enabled"
fi

if find "$param_local_build_dir" -name 'libcsp.a' -print -quit | grep -q .; then
	fail "the CSP-disabled composition built libcsp"
fi

printf '%s\n' \
	'param list' \
	'param get test_u32' \
	'param set test_u32 1234' \
	'param get test_u32' \
	'param set node_id 2' \
	'param set log_level 5' \
	'param set test_u32 4294967296' \
	'param set test_i32 -2147483649' \
	'param set test_float 2.25' \
	'param get test_float' \
	'param save' \
	'param defaults' \
	'param get test_u32' \
	'param load' \
	'param get test_u32' \
	'param clear' |
	"$executable" --uart_stdinout --stop_at=1.0 --no-color \
		-flash="$work_dir/flash.bin" >"$capture_file" 2>&1

expect 'Parameter table initialized' "the local table did not initialize"
expect '0:0 node_id' "param list omitted node_id"
expect '0:4 test_float' "param list omitted test_float"
expect 'test_u32 = 42' "the compiled unsigned default was not visible"
expect 'test_u32 = 1234' "local unsigned set/get failed"
expect "set: parameter 'node_id' is read-only" "read-only rejection failed"
expect "set: parameter 'log_level' failed (-34)" "log-level range rejection failed"
expect "set: invalid u32 value '4294967296'" "unsigned range rejection failed"
expect "set: invalid i32 value '-2147483649'" "signed range rejection failed"
expect 'test_float = 2.25' "floating-point set/get failed"
expect 'Parameter snapshot save: PASS' "local persistence save failed"
expect 'Parameter defaults: PASS (saved snapshot unchanged)' "default restore failed"
expect 'Parameter snapshot load: PASS' "local persistence load failed"
expect 'Parameter snapshot clear: PASS (RAM unchanged)' "local persistence clear failed"

if grep -Fq 'CSP initialized' "$capture_file"; then
	fail "the CSP-disabled composition initialized CSP"
fi
if grep -Fq 'Parameter server started' "$capture_file"; then
	fail "the local-only composition started the CSP adapter"
fi

cat "$capture_file"
echo "PARAM LOCAL RESULT: PASS"
