#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ROBOT_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ROBOT_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_ROBOT_RUNNER="$KFSW_REPO_DIR/tests/hil/run.sh"

echo "ROBOT: validate all suites without executing hardware actions"
KFSW_ROBOT_OUT_DIR="$KFSW_WORKSPACE_ROOT/build/robot/dry-run" \
	"$KFSW_ROBOT_RUNNER" --dryrun

"$KFSW_CI_DIR/build.sh" linux linux_node2

echo "ROBOT: execute every software-compatible scenario"
KFSW_ROBOT_OUT_DIR="$KFSW_WORKSPACE_ROOT/build/robot/software" \
	"$KFSW_ROBOT_RUNNER" --exclude physical

echo "ROBOT RESULT: PASS"
