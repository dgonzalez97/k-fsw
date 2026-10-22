#!/usr/bin/env python3
"""Check recovery composition guards and restart with persistent simulated flash."""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path, help="Configured and built recovery FWU test directory")
args = parser.parse_args()
build = args.build.resolve()
if "CONFIG_KFSW_FWU_RECOVERY=y" not in (build / "zephyr/.config").read_text():
    parser.error("build must enable CONFIG_KFSW_FWU_RECOVERY")
entries = json.loads((build / "compile_commands.json").read_text())
entry = next(item for item in entries if item["file"].endswith("/src/fwu/fwu.c"))
command = shlex.split(entry["command"])

cases = {
    "offset": ("#undef CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS\n"
               "#define CONFIG_KFSW_FWU_SLOT_OFFSET_SECTORS 1", "swap offset"),
    "swap": ("#define CONFIG_BOOTLOADER_MCUBOOT 1\n"
             "#define CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET 1", "updater mode"),
    "swap_integration": ("#define CONFIG_KFSW_FWU_MCUBOOT 1", "secondary-slot services"),
    "golden": ("#define CONFIG_KFSW_GOLDEN 1", "secondary-slot services"),
    "scrub": ("#define CONFIG_KFSW_SCRUB 1", "secondary-slot services"),
    "files": ("#define CONFIG_KFSW_FWU_FILES 1", "secondary-slot services"),
    "target": ("#include <zephyr/devicetree.h>\n"
               "#undef DT_CHOSEN_kfsw_fwu_partition\n"
               "#define DT_CHOSEN_kfsw_fwu_partition DT_NODELABEL(slot1_partition)",
               "target primary A"),
    "code": ("#include <zephyr/devicetree.h>\n"
             "#undef DT_CHOSEN_zephyr_code_partition\n"
             "#define DT_CHOSEN_zephyr_code_partition DT_NODELABEL(slot0_partition)",
             "execute from permanent Golden"),
    "overlap": ("#include <zephyr/devicetree.h>\n"
                "#undef DT_REG_ADDR\n#define DT_REG_ADDR(node) 0", "must not overlap"),
    "no_bootloader": ("#undef CONFIG_ZTEST", "requires MCUboot outside native tests"),
}

with tempfile.TemporaryDirectory(prefix="kfsw-recovery-") as temporary:
    output = Path(temporary)
    command[command.index("-o") + 1] = str(output / "fwu.o")
    for name, (definitions, diagnostic) in cases.items():
        header = output / f"{name}.h"
        header.write_text(definitions + "\n")
        result = subprocess.run(command + ["-include", str(header)], cwd=entry["directory"],
                                capture_output=True, text=True, check=False)
        if result.returncode == 0 or diagnostic not in result.stderr:
            raise SystemExit(f"{name}: expected rejection missing\n{result.stderr}")
        print(f"{name}: rejected (compiler exit {result.returncode})")

    flash = output / "flash.bin"
    executable = build / "zephyr/zephyr.exe"
    for iteration in range(2):
        result = subprocess.run([str(executable), f"-flash={flash}",
                                 "-test=services_fwu::test_interruption_leaves_partial_image"],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0 or "PROJECT EXECUTION SUCCESSFUL" not in result.stdout:
            raise SystemExit(f"restart {iteration}: {result.stdout}\n{result.stderr}")
        # The native board's primary slot starts at 0xc000. Verify the partial
        # image survives process exit before testing that FWU starts idle again.
        with flash.open("rb") as stream:
            stream.seek(0xc000)
            assert stream.read(8) == bytes((index * 31 + 7) & 255 for index in range(8))
        print(f"partial upload process {iteration + 1}: exit 0, bytes persisted")
