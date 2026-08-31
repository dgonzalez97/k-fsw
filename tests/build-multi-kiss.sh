#!/usr/bin/env bash
set -Eeuo pipefail

repo_dir="$(readlink -f "$(dirname "$0")/..")"
build_root="${KFSW_MULTI_KISS_BUILD_ROOT:-$(dirname "$repo_dir")/build/tests/multi-kiss}"

echo "MULTI-KISS BUILD: router"
KFSW_BUILD_DIR="$build_root/router" \
	KFSW_EXTRA_CONF_FILE="$repo_dir/tests/config/multi-kiss-router.conf" \
	KFSW_EXTRA_DTC_OVERLAY_FILE="$repo_dir/tests/config/multi-kiss-router.overlay" \
	KFSW_PRISTINE=always "$repo_dir/tools/build.sh" linux

echo "MULTI-KISS BUILD: node A"
KFSW_BUILD_DIR="$build_root/node-a" \
	KFSW_EXTRA_CONF_FILE="$repo_dir/tests/config/multi-kiss-node-a.conf" \
	KFSW_PRISTINE=always "$repo_dir/tools/build.sh" linux

echo "MULTI-KISS BUILD: node B"
KFSW_BUILD_DIR="$build_root/node-b" \
	KFSW_EXTRA_CONF_FILE="$repo_dir/tests/config/multi-kiss-node-b.conf" \
	KFSW_PRISTINE=always "$repo_dir/tools/build.sh" linux

echo "MULTI-KISS BUILD RESULT: PASS"
