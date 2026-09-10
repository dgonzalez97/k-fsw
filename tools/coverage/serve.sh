#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_SERVE_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_COVERAGE_TOOLS_DIR="$(dirname "$KFSW_SERVE_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_COVERAGE_TOOLS_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_COVERAGE_HTML="$KFSW_WORKSPACE_ROOT/build/coverage/html"
port="${1:-8001}"

if [[ ! -s "$KFSW_COVERAGE_HTML/index.html" ]]; then
	echo "ERROR: the coverage report is missing."
	echo "Run: $KFSW_REPO_DIR/tools/ci/coverage.sh"
	exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
	echo "ERROR: python3 is required for the local coverage server"
	exit 1
fi

echo "Serving K-FSW coverage at http://127.0.0.1:$port/"
exec python3 -m http.server "$port" --bind 127.0.0.1 --directory "$KFSW_COVERAGE_HTML"
