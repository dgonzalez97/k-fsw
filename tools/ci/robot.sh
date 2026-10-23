#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ROBOT_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ROBOT_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_ROBOT_RUNNER="$KFSW_REPO_DIR/tests/hil/run.sh"

robot_python="$KFSW_WORKSPACE_ROOT/.venv/bin/python"
[[ -x "$robot_python" ]] || robot_python=python3

host_tools="${CARGO_TARGET_DIR:-${KFSW_CSP_TOOLS_DIR:-$KFSW_WORKSPACE_ROOT/tools/kfsw-csp-tools}/target}/release"
if [[ "${KFSW_HOST_TOOLS:-0}" == 1 ]]; then
	if ! "$KFSW_REPO_DIR/tools/host-tools.sh"; then
		echo "HOST TOOLS: unavailable; install Rust and run tools/host-tools.sh; dependent cases will skip"
	fi
fi
if [[ -x "$host_tools/csp-kiss" && -x "$host_tools/csp-iperf" ]]; then
	export KFSW_CSP_TOOLS_TEST_BINARY="${KFSW_CSP_TOOLS_TEST_BINARY:-$host_tools/csp-kiss}"
	export KFSW_CSP_IPERF_TEST_BINARY="${KFSW_CSP_IPERF_TEST_BINARY:-$host_tools/csp-iperf}"
fi

# Optional host tools and vcan remain opt-in. Acceptance requires all cases.
if [[ "${KFSW_ROBOT_ACCEPTANCE:-0}" == 1 ]]; then
	"$robot_python" "$KFSW_REPO_DIR/tests/hil/software-preflight.py"
fi

# Which image a scenario picks, and what it refuses, is decided in shell and is
# worth checking without a bench attached.
"$robot_python" "$KFSW_REPO_DIR/tests/hil-scenarios-test.py"

# The fixtures refuse an existing output directory, so a second run would fail.
rm -rf "${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/robot/dry-run" "${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/robot/software"

echo "ROBOT: validate all suites without executing hardware actions"
KFSW_ROBOT_OUT_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/robot/dry-run" \
	"$KFSW_ROBOT_RUNNER" --dryrun

KFSW_EXTRA_CONF_FILE="$KFSW_REPO_DIR/tests/config/param-fixtures.conf;$KFSW_REPO_DIR/config/profiles/linux-temperature.conf" \
	"$KFSW_CI_DIR/build.sh" linux
KFSW_BUILD_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/tests/linux-node2" \
	KFSW_PRISTINE=always "$KFSW_REPO_DIR/tests/build-linux-node2.sh"

echo "ROBOT: execute every software-compatible scenario"
robot_status=0
KFSW_ROBOT_OUT_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/robot/software" \
	"$KFSW_ROBOT_RUNNER" --exclude physical || robot_status=$?

"$robot_python" "$KFSW_CI_DIR/robot-counts.py" \
	"${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/robot/software/output.xml"
[[ "$robot_status" -eq 0 ]] || exit "$robot_status"
echo "ROBOT RESULT: PASS"
