#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_NODE2_BUILD_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_NODE2_BUILD_TOOL")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
node2_build_dir="${KFSW_BUILD_DIR:-$KFSW_WORKSPACE_ROOT/build/tests/linux-node2}"

KFSW_BUILD_DIR="$node2_build_dir" \
	KFSW_EXTRA_CONF_FILE="$KFSW_TESTS_DIR/config/linux-node2.conf" \
	"$KFSW_REPO_DIR/tools/build.sh" linux
