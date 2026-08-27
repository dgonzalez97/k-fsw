#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_SERVE_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_DOCS_TOOLS_DIR="$(dirname "$KFSW_SERVE_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOLS_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_DOCS_HTML="$KFSW_WORKSPACE_ROOT/build/docs/html"
port="${KFSW_DOCS_PORT:-8000}"

if [[ ! -s "$KFSW_DOCS_HTML/index.html" ]]; then
	echo "ERROR: generated documentation is missing."
	echo "Run: $KFSW_REPO_DIR/tools/docs/build.sh"
	exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
	echo "ERROR: python3 is required for the local documentation server"
	exit 1
fi

echo "Serving K-FSW documentation at http://127.0.0.1:$port/"
exec python3 -m http.server "$port" --bind 127.0.0.1 --directory "$KFSW_DOCS_HTML"
