#!/usr/bin/env bash
set -Eeuo pipefail

# Run the ztest image twice with a persistent flash backend and fresh process RAM.
image="${1:?usage: boot-diagnostics-restart.sh <zephyr.exe>}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"$image" -flash="$test_dir/flash.bin" >"$test_dir/write.log" 2>&1
rg -q 'BOOT TRIAL WRITE VERIFIED' "$test_dir/write.log"
"$image" -flash="$test_dir/flash.bin" >"$test_dir/recover.log" 2>&1
rg -q 'BOOT TRIAL RECOVER VERIFIED' "$test_dir/recover.log"
echo 'BOOT DIAGNOSTICS RESTART: PASS'
