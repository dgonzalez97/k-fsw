#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
work_dir="$(mktemp -d /tmp/kfsw-build-options.XXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT

# Exercise the real build/common scripts without touching workspace outputs.
mkdir -p "$work_dir/bin"
cat > "$work_dir/bin/west" <<'SH'
#!/usr/bin/env bash
printf '%s\0' "$@" > "$BUILD_OPTIONS_CAPTURE"
exit "${BUILD_OPTIONS_WEST_STATUS:-0}"
SH
chmod +x "$work_dir/bin/west"
export PATH="$work_dir/bin:$PATH"
export KFSW_VENV_DIR="$work_dir/no-venv"
export KFSW_OUTPUT_ROOT="$work_dir/output with spaces"
export BUILD_OPTIONS_CAPTURE="$work_dir/args"
unset KFSW_EXTRA_CONF_FILE KFSW_MCUBOOT_KEY KFSW_BUILD_DIR KFSW_CMAKE_ARGS
unset KFSW_SYSBUILD KFSW_CONF_FILE KFSW_EXTRA_DTC_OVERLAY_FILE
unset KFSW_MCUBOOT_DTC_OVERLAY_FILE
export KFSW_PRISTINE=auto

run_build() {
	rm -f "$BUILD_OPTIONS_CAPTURE"
	bash "$repo_dir/tools/build.sh" "$@" > "$work_dir/log" 2>&1
}

check_args() {
	python3 - "$BUILD_OPTIONS_CAPTURE" "$repo_dir" "$KFSW_OUTPUT_ROOT" "$@" <<'PY'
from pathlib import Path
import sys
capture, repo, output, target, board, *profile = sys.argv[1:]
actual = Path(capture).read_bytes().split(b'\0')[:-1]
expected = ['build', '-p', 'auto', '-b', board, f'{repo}/app',
            '-d', f'{output}/{target}']
if profile:
    expected += ['--extra-conf', profile[0]]
expected += ['--', f'-DUSER_CACHE_DIR={output}/cache']
assert actual == [value.encode() for value in expected], (actual, expected)
PY
}

run_build
check_args nucleo_l496zg nucleo_l496zg
run_build nucleo_l496zg
check_args nucleo_l496zg nucleo_l496zg
run_build linux
check_args linux native_sim/native/64
profile="$repo_dir/config/profiles/log-semihost.conf"
run_build nucleo_l496zg --semihost
check_args nucleo_l496zg nucleo_l496zg "$profile"
run_build --semihost
check_args nucleo_l496zg nucleo_l496zg "$profile"
export KFSW_EXTRA_CONF_FILE="$work_dir/first profile.conf;$work_dir/second.conf"
run_build nucleo_l496zg
check_args nucleo_l496zg nucleo_l496zg "$KFSW_EXTRA_CONF_FILE"
run_build --semihost nucleo_l496zg
check_args nucleo_l496zg nucleo_l496zg "$KFSW_EXTRA_CONF_FILE;$profile"
unset KFSW_EXTRA_CONF_FILE
for target in nucleo_f746zg frdm_k64f; do
	run_build "$target" --semihost
	board="$target"
	[[ "$target" != frdm_k64f ]] || board=frdm_k64f/mk64f12
	check_args "$target" "$board" "$profile"
done
run_build --help
[[ ! -e "$BUILD_OPTIONS_CAPTURE" ]]
grep -q -- '--semihost' "$work_dir/log"

expect_error() {
	local expected_status="$1" message="$2" status=0
	shift 2
	run_build "$@" || status=$?
	[[ "$status" == "$expected_status" ]]
	[[ ! -e "$BUILD_OPTIONS_CAPTURE" ]]
	grep -F -- "$message" "$work_dir/log" > /dev/null
}
expect_error 2 'unknown option' --bogus
expect_error 2 'expected one target name' nucleo_l496zg linux
expect_error 2 'expected one target name' ''
expect_error 2 'expected one target name' ../linux
expect_error 2 'duplicate --semihost' --semihost --semihost
expect_error 2 '--help must be used alone' --help linux
expect_error 1 'unsupported K-FSW target' nonexistent
for target in linux rpi_pico_w; do
	expect_error 2 '--semihost is unsupported' "$target" --semihost
done
KFSW_PRISTINE=invalid expect_error 1 'KFSW_PRISTINE must be' nucleo_l496zg
status=0
BUILD_OPTIONS_WEST_STATUS=7 run_build nucleo_l496zg --semihost || status=$?
[[ "$status" == 7 ]]
echo 'BUILD OPTIONS RESULT: PASS'
