#!/usr/bin/env python3
"""Turn `log remote` dictionary lines back into text.

A node whose log_remote_format is 1 sends each message as a cbprintf package:
the address of its format string, the arguments, and any strings that were in
RAM. The format strings stay in the image, so decoding needs the ELF of the
image that produced the lines. A four-byte IEEE CRC32 checks the node-rendered
text before it is printed. Missing or different CRCs leave the package marked
not decoded and return exit 1. This checks text agreement, not image identity;
identical text from another ELF and CRC32 collisions can pass. Other lines pass
through unchanged.

    tools/ground/log-decode.py --elf build/nucleo_l496zg/zephyr/zephyr.elf capture.txt
"""

import argparse
import binascii
import pathlib
import re
import struct
import sys

from elftools.elf.elffile import ELFFile

ZEPHYR_BASE = pathlib.Path(__file__).resolve().parents[3] / "zephyr"
sys.path.insert(0, str(ZEPHYR_BASE / "scripts" / "logging" / "dictionary"))

# pylint: disable=wrong-import-position
from dictionary_parser.log_database import LogDatabase  # noqa: E402
from dictionary_parser.log_parser import formalize_fmt_string  # noqa: E402
from dictionary_parser.log_parser_v3 import LogParserV3  # noqa: E402

PACKAGE = re.compile(r"\bpkg=(\S*)")
VERIFIED_PACKAGE = re.compile(r"00000000:([0-9a-fA-F]+) crc32=([0-9a-fA-F]{8})(?=\s|$)")
TEXT_SIZE = 192


class ElfStrings(LogDatabase):
    """A dictionary database that reads strings straight from the ELF."""

    def __init__(self, elf_path):
        super().__init__()
        self.sections = []
        with open(elf_path, "rb") as handle:
            elf = ELFFile(handle)
            self.set_tgt_bits(elf.elfclass)
            self.set_tgt_endianness(
                self.LITTLE_ENDIAN if elf.little_endian else self.BIG_ENDIAN
            )
            for section in elf.iter_sections():
                if section["sh_flags"] & 0x2 and section["sh_type"] != "SHT_NOBITS":
                    self.sections.append((section["sh_addr"], section.data()))

    def find_string(self, string_ptr):
        for start, data in self.sections:
            if start <= string_ptr < start + len(data):
                offset = string_ptr - start
                end = data.find(b"\0", offset)
                if end < 0:
                    return None
                return data[offset:end].decode("utf-8", errors="replace")
        return None

    def get_kconfigs(self):
        return {}


def decode(parser, database, package):
    """Rebuild the text of one package, as the node would have formatted it."""
    int_size = parser.data_types.get_sizeof(parser.data_types.INT)
    ptr_size = parser.data_types.get_sizeof(parser.data_types.PTR)
    ptr_format = ("<" if database.is_tgt_little_endian() else ">") + (
        "Q" if ptr_size == 8 else "I"
    )

    end_of_args = package[0] * int_size
    ro_count = package[2]
    rw_count = package[3]
    strings = LogParserV3.extract_string_table(package[end_of_args + ro_count + rw_count :])

    fmt_ptr = struct.unpack_from(ptr_format, package, ptr_size)[0]
    # pylint: disable=protected-access
    fmt = parser._LogParserV3__get_string(fmt_ptr, -ptr_size, strings)
    if not fmt or fmt.startswith("<string@"):
        raise ValueError(f"no format string at 0x{fmt_ptr:x}; is this the right ELF?")
    args = parser.process_one_fmt_str(fmt, package[2 * ptr_size : end_of_args], strings)
    return formalize_fmt_string(fmt) % args


def decode_line(parser, database, line):
    """Replace a package only after its rendered text agrees with the node CRC."""
    match = PACKAGE.search(line)
    if not match:
        return line
    verified = VERIFIED_PACKAGE.match(line, match.start(1))
    if not verified:
        raise ValueError("missing or malformed rendered-text CRC32")
    text = decode(parser, database, bytes.fromhex(verified.group(1)))
    # Match log_history_format: replace CR/LF, retain at most 191 bytes, omit NUL.
    rendered = text.encode("utf-8").replace(b"\r", b" ").replace(b"\n", b" ")
    rendered = rendered.split(b"\0", 1)[0][: TEXT_SIZE - 1]
    if binascii.crc32(rendered) != int(verified.group(2), 16):
        raise ValueError("rendered-text CRC32 mismatch; is this the right ELF?")
    return line[: match.start()] + rendered.decode("utf-8", errors="replace") + line[verified.end() :]


def main() -> int:
    argparser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    argparser.add_argument("--elf", required=True, help="zephyr.elf of the node's image")
    argparser.add_argument("capture", nargs="?", help="captured shell output; stdin if absent")
    args = argparser.parse_args()

    database = ElfStrings(args.elf)
    parser = LogParserV3(database)
    source = open(args.capture, encoding="utf-8") if args.capture else sys.stdin
    failures = 0

    with source:
        for line in source:
            line = line.rstrip("\r\n")
            try:
                line = decode_line(parser, database, line)
            except (ValueError, struct.error, TypeError, IndexError, OverflowError) as error:
                failures += 1
                line += f"  # not decoded: {error}"
            print(line)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
