#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ALL_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ALL_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

cd "$KFSW_WORKSPACE_ROOT"

# Only the environment, never a target: sourcing _common.sh here would export
# KFSW_BUILD_DIR and every later build would land in one directory. west is
# needed for the line below, and each stage sets up its own environment.
if [[ -f "$KFSW_WORKSPACE_ROOT/.venv/bin/activate" ]]; then
	source "$KFSW_WORKSPACE_ROOT/.venv/bin/activate"
fi

"$KFSW_REPO_DIR/tools/zephyr-patch.sh" --check
west manifest --validate
"$KFSW_CI_DIR/build.sh"
# Against the image build.sh just made, which carries no test fixtures. The
# workflow checks this too; having it only there is how two releases went out
# with a stale icd.md.
"$KFSW_REPO_DIR/tools/docs/icd.py" --check
python3 "$KFSW_CI_DIR/golden.py"
"$KFSW_CI_DIR/quality.sh"
"$KFSW_CI_DIR/unit.sh"
"$KFSW_CI_DIR/integration.sh"
"$KFSW_CI_DIR/csp-v1.sh"
"$KFSW_CI_DIR/valgrind.sh"
"$KFSW_CI_DIR/robot.sh"
"$KFSW_CI_DIR/docs.sh"
