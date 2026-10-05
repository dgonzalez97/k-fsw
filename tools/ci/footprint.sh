#!/usr/bin/env bash
# Flash and RAM use of the NUCLEO image, per symbol, from Zephyr's rom_report
# and ram_report, drawn as interactive sunbursts for the documentation.
set -Eeuo pipefail

KFSW_FOOTPRINT_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_FOOTPRINT_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

target="${KFSW_FOOTPRINT_TARGET:-nucleo_l496zg}"

source "$KFSW_TOOLS_DIR/_common.sh" "$target"

python -c 'import plotly' 2>/dev/null || {
	echo "ERROR: plotly is required"
	echo "  ./.venv/bin/pip install plotly"
	exit 1
}

out_dir="$KFSW_ROOT/build/footprint"
rm -rf "$out_dir"
mkdir -p "$out_dir"

"$KFSW_TOOLS_DIR/build.sh" "$target"

for report in rom ram; do
	echo "FOOTPRINT: $report report"
	# Keep the tree, not west's banner and command line with local paths.
	west build -d "$KFSW_BUILD_DIR" -t "${report}_report" |
		sed -n '/^Path /,$p' >"$out_dir/$report.txt"
	python "$ZEPHYR_BASE/scripts/footprint/plot.py" "$KFSW_BUILD_DIR/$report.json" \
		--html "$out_dir/$report.html"
done

echo "FOOTPRINT RESULT: PASS ($out_dir)"
