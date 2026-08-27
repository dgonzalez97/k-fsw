#!/usr/bin/env python3
"""Flip one PARAM snapshot value byte in an offline native_sim flash image."""

import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <native-sim-flash>", file=sys.stderr)
        return 2

    path = pathlib.Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    magic = b"KPAR"
    name = b"test_u32"
    magic_offsets = [i for i in range(len(data)) if data.startswith(magic, i)]
    name_offsets = [i for i in range(len(data)) if data.startswith(name, i)]
    if len(magic_offsets) != 1 or len(name_offsets) != 1:
        print("snapshot fixture was not uniquely identifiable", file=sys.stderr)
        return 1

    value_offset = name_offsets[0] + len(name)
    if not magic_offsets[0] < value_offset < len(data):
        print("snapshot fixture value offset is invalid", file=sys.stderr)
        return 1

    data[value_offset] ^= 0x01
    path.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
