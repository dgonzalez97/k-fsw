#!/usr/bin/env bash
# Optional host tools; the manifest group remains disabled by default.
set -Eeuo pipefail
repo="$(dirname "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")")"
workspace="$(dirname "$repo")"
source_dir="${KFSW_CSP_TOOLS_DIR:-$workspace/tools/kfsw-csp-tools}"
if [[ ! -f "$source_dir/Cargo.toml" ]]; then
	cd "$workspace"
	west update kfsw-csp-tools
fi
"$repo/tools/kfsw-csp-tools" build
printf 'KFSW_CSP_TOOLS_TEST_BINARY=%s\nKFSW_CSP_IPERF_TEST_BINARY=%s\n' \
	"${CARGO_TARGET_DIR:-$source_dir/target}/release/csp-kiss" "${CARGO_TARGET_DIR:-$source_dir/target}/release/csp-iperf"
