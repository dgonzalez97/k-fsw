#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ROBOT_RUNNER="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_HIL_DIR="$(dirname "$KFSW_ROBOT_RUNNER")"
KFSW_TESTS_DIR="$(dirname "$KFSW_HIL_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_TERMINAL_RUNNER="$KFSW_TESTS_DIR/platform/robot-terminal-runner"

robot_command=""

if [[ -x "$KFSW_WORKSPACE_ROOT/.venv/bin/robot" ]]; then
	robot_command="$KFSW_WORKSPACE_ROOT/.venv/bin/robot"
elif command -v robot >/dev/null 2>&1; then
	robot_command="$(command -v robot)"
else
	echo "ERROR: Robot Framework is required"
	echo "Install: pip install -r $KFSW_HIL_DIR/requirements.txt"
	exit 1
fi

if [[ ! -f "$KFSW_TERMINAL_RUNNER/src/tmux_interaction_lib.py" ]]; then
	echo "ERROR: robot-terminal-runner submodule is not initialized"
	echo "Run: git -C $KFSW_REPO_DIR submodule update --init --recursive"
	exit 1
fi

export KFSW_REPO_DIR
export PYTHONPATH="$KFSW_TERMINAL_RUNNER/src${PYTHONPATH:+:$PYTHONPATH}"

output_dir="${KFSW_ROBOT_OUT_DIR:-$KFSW_WORKSPACE_ROOT/build/robot}"

echo "ROBOT HIL: output: $output_dir"
echo "ROBOT HIL: debug UART: ${KFSW_DEBUG_SERIAL:-/dev/ttyACM0}"
echo "ROBOT HIL: FTDI UART: ${KFSW_FTDI_DEVICE:-auto-discover}"

exec "$robot_command" --outputdir "$output_dir" "$@" "$KFSW_HIL_DIR"
