#!/usr/bin/env bash
# Render the .dot sources in docs/media to SVG. The SVGs are committed because
# GitHub doesn't render graphviz in READMEs.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

command -v dot >/dev/null || {
	echo "ERROR: graphviz is required (apt-get install graphviz)"
	exit 1
}

for source in "$here"/docs/media/*.dot; do
	[[ -e "$source" ]] || continue
	dot -Tsvg "$source" -o "${source%.dot}.svg"
	echo "DIAGRAM: ${source%.dot}.svg"
done
