#!/usr/bin/env python3
"""Synthetic artifacts exercise offline proof without builds or hardware."""

import contextlib
import importlib.util
import io
import pathlib
import pickle
import struct
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

REPO = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("proof", REPO / "tools/f746-validate.py")
proof = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(proof)


class Edt:
    def __init__(self, label):
        self.label2node = {
            name: SimpleNamespace(regs=[SimpleNamespace(addr=addr, size=size)])
            for name, (addr, size) in proof.MAP.items()}
        self.label2node["flash0"] = SimpleNamespace(
            regs=[SimpleNamespace(addr=proof.BASE, size=0x100000)])
        self.label2node["user_button"] = SimpleNamespace()
        self.alias2node = {"mcuboot-button0": self.label2node["user_button"]}
        self.label = label

    def get_node(self, name):
        return self.alias2node[name]

    def chosen_node(self, name):
        return self.label2node[self.label]


def elf(image, start, header):
    data = bytearray(84)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", data, 16, 2, 40, 1)
    struct.pack_into("<II", data, 24, start + header + 1, 52)
    struct.pack_into("<HHH", data, 40, 52, 32, 1)
    struct.pack_into("<8I", data, 52, 1, 84, start, start, len(image), len(image), 5, 4)
    section_offset = len(data) + len(image)
    struct.pack_into("<I", data, 32, section_offset)
    struct.pack_into("<HH", data, 46, 40, 1)
    section = struct.pack("<10I", 0, 1, 2, start, 84, len(image), 0, 0, 4, 0)
    return bytes(data) + image + section


def signed(key, payload):
    data = bytearray(512)
    struct.pack_into("<IIHHII", data, 0, 0x96F3B83D, 0, 512, 0, len(payload), 0)
    data.extend(payload)
    public = key.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    signature = key.sign(bytes(data), ec.ECDSA(hashes.SHA256()))
    tlvs = b"".join(struct.pack("<BBH", kind, 0, len(value)) + value
                    for kind, value in ((1, proof.hashlib.sha256(public).digest()),
                                        (0x10, proof.hashlib.sha256(data).digest()),
                                        (0x22, signature)))
    return bytes(data) + struct.pack("<HH", 0x6907, len(tlvs) + 4) + tlvs


class ProofTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        key = ec.generate_private_key(ec.SECP256R1())
        public = key.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
        for role in ("a", "golden"):
            for domain in ("mcuboot", "app"):
                directory = self.root / role / domain / "zephyr"
                directory.mkdir(parents=True)
                cfg = {name: "y" for name in "ARM CPU_CORTEX_M7 SOC_STM32F746XX BOARD_NUCLEO_F746ZG".split()}
                header = 0
                if domain == "mcuboot":
                    cfg.update({name: "y" for name in (
                        "BOOT_FIRMWARE_LOADER BOOT_VALIDATE_SLOT0 "
                        "BOOT_FIRMWARE_LOADER_NO_APPLICATION GPIO "
                        "BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO BOOT_SIGNATURE_TYPE_ECDSA_P256").split()})
                    label = "boot_partition"
                    image = b"boot" + public
                    (directory / "autogen-pubkey.c").write_text(
                        "const unsigned char ecdsa_pub_key[] = {" +
                        ",".join(f"0x{value:02x}" for value in public) + "};")
                else:
                    cfg.update({name: "y" for name in (
                        "BOOTLOADER_MCUBOOT MCUBOOT_BOOTLOADER_MODE_FIRMWARE_UPDATER "
                        "USE_DT_CODE_PARTITION KFSW_CSP KFSW_CSP_KISS_UART "
                        "KFSW_CSP_UART_INTERRUPT_DRIVEN KFSW_PARAM KFSW_PARAM_CSP").split()})
                    cfg.update(KFSW_CSP_ROUTE_TABLE='"0/0 KISS"', KFSW_CSP_ADDRESS="3",
                               ROM_START_OFFSET="0x200")
                    if role == "a":
                        cfg.update(KFSW_COMMAND="y", KFSW_COMMAND_CSP="y")
                    else:
                        cfg["MCUBOOT_APPLICATION_FIRMWARE_UPDATER"] = "y"
                    label = "slot0_partition" if role == "a" else "slot1_partition"
                    header = 512
                    image = signed(key, b"prototype payload")
                offset = proof.MAP[label][0]
                cfg["FLASH_LOAD_OFFSET"] = hex(offset)
                cfg["FLASH_LOAD_SIZE"] = hex(proof.MAP[label][1])
                (directory / ".config").write_text("\n".join(
                    f"CONFIG_{name}={value}" for name, value in cfg.items()))
                (directory / "edt.pickle").write_bytes(pickle.dumps(Edt(label)))
                (directory / ("zephyr.bin" if domain == "mcuboot" else
                              "zephyr.signed.bin")).write_bytes(image)
                (directory / "zephyr.elf").write_bytes(elf(
                    image[:header + 17] if header else image, proof.BASE + offset, header))

    def check(self):
        with contextlib.redirect_stdout(io.StringIO()):
            proof.check(self.root, REPO.parent)

    def test_pinned_devicetree_api(self):
        sys.path.insert(0, str(REPO.parent / "zephyr/scripts/dts/python-devicetree/src"))
        from devicetree.edtlib import EDT

        partitions = "".join(
            f"{label}: partition@{offset:x} {{ reg = <{offset} {size}>; }};"
            for label, (offset, size) in proof.MAP.items())
        source = self.root / "proof.dts"
        source.write_text(
            '/dts-v1/; / { #address-cells = <1>; #size-cells = <1>; '
            'aliases { mcuboot-button0 = &user_button; }; '
            'chosen { zephyr,code-partition = &slot0_partition; }; '
            'user_button: button {}; '
            'flash0: flash@8000000 { reg = <0x08000000 0x100000>; '
            'partitions { #address-cells = <1>; #size-cells = <1>; ' + partitions + '}; }; };')
        edt = EDT(str(source), [])
        self.assertEqual(proof.layout(edt), proof.MAP)
        self.assertIs(edt.get_node("mcuboot-button0"), edt.label2node["user_button"])
        self.assertIs(edt.chosen_node("zephyr,code-partition"), edt.label2node["slot0_partition"])

    def test_cli_errors(self):
        tool = REPO / "tools/f746-proof.sh"
        for arguments, expected in ((("--help",), 0), (("flash",), 2),
                                    (("check", str(self.root / "missing")), 1)):
            with self.subTest(arguments=arguments):
                result = subprocess.run([str(tool), *arguments], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stderr)

    def test_happy(self):
        self.check()

    def test_missing_artifact(self):
        (self.root / "a/app/zephyr/zephyr.elf").unlink()
        with self.assertRaises(FileNotFoundError):
            self.check()

    def test_negative_configs(self):
        path = self.root / "a/mcuboot/zephyr/.config"
        original = path.read_text()
        for name in ("ARM", "CPU_CORTEX_M7", "SOC_STM32F746XX", "BOOT_FIRMWARE_LOADER",
                     "BOOT_VALIDATE_SLOT0", "BOOT_FIRMWARE_LOADER_NO_APPLICATION",
                     "BOOT_FIRMWARE_LOADER_ENTRANCE_GPIO", "BOOT_SIGNATURE_TYPE_ECDSA_P256"):
            with self.subTest(name=name):
                path.write_text(original.replace(f"CONFIG_{name}=y", f"CONFIG_{name}=n"))
                with self.assertRaises(ValueError):
                    self.check()
        path.write_text(original)

    def test_signed_corruption_and_oversize(self):
        path = self.root / "a/app/zephyr/zephyr.signed.bin"
        original = path.read_bytes()
        for position in (0, 8, 12, 16, 512, len(original) - 1):
            with self.subTest(position=position):
                damaged = bytearray(original)
                damaged[position] ^= 1
                path.write_bytes(damaged)
                with self.assertRaises(Exception):
                    self.check()
        path.write_bytes(original + bytes(0x40000))
        with self.assertRaisesRegex(ValueError, "budget"):
            self.check()

    def test_elf_segment_padding_is_not_payload(self):
        image = b"code" + b"\xff" * 4 + b"data"
        data = bytearray(elf(image, proof.BASE, 0))
        data[88:92] = b"\0" * 4
        offset = 84 + len(image)
        struct.pack_into("<HH", data, 46, 40, 2)
        struct.pack_into("<I", data, offset + 20, 4)
        data.extend(struct.pack("<10I", 0, 1, 2, proof.BASE + 8, 92, 4, 0, 0, 4, 0))
        proof.elf_check(bytes(data), image, proof.BASE, len(image), 0)
        data[92] ^= 1
        with self.assertRaisesRegex(ValueError, "section bytes"):
            proof.elf_check(bytes(data), image, proof.BASE, len(image), 0)

    def test_wrong_elf_architecture_and_address(self):
        path = self.root / "golden/app/zephyr/zephyr.elf"
        original = path.read_bytes()
        for position, value in ((18, 62), (64, proof.BASE + 0x40000)):
            damaged = bytearray(original)
            struct.pack_into("<I" if position == 64 else "<H", damaged, position, value)
            path.write_bytes(damaged)
            with self.assertRaises(ValueError):
                self.check()

    def test_overlap_and_wrong_partition(self):
        path = self.root / "golden/app/zephyr/edt.pickle"
        edt = pickle.loads(path.read_bytes())
        edt.label2node["slot1_partition"].regs[0].addr = 0x40000
        path.write_bytes(pickle.dumps(edt))
        with self.assertRaises(ValueError):
            self.check()

    def test_wrong_key(self):
        path = self.root / "a/app/zephyr/zephyr.signed.bin"
        path.write_bytes(signed(ec.generate_private_key(ec.SECP256R1()), b"prototype payload"))
        with self.assertRaisesRegex(ValueError, "signing key"):
            self.check()

    def test_bootloader_oversize(self):
        path = self.root / "a/mcuboot/zephyr/zephyr.bin"
        path.write_bytes(path.read_bytes() + bytes(0x10000))
        with self.assertRaisesRegex(ValueError, "exceeds"):
            self.check()

    def test_wrong_chosen_partition(self):
        path = self.root / "golden/app/zephyr/edt.pickle"
        edt = pickle.loads(path.read_bytes())
        edt.label = "slot0_partition"
        path.write_bytes(pickle.dumps(edt))
        with self.assertRaisesRegex(ValueError, "partition"):
            self.check()

    def test_forbidden_writer(self):
        path = self.root / "a/app/zephyr/.config"
        path.write_text(path.read_text() + "\nCONFIG_KFSW_FWU=y\n")
        with self.assertRaisesRegex(ValueError, "KFSW_FWU"):
            self.check()


if __name__ == "__main__":
    unittest.main()
