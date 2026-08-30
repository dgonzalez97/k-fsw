#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_PDF_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_DOCS_TOOLS_DIR="$(dirname "$KFSW_PDF_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_DOCS_TOOLS_DIR")"
KFSW_REPO_DIR="$(dirname "$KFSW_TOOLS_DIR")"
KFSW_WORKSPACE_ROOT="$(dirname "$KFSW_REPO_DIR")"
KFSW_PDF_SOURCE_DIR="$KFSW_REPO_DIR/docs/pdf"
KFSW_PDF_OUTPUT="$KFSW_WORKSPACE_ROOT/build/k-fsw-guide.pdf"

if ! command -v pandoc >/dev/null 2>&1; then
	echo "ERROR: Pandoc is required to build the printable guide."
	echo "Debian/Ubuntu: sudo apt-get install pandoc"
	exit 1
fi

pdf_python=""
python_candidates=("$KFSW_WORKSPACE_ROOT/.venv/bin/python" python3)

for candidate in "${python_candidates[@]}"; do
	if [[ "$candidate" == */* && ! -x "$candidate" ]]; then
		continue
	fi

	if command -v "$candidate" >/dev/null 2>&1 \
		&& "$candidate" -c "import weasyprint" >/dev/null 2>&1; then
		pdf_python="$candidate"
		break
	fi
done

if [[ -z "$pdf_python" ]]; then
	echo "ERROR: WeasyPrint is required to build the printable guide."
	echo "Install it in the workspace environment with:"
	echo "  $KFSW_WORKSPACE_ROOT/.venv/bin/pip install -r $KFSW_PDF_SOURCE_DIR/requirements.txt"
	exit 1
fi

manual_sources=(
	"$KFSW_REPO_DIR/docs/index.md"
	"$KFSW_REPO_DIR/docs/getting-started/index.md"
	"$KFSW_REPO_DIR/docs/architecture/index.md"
	"$KFSW_REPO_DIR/docs/zephyr/index.md"
	"$KFSW_REPO_DIR/docs/communications/index.md"
	"$KFSW_REPO_DIR/docs/services/index.md"
	"$KFSW_REPO_DIR/docs/targets/index.md"
	"$KFSW_REPO_DIR/docs/commands/index.md"
	"$KFSW_REPO_DIR/docs/testing/index.md"
	"$KFSW_REPO_DIR/docs/development/index.md"
	"$KFSW_REPO_DIR/docs/status/index.md"
)

for input in "${manual_sources[@]}" \
	"$KFSW_PDF_SOURCE_DIR/guide.css" \
	"$KFSW_PDF_SOURCE_DIR/links.lua"; do
	if [[ ! -f "$input" ]]; then
		echo "ERROR: printable-guide input is missing: $input"
		exit 1
	fi
done

KFSW_PDF_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/kfsw-pdf.XXXXXX")"
trap 'rm -rf -- "$KFSW_PDF_TMP_DIR"' EXIT

guide_html="$KFSW_PDF_TMP_DIR/k-fsw-guide.html"
guide_date="$(LC_ALL=C date -u +'%d %B %Y')"
pandoc_version="$(pandoc --version)"
mkdir -p "$(dirname "$KFSW_PDF_OUTPUT")"

echo "PDF: ${pandoc_version%%$'\n'*}"
pandoc \
	--from markdown-citations \
	--file-scope \
	--standalone \
	--self-contained \
	--number-sections \
	--toc \
	--toc-depth=2 \
	--section-divs \
	--lua-filter="$KFSW_PDF_SOURCE_DIR/links.lua" \
	--css="$KFSW_PDF_SOURCE_DIR/guide.css" \
	--metadata title="K-FSW Guide" \
	--metadata subtitle="Engineering Manual" \
	--metadata date="$guide_date" \
	--output "$guide_html" \
	"${manual_sources[@]}"

echo "PDF: WeasyPrint $("$pdf_python" -c 'import weasyprint; print(weasyprint.__version__)')"
"$pdf_python" -m weasyprint "$guide_html" "$KFSW_PDF_OUTPUT"

if [[ ! -s "$KFSW_PDF_OUTPUT" ]]; then
	echo "PDF RESULT: FAIL (missing $KFSW_PDF_OUTPUT)"
	exit 1
fi

echo "PDF RESULT: PASS ($KFSW_PDF_OUTPUT)"
