#!/usr/bin/env python3
"""Offline F746 firmware-loader layout and signed-image integrity proof; no hardware IO."""

import argparse
import hashlib
import pathlib
import pickle
import re
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

REPO = pathlib.Path(__file__).resolve().parents[1]
MAP = {"boot_partition": (0, 0x10000), "slot1_partition": (0x10000, 0x30000),
       "slot0_partition": (0x40000, 0x40000), "storage_partition": (0x80000, 0x80000)}
BASE = 0x08000000


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def config(directory):
    return dict(line.removeprefix("CONFIG_").split("=", 1)
                for line in (directory / ".config").read_text().splitlines()
                if line.startswith("CONFIG_") and "=" in line)


def enabled(cfg, names, value="y"):
    for name in names.split():
        require(cfg.get(name, "n") == value, f"{name}: expected {value}")


def layout(edt):
    result = {name: (edt.label2node[name].regs[0].addr,
                     edt.label2node[name].regs[0].size) for name in MAP}
    require(result == MAP, "unexpected F746 partition map / overlap / size")
    require(edt.label2node["flash0"].regs[0].addr == BASE
            and edt.label2node["flash0"].regs[0].size == 0x100000,
            "expected 1 MiB STM32F746 flash")
    return result


def entry_check(data, image, start, header):
    require(data[:7] == b"\x7fELF\x01\x01\x01", "expected ELF32 little endian")
    require(struct.unpack_from("<HH", data, 16) == (2, 40), "expected ARM executable")
    entry = struct.unpack_from("<I", data, 24)[0] & ~1
    payload_end = header + struct.unpack_from("<I", image, 12)[0] if header else len(image)
    require(start + header <= entry < start + payload_end, "ELF entry outside image payload")


def signed_image(data, limit, header, public):
    require(32 <= len(data) <= limit, "signed image exceeds budget or is truncated")
    magic, load, hdr, protected, size, flags = struct.unpack_from("<IIHHII", data)
    require(magic == 0x96F3B83D and load == 0 and hdr == header and flags == 0,
            "wrong MCUboot header, load address, header size or flags")
    require(protected == 0, "unexpected protected TLVs in phase 1")
    end = hdr + size
    require(size > 0 and end + 4 <= len(data), "truncated image payload")
    tlv_magic, total = struct.unpack_from("<HH", data, end)
    require(tlv_magic == 0x6907 and end + total == len(data),
            "invalid TLV length or unexpected padding/trailer")
    values = {}
    position = end + 4
    while position < len(data):
        require(position + 4 <= len(data), "truncated TLV")
        kind, reserved, length = struct.unpack_from("<BBH", data, position)
        position += 4
        require(reserved == 0 and length > 0 and position + length <= len(data)
                and kind not in values, "invalid or duplicate TLV")
        values[kind] = data[position:position + length]
        position += length
    require(set(values) == {0x01, 0x10, 0x22}, "missing hash/key/signature or unsupported TLV")
    require(values[0x10] == hashlib.sha256(data[:end]).digest(), "image hash mismatch")
    require(values[0x01] == hashlib.sha256(public).digest(), "wrong signing key")
    key = serialization.load_der_public_key(public)
    require(isinstance(key, ec.EllipticCurvePublicKey) and isinstance(key.curve, ec.SECP256R1),
            "expected ECDSA P-256 public key")
    key.verify(values[0x22], data[:end], ec.ECDSA(hashes.SHA256()))
    return values[0x10].hex()


def public_key(directory):
    source = (directory / "autogen-pubkey.c").read_text()
    match = re.search(r"const unsigned char \w+_pub_key\[\]\s*=\s*\{([^}]+)\}", source)
    require(match is not None, "missing generated public key")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match[1]))


