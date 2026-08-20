#!/usr/bin/env bash
set -Eeuo pipefail

KFSW_CI_BUILD_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_CI_BUILD_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

default_profiles=(
	linux
	linux_uart
	nucleo_l496zg
	nucleo_l496zg_uart
)

if [[ $# -gt 0 ]]; then
	profiles=("$@")
else
	profiles=("${default_profiles[@]}")
fi

for profile in "${profiles[@]}"; do
	echo
	echo "CI BUILD: $profile"
	KFSW_PRISTINE=always "$KFSW_TOOLS_DIR/build.sh" "$profile"
done
