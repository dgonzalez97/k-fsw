#!/usr/bin/env bash
# Render the .dot sources under docs/media to SVG.
#
# Committed as SVG rather than rendered on the fly, because GitHub will not run
# graphviz when it displays a README and a diagram nobody can see is worse than
# the ASCII it replaced.
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
