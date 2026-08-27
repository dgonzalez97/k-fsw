#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ALL_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ALL_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"

"$KFSW_CI_DIR/build.sh"
"$KFSW_REPO_DIR/tests/storage-smoke.sh"
"$KFSW_CI_DIR/quality.sh"
"$KFSW_CI_DIR/unit.sh"
"$KFSW_CI_DIR/valgrind.sh"
