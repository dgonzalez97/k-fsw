#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_DOCS_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_DOCS_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOLS_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_DOCS_OUTPUT="$KFSW_WORKSPACE_ROOT/build/docs"

if ! command -v doxygen >/dev/null 2>&1; then
	echo "ERROR: Doxygen is required to build the K-FSW manual."
	echo "Debian/Ubuntu: sudo apt-get install doxygen"
	echo "Fedora: sudo dnf install doxygen"
	exit 1
fi

required_inputs=(
	"$KFSW_REPO_DIR/docs"
	"$KFSW_WORKSPACE_ROOT/kfsw-platform/include/kfsw"
	"$KFSW_WORKSPACE_ROOT/kfsw-services/include/kfsw"
	"$KFSW_WORKSPACE_ROOT/kfsw-comms/include/kfsw"
	"$KFSW_WORKSPACE_ROOT/kfsw-modules/radio-uhf/include/kfsw"
	"$KFSW_WORKSPACE_ROOT/kfsw-modules/boton-test/include/kfsw"
)

for input in "${required_inputs[@]}"; do
	if [[ ! -d "$input" ]]; then
		echo "ERROR: documentation input is missing: $input"
		echo "Run 'west update' from $KFSW_WORKSPACE_ROOT to restore pinned projects."
		exit 1
	fi
done

rm -rf -- "$KFSW_DOCS_OUTPUT"
mkdir -p "$KFSW_DOCS_OUTPUT"

echo "DOCS: Doxygen $(doxygen --version)"
echo "DOCS: output: $KFSW_DOCS_OUTPUT/html"

cd "$KFSW_REPO_DIR"
doxygen docs/Doxyfile

if [[ ! -s "$KFSW_DOCS_OUTPUT/html/index.html" ]]; then
	echo "ERROR: Doxygen did not generate $KFSW_DOCS_OUTPUT/html/index.html"
	exit 1
fi

echo "DOXYGEN RESULT: PASS"