def check(root, workspace):
    sys.path.insert(0, str(workspace / "zephyr/scripts/dts/python-devicetree/src"))
    public = None
    for role in ("a", "golden"):
        build = root / role
        for domain in ("mcuboot", "app"):
            directory = build / domain / "zephyr"
            cfg = config(directory)
            enabled(cfg, "ARM CPU_CORTEX_M7 SOC_STM32F746XX BOARD_NUCLEO_F746ZG")
            with (directory / "edt.pickle").open("rb") as stream:
                edt = pickle.load(stream)
            regions = layout(edt)
            digest = None
            if domain == "mcuboot":
                enabled(cfg, "BOOT_FIRMWARE_LOADER BOOT_VALIDATE_SLOT0 "
                        "BOOT_FIRMWARE_LOADER_NO_APPLICATION GPIO "
                        "BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO BOOT_SIGNATURE_TYPE_ECDSA_P256")
                enabled(cfg, "BOOT_VALIDATE_SLOT0_ONCE BOOT_SWAP_USING_OFFSET "
                        "BOOT_SWAP_USING_MOVE BOOT_SWAP_USING_SCRATCH BOOT_UPGRADE_ONLY "
                        "SINGLE_APPLICATION_SLOT BOOT_DIRECT_XIP BOOT_RAM_LOAD MCUBOOT_SERIAL", "n")
                require(edt.get_node("mcuboot-button0") == edt.label2node["user_button"],
                        "recovery entrance must use board user button")
                key = public_key(directory)
                require(public is None or key == public, "bootloaders trust different keys")
                public = key
                label, header = "boot_partition", 0
                image = (directory / "zephyr.bin").read_bytes()
                require(public in image, "generated public key absent from MCUboot binary")
            else:
                enabled(cfg, "BOOTLOADER_MCUBOOT MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER "
                        "USE_DT_CODE_PARTITION KFSW_CSP CAN KFSW_CSP_CAN KFSW_CSP_KISS_UART "
                        "KFSW_CSP_UART_INTERRUPT_DRIVEN KFSW_PARAM KFSW_PARAM_CSP")
                enabled(cfg, "KFSW_FWU KFSW_FWU_LITE KFSW_GOLDEN KFSW_STORAGE "
                        "KFSW_STORAGE_TMP KFSW_PARAM_PERSISTENCE FILE_SYSTEM SHELL "
                        "KFSW_FTP KFSW_HK KFSW_FBO KFSW_JOURNAL KFSW_HEALTH "
                        "KFSW_LIMITS KFSW_REMEXEC KFSW_WATCHDOG KFSW_LASTWORDS", "n")
                enabled(cfg, "KFSW_COMMAND KFSW_COMMAND_CSP", "y" if role == "a" else "n")
                enabled(cfg, "MCUBOOT_APPLICATION_FIRMWARE_UPDATER", "y" if role == "golden" else "n")
                require(cfg.get("KFSW_CSP_CAN_INTERFACE_NAME") == '"CAN"'
                        and cfg.get("KFSW_CSP_ROUTE_TABLE") == '"1 KISS,0/0 CAN"'
                        and cfg.get("KFSW_CSP_ADDRESS") == "3", "wrong dual-link routing")
                label = "slot0_partition" if role == "a" else "slot1_partition"
                require(edt.chosen_node("zephyr,code-partition") == edt.label2node[label],
                        "wrong executable partition")
                header = int(cfg["ROM_START_OFFSET"], 0)
                require(header >= 0x200 and header % 0x200 == 0, "invalid vector/header alignment")
                image = (directory / "zephyr.signed.bin").read_bytes()
                digest = signed_image(image, regions[label][1], header, public)
            offset, budget = regions[label]
            require(int(cfg["FLASH_LOAD_OFFSET"], 0) == offset, "wrong configured load offset")
            require(int(cfg["FLASH_LOAD_SIZE"], 0) == budget, "wrong configured region size")
            require(len(image) <= budget, "image exceeds region")
            entry_check((directory / "zephyr.elf").read_bytes(), image,
                        BASE + offset, header)
            print(f"{role}/{domain}: offset={offset:#x}, bytes={len(image)}, budget={budget}"
                  + (f", sha256={digest}" if digest else ""))
    print("F746 offline proof PASS; boot execution and recovery upload NOT RUN")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--workspace", type=pathlib.Path, default=REPO.parent)
    args = parser.parse_args()
    try:
        check(args.output, args.workspace)
    except Exception as error:
        print(f"F746 proof FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
