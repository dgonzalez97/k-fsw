#!/usr/bin/env python3
"""Place a host file into a native_sim node's LittleFS partition.

The ground station runs as a native_sim process whose flash is a plain host
file, so a file can be put into its filesystem before it boots rather than
being transferred in. That matters for a firmware image: it is far larger than
anything the on-board diagnostic generator can produce, and it has to come from
the host in the first place.

The LittleFS geometry must match the one Zephyr mounts, or the node will see an
unformatted partition and refuse to mount it rather than silently misread it.
"""

import argparse
import pathlib
import sys

from littlefs import LittleFS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flash", required=True, help="native_sim flash backing file")
    parser.add_argument("--offset", required=True, help="partition offset, e.g. 0x100000")
    parser.add_argument("--size", required=True, help="partition size, e.g. 0x80000")
    parser.add_argument("--block-size", default="4096")
    parser.add_argument("--flash-size", default="0x200000")
    parser.add_argument("--read-size", default="16")
    parser.add_argument("--prog-size", default="16")
    parser.add_argument("--cache-size", default="64")
    parser.add_argument("--lookahead-size", default="32")
    parser.add_argument("source", help="host file to place")
    parser.add_argument("destination", help="path inside the partition, e.g. /firmware.bin")
    arguments = parser.parse_args()

    offset = int(arguments.offset, 0)
    size = int(arguments.size, 0)
    block_size = int(arguments.block_size, 0)
    flash_size = int(arguments.flash_size, 0)

    if size % block_size != 0:
        print(f"partition size {size} is not a multiple of the block size", file=sys.stderr)
        return 1

    payload = pathlib.Path(arguments.source).read_bytes()
    if len(payload) >= size:
        print(f"{arguments.source} is {len(payload)} bytes and does not fit in {size}",
              file=sys.stderr)
        return 1

    filesystem = LittleFS(
        block_size=block_size,
        block_count=size // block_size,
        read_size=int(arguments.read_size, 0),
        prog_size=int(arguments.prog_size, 0),
        cache_size=int(arguments.cache_size, 0),
        lookahead_size=int(arguments.lookahead_size, 0),
    )

    destination = arguments.destination.lstrip("/")
    parent = pathlib.PurePosixPath(destination).parent
    if str(parent) != ".":
        built = ""
        for part in parent.parts:
            built = f"{built}/{part}" if built else part
            try:
                filesystem.mkdir(built)
            except Exception:
                pass

    with filesystem.open(destination, "wb") as handle:
        handle.write(payload)

    image = filesystem.context.buffer

    flash = pathlib.Path(arguments.flash)
    # The backing file covers the whole simulated device, so it is padded to
    # full size before the partition is spliced in at its own offset.
    existing = flash.read_bytes() if flash.exists() else b""
    contents = bytearray(b"\xff" * flash_size)
    contents[: len(existing)] = existing[:flash_size]
    contents[offset : offset + len(image)] = image
    flash.write_bytes(bytes(contents))

    print(f"staged {len(payload)} bytes as /{destination} "
          f"at 0x{offset:x} in {flash} ({len(image)} byte filesystem)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
