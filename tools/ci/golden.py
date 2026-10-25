#!/usr/bin/env python3
"""Build and check the recovery composition and its devicetree size limit."""

import argparse
import os
import pathlib
import pickle
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
WORKSPACE = REPO.parent
PROFILES = REPO / "config/profiles"
REQUIRED = {
    "KFSW_CSP": "recovery must remain reachable over comms",
    "KFSW_FWU": "recovery must accept a replacement firmware image",
    "KFSW_FWU_LITE": "recovery must provide the lite update path",
    "KFSW_PARAM": "operators must be able to inspect recovery parameters",
    "KFSW_GOLDEN_IMAGE": "the image must identify itself as recovery in @BOOT",
    "KFSW_LOG_REMOTE": "a failed recovery upload must be diagnosable from the ground",
}
FORBIDDEN = {
    "KFSW_STORAGE": "recovery must work without a filesystem",
    "KFSW_PARAM_PERSISTENCE": "persistent state must not prevent recovery",
    "KFSW_FTP": "recovery updates use FWU rather than filesystem transfer",
    "KFSW_CSP_HMAC_REQUIRE": "authentication must not lock operators out of recovery",
    "KFSW_DEBUG_SHELL": "recovery is commanded over CSP; there is no shell in orbit",
    "SHELL": "recovery is commanded over CSP; there is no shell in orbit",
    "MCUBOOT_SHELL": "recovery does not administer the bootloader locally",
}


def check(build):
    zephyr = build / "app/zephyr"
    config = dict(
        line.split("=", 1)
        for line in (zephyr / ".config").read_text().splitlines()
        if line.startswith("CONFIG_") and "=" in line
    )
    failures = []
    for rules, expected in ((REQUIRED, "y"), (FORBIDDEN, "n")):
        for symbol, reason in rules.items():
            value = config.get(f"CONFIG_{symbol}", "n")
            if value != expected:
                failures.append(f"CONFIG_{symbol}={value}, expected {expected}: {reason}")

    sys.path.insert(0, str(WORKSPACE / "zephyr/scripts/dts/python-devicetree/src"))
    with (zephyr / "edt.pickle").open("rb") as stream:
        edt = pickle.load(stream)
    partition = edt.label2node["kfsw_golden_partition"]
    limit = partition.regs[0].size
    if limit is None or limit <= 0:
        raise ValueError("golden partition has no positive devicetree size")
    size = (zephyr / "zephyr.signed.bin").stat().st_size
    print(f"GOLDEN: signed image {size} bytes, devicetree golden partition {limit} bytes")
    if size > limit:
        failures.append(
            f"image exceeds golden partition by {size - limit} bytes: "
            "the complete recovery image must fit where it is stored"
        )
    for failure in failures:
        print(f"GOLDEN: FAIL - {failure}")
    if failures:
        return 1
    print("GOLDEN RESULT: PASS - recovery composition and partition fit")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=pathlib.Path,
                        default=PROFILES / "nucleo-golden.conf")
    parser.add_argument("--build-dir", type=pathlib.Path,
                        default=pathlib.Path(os.environ.get("KFSW_OUTPUT_ROOT",
                                                         WORKSPACE / "build")) / "ci-golden")
    args = parser.parse_args()
    profile = args.profile.resolve(strict=True)
    build = args.build_dir.resolve()
    # Recovery is the last resort, so this gate may not pass without verifying.
    # MCUboot's own development key is in the tree and is enough to prove the
    # composition signs and fits; a release key only changes who signed it.
    candidates = [pathlib.Path(os.environ[name]).expanduser()
                  for name in ("KFSW_MCUBOOT_KEY",) if name in os.environ]
    candidates += [pathlib.Path("~/.config/kfsw/mcuboot-signing-key.pem").expanduser(),
                   WORKSPACE / "bootloader/mcuboot/root-ec-p256.pem"]
    key = next((path for path in candidates
                if path.is_file() and os.access(path, os.R_OK)), None)
    if key is None:
        print("GOLDEN: FAIL - no readable signing key; tried "
              + ", ".join(str(path) for path in candidates))
        return 1
    env = os.environ.copy()
    env.update(
        KFSW_SYSBUILD="1", KFSW_PRISTINE="always", KFSW_BUILD_DIR=str(build),
        KFSW_MCUBOOT_KEY=str(key.resolve()),
        KFSW_EXTRA_CONF_FILE=f"{PROFILES / 'nucleo-mcuboot.conf'};{profile}",
        KFSW_EXTRA_DTC_OVERLAY_FILE=";".join(str(PROFILES / name) for name in (
            "nucleo-mcuboot-flash.overlay", "nucleo-mcuboot.overlay",
            "nucleo-mcuboot-fwu.overlay", "nucleo-can.overlay")),
        KFSW_MCUBOOT_DTC_OVERLAY_FILE=str(PROFILES / "nucleo-mcuboot-flash.overlay"),
    )
    build.parent.mkdir(parents=True, exist_ok=True)
    log = build.with_name(build.name + ".log")
    with log.open("w") as stream:
        result = subprocess.run(["bash", str(REPO / "tools/build.sh"), "nucleo_l496zg"],
                                env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(f"GOLDEN: FAIL - build exited {result.returncode}; see {log}")
        return 1
    return check(build)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, ImportError, pickle.UnpicklingError) as error:
        sys.exit(f"GOLDEN: FAIL - {error}")
