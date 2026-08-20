#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_ALL_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_ALL_TOOL")"

"$KFSW_CI_DIR/build.sh"
"$KFSW_CI_DIR/quality.sh"
"$KFSW_CI_DIR/unit.sh"
"$KFSW_CI_DIR/valgrind.sh"
