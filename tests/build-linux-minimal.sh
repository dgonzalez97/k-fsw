#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_MINIMAL_BUILD_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_TESTS_DIR="$(dirname "$KFSW_MINIMAL_BUILD_TOOL")"
KFSW_REPO_DIR="$(dirname "$KFSW_TESTS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"

KFSW_BUILD_DIR="$KFSW_WORKSPACE_ROOT/build/tests/linux-minimal" \
	KFSW_CONF_FILE="$KFSW_REPO_DIR/app/prj.conf;$KFSW_TESTS_DIR/config/linux-minimal.conf" \
	KFSW_PRISTINE=always \
	"$KFSW_REPO_DIR/tools/build.sh" linux
