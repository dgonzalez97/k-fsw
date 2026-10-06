#!/usr/bin/env bash
# SPDX 2.3 bill of materials for the NUCLEO image: the application, Zephyr and
# every module and repository that went into it, with file hashes and
# licences, from Zephyr's west spdx.
set -Eeuo pipefail

KFSW_SBOM_TOOL="$(readlink -f "${BASH_SOURCE[0]}")"
KFSW_CI_DIR="$(dirname "$KFSW_SBOM_TOOL")"
KFSW_TOOLS_DIR="$(dirname "$KFSW_CI_DIR")"

target="${KFSW_SBOM_TARGET:-nucleo_l496zg}"

source "$KFSW_TOOLS_DIR/_common.sh" "$target"

out_dir="$KFSW_ROOT/build/sbom"
build_dir="$out_dir/build"
rm -rf "$out_dir"
mkdir -p "$out_dir"

# The CMake file-based API has to be asked for before the first configure, and
# the build has to leave zephyr.meta, the revisions of every module.
west spdx --init -d "$build_dir"
KFSW_BUILD_DIR="$build_dir" KFSW_PRISTINE=never KFSW_CMAKE_ARGS="-DCONFIG_BUILD_OUTPUT_META=y" \
	"$KFSW_TOOLS_DIR/build.sh" "$target" \
	>"$out_dir/build.log" 2>&1 || {
	tail -n 30 "$out_dir/build.log"
	echo "SBOM RESULT: FAIL (the build failed)"
	exit 1
}
west spdx -d "$build_dir" -s "$out_dir/spdx"

ls "$out_dir/spdx"
echo "SBOM RESULT: PASS ($out_dir/spdx)"
