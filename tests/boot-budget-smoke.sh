#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname "$0")/../tools/_common.sh" linux

build_root="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/tests/boot-budget"
fixture="$KFSW_REPO_DIR/tests/unit/services_boot_budget"
work_dir="$(mktemp -d /tmp/kfsw-boot-budget.XXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT

west build -b native_sim/native/64 -d "$build_root/app" "$fixture" -- \
    -DUSER_CACHE_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/cache"
west build -b native_sim/native/64 -d "$build_root/golden" "$fixture" -- \
    -DEXTRA_CONF_FILE="$fixture/golden.conf" \
    -DUSER_CACHE_DIR="${KFSW_OUTPUT_ROOT:-$KFSW_ROOT/build}/cache"

python3 - "$build_root" "$work_dir" <<'PYTEST'
from pathlib import Path
import subprocess
import sys

build, work = map(Path, sys.argv[1:])
flash = work / "flash.bin"

def run(image, case, marker):
    result = subprocess.run(
        [str(build / image / "zephyr/zephyr.exe"), "--no-color",
         f"-flash={flash}", "-boot_budget_hosted", f"-test=boot_budget::{case}"],
        text=True, capture_output=True, timeout=30, check=False)
    (work / f"{image}-{case}.log").write_text(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    assert marker in result.stdout, result.stdout + result.stderr
    print(marker)

run("app", "test_hosted_prepare", "BOOT BUDGET: prepared left=3")
run("app", "test_hosted_inspect", "BOOT BUDGET: application left=2")
run("app", "test_hosted_inspect", "BOOT BUDGET: application left=1")
# Three attempts means three startups: the one that spends the last attempt
# still runs the application, and the startup after it finds nothing left.
run("app", "test_hosted_inspect", "BOOT BUDGET: application left=0")
run("app", "test_hosted_inspect", "BOOT BUDGET: reset into installed Golden")
# The hosted bootloader model selects the recovery executable only after the
# actual installer has placed all recovery bytes in the secondary flash slot.
data = flash.read_bytes()
assert data[0x180000:0x181000] == data[0x76000:0x77000], "Golden copy differs"
run("golden", "test_hosted_golden",
    "BOOT BUDGET: Golden up; no budget consumed; self-install refused")
run("golden", "test_hosted_golden",
    "BOOT BUDGET: Golden up; no budget consumed; self-install refused")
print("BOOT BUDGET RESULT: PASS (hosted bootloader model)")
PYTEST
