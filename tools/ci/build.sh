#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_CI_BUILD_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_CI_BUILD_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

default_targets=(
	linux
	nucleo_l496zg
)

if [[ $# -gt 0 ]]; then
	targets=("$@")
else
	targets=("${default_targets[@]}")
fi

for target in "${targets[@]}"; do
	echo
	echo "CI BUILD: $target"
	KFSW_PRISTINE=always "$KFSW_TOOLS_DIR/build.sh" "$target"
done
