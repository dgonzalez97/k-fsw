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

# shellcheck source=tests/hil/shapes.sh
source "$KFSW_HIL_DIR/shapes.sh"

selection=()

# A bare name is a bench shape. Anything starting with a dash is passed to
# Robot as before, which is what CI and the documented one-off runs do.
if [[ $# -gt 0 && "$1" != -* ]]; then
	shape="$1"
	shift
	if ! kfsw_shape_needs "$shape" >/dev/null; then
		echo "ERROR: $shape is not a bench shape" >&2
		echo "Known: ${KFSW_SHAPES[*]}" >&2
		exit 2
	fi
	read -r -a selection <<<"$(kfsw_shape_selection "$shape")"
	echo "ROBOT: shape: $shape"
	echo "ROBOT: needs: $(kfsw_shape_needs "$shape")"
	if [[ -x "$KFSW_HIL_DIR/preflight.sh" ]]; then
		"$KFSW_HIL_DIR/preflight.sh" "$shape" || exit 1
	fi
elif [[ $# -eq 0 ]]; then
	# Running every suite on a desk that cannot serve them wastes bench time,
	# so say what this one can do instead of trying everything.
	echo "Give a bench shape, or Robot options to pass through."
	echo
	for shape in "${KFSW_SHAPES[@]}"; do
		printf '  %-12s %s\n' "$shape" "$(kfsw_shape_needs "$shape")"
	done
	echo
	echo "What this bench can run right now:"
	echo
	exec "$KFSW_HIL_DIR/preflight.sh"
fi

echo "ROBOT: output: $output_dir"
echo "ROBOT: debug UART: ${KFSW_DEBUG_SERIAL:-not set}"
echo "ROBOT: FTDI UART: ${KFSW_FTDI_DEVICE:-auto-discover}"

exec "$robot_command" --outputdir "$output_dir" \
	${selection[@]+"${selection[@]}"} "$@" "$KFSW_HIL_DIR"
