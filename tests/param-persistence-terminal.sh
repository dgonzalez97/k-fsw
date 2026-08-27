#!/usr/bin/env bash
set -Eeuo pipefail

source "$(dirname "$0")/../tools/_common.sh" linux

executable="$KFSW_BUILD_DIR/zephyr/zephyr.exe"
work_dir="$(mktemp -d /tmp/kfsw-param-terminal.XXXXXX)"
flash_image="$work_dir/flash.bin"
input_fifo="$work_dir/input"
output_log="$work_dir/output.log"
simulator_pid=""
relay_pid=""

cleanup()
{
	[[ -n "$simulator_pid" ]] && kill "$simulator_pid" 2>/dev/null || true
	[[ -n "$relay_pid" ]] && kill "$relay_pid" 2>/dev/null || true
	wait 2>/dev/null || true
	exec 3>&- 2>/dev/null || true
	rm -rf -- "$work_dir"
}

fail()
{
	echo "KFSW PARAM TERMINAL: FAIL ($1)"
	exit 1
}

ready_count()
{
	grep -Fc '@READY ' "$output_log" 2>/dev/null || true
}

start_simulator()
{
	local previous_ready_count="$1"

	"$executable" --uart_stdinout --no-color -flash="$flash_image" \
		<&3 >>"$output_log" 2>&1 &
	simulator_pid=$!

	for _ in {1..200}; do
		if [[ "$(ready_count)" -gt "$previous_ready_count" ]]; then
			return 0
		fi
		kill -0 "$simulator_pid" 2>/dev/null || return 1
		sleep 0.05
	done
	return 1
}

restart_simulator()
{
	local previous_ready_count

	previous_ready_count="$(ready_count)"
	kill "$simulator_pid" 2>/dev/null || true
	wait "$simulator_pid" 2>/dev/null || true
	simulator_pid=""
	start_simulator "$previous_ready_count" || fail "restart did not reach readiness"
	echo "KFSW PARAM TERMINAL: RESTARTED"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

[[ -x "$executable" ]] || fail "KFSW-Linux is not built"
mkfifo "$input_fifo"
exec 3<>"$input_fifo"
touch "$output_log"
tail -n +1 -F "$output_log" &
relay_pid=$!

start_simulator 0 || fail "initial boot did not reach readiness"
echo "KFSW PARAM TERMINAL: READY"

while IFS= read -r command; do
	if [[ "$command" == __KFSW_RESTART__ ]]; then
		restart_simulator
	else
		printf '%s\n' "$command" >&3
	fi
done
