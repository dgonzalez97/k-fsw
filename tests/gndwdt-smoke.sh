#!/usr/bin/env bash
# Check the five-day floor and contact reporting on the Linux target.
# Unit tests advance a fake clock to cover expiry without waiting five days.
set -Eeuo pipefail

KFSW_GNDWDT_TEST="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_GNDWDT_TEST")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

build_dir="$KFSW_WORKSPACE_ROOT/build/tests/linux-gndwdt"
executable="$build_dir/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-gndwdt-smoke.XXXXXX)"
node_pid=""

# Matches CONFIG_KFSW_GNDWDT_TIMEOUT_S in tests/config/linux-gndwdt.conf.
timeout_s=432000

cleanup()
{
	[[ -n "$node_pid" ]] && kill "$node_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- || true
	rm -rf -- "$work_dir"
}

fail()
{
	echo "GNDWDT RESULT: FAIL"
	echo "  $1"
	if [[ -s "$work_dir/node.log" ]]; then
		echo "--- node.log ---"
		sed -n '1,200p' "$work_dir/node.log"
	fi
	exit 1
}

wait_for_output()
{
	local expected="$1"
	local deadline=$((SECONDS + 20))

	while (( SECONDS < deadline )); do
		grep -Fq "$expected" "$work_dir/node.log" 2>/dev/null && return 0
		sleep 0.05
	done

	return 1
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "GNDWDT SMOKE: building the node"
KFSW_BUILD_DIR="$build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/linux-gndwdt.conf" \
	"$KFSW_REPO_DIR/tools/build.sh" linux

mkfifo "$work_dir/node.in"
exec 3<>"$work_dir/node.in"

"$executable" --uart_stdinout --device_id=1 --no-color \
	-flash="$work_dir/flash.bin" <&3 >"$work_dir/node.log" 2>&1 &
node_pid=$!

wait_for_output "@READY " || fail "the node did not report readiness"

printf '%s\n' 'gndwdt show' >&3
wait_for_output "timeout_s: $timeout_s" || \
	fail "the ground watchdog did not report its compiled timeout"
wait_for_output "state: armed" || fail "the ground watchdog did not start armed"

# Contact more often than the timeout: the node has to stay up.
for _ in {1..6}; do
	printf '%s\n' 'gndwdt contact' >&3
	sleep 1
done

[[ "$(grep -c '@BOOT ' "$work_dir/node.log")" -eq 1 ]] || \
	fail "the node reset while contact was being recorded"

printf '%s\n' 'gndwdt show' >&3
wait_for_output "contacts: 6" || fail "recorded contact was not counted"

printf '%s\n' 'gndwdt timeout 431999' >&3
wait_for_output 'Timeout refused:' || fail "a timeout below five days was accepted"

cat "$work_dir/node.log"
echo "GNDWDT RESULT: PASS"
