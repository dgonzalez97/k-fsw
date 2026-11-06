#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_DOCS_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_DOCS_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOLS_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
export KFSW_DOCS_OUTPUT="${KFSW_OUTPUT_ROOT:-$KFSW_WORKSPACE_ROOT/build}/docs"

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
	"$KFSW_WORKSPACE_ROOT/kfsw-modules/button-test/include/kfsw"
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

# Show the commit the manual was built from. Empty outside a git tree.
KFSW_DOCS_VERSION="$(git describe --tags --always --dirty 2>/dev/null || true)"
export KFSW_DOCS_VERSION

doxygen docs/Doxyfile

if [[ ! -s "$KFSW_DOCS_OUTPUT/html/index.html" ]]; then
	echo "ERROR: Doxygen did not generate $KFSW_DOCS_OUTPUT/html/index.html"
	exit 1
fi

# The reader's browser draws the diagrams. KFSW_MERMAID_JS names a local copy
# of mermaid.min.js for a manual read offline; otherwise the pinned release is
# fetched, and a reader without a network sees the diagram source instead.
mermaid_script='<script src="https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.min.js" integrity="sha384-rbtjAdnIQE/aQJGEgXrVUlMibdfTSa4PQju4HDhN3sR2PmaKFzhEafuePsl9H/9I" crossorigin="anonymous"></script>'
if [[ -n "${KFSW_MERMAID_JS:-}" ]]; then
	cp -- "$KFSW_MERMAID_JS" "$KFSW_DOCS_OUTPUT/html/mermaid.min.js"
	mermaid_script='<script src="mermaid.min.js"></script>'
fi
while IFS= read -r -d '' page; do
	sed -i "s#</body>#$mermaid_script\n<script>mermaid.initialize({ startOnLoad: true, theme: 'neutral' });</script>\n</body>#" "$page"
done < <(grep -lZ 'class="mermaid"' "$KFSW_DOCS_OUTPUT"/html/*.html || true)

echo "DOXYGEN RESULT: PASS"
