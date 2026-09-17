#!/usr/bin/env bash
# The ground watchdog on the Linux target: contact holds the countdown open,
# and silence past the timeout resets the node. A reset on native_sim stops
# the process, which is what this watches for.
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
timeout_s=3

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

if [[ ! -x "$executable" ]]; then
	echo "GNDWDT SMOKE: building the node"
	KFSW_BUILD_DIR="$build_dir" \
		KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/linux-gndwdt.conf" \
		"$KFSW_REPO_DIR/tools/build.sh" linux
fi

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

# Now go quiet. The node has to notice and reset itself. A reset on native_sim
# re-runs the image in place rather than ending the process, so the evidence is
# a second boot reporting a software reset.
wait_for_output "Ground watchdog: no contact for $timeout_s s; resetting" || \
	fail "the ground watchdog did not report the timeout"

wait_for_output "reset_cause=software" || \
	fail "the node did not come back from a software reset"

boots="$(grep -c '@BOOT ' "$work_dir/node.log")"
(( boots >= 2 )) || fail "expected a second boot after the reset, saw $boots"

cat "$work_dir/node.log"
echo "GNDWDT RESULT: PASS"
