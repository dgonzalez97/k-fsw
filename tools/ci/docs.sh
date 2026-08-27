#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_DOCS_CI_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_DOCS_CI_TOOL")"
KFSW_REPO_DIR="$(dirname "$(dirname "$KFSW_CI_DIR")")"

"$KFSW_REPO_DIR/tools/docs/build.sh"

index_file="$(dirname "$KFSW_REPO_DIR")/build/docs/html/index.html"
if [[ ! -s "$index_file" ]]; then
	echo "DOCS RESULT: FAIL (missing $index_file)"
	exit 1
fi

echo "DOCS RESULT: PASS ($index_file)"
