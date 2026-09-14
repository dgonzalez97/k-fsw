#!/usr/bin/env python3
"""Forward bytes between two serial endpoints and drop some of them.

Every so often a run of bytes is dropped, which the receiver sees as a lost
packet, so a transfer's recovery can be tested.
"""

import argparse
import os
import random
import selectors
import sys


def open_endpoint(path: str) -> int:
    return os.open(path, os.O_RDWR | os.O_NOCTTY)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--left", required=True, help="first endpoint, usually a pseudo-terminal")
    parser.add_argument("--right", required=True, help="second endpoint")
    parser.add_argument("--drop-every", type=int, default=4000,
                        help="drop a run of bytes once per this many forwarded")
    parser.add_argument("--drop-bytes", type=int, default=24,
                        help="length of each dropped run")
    parser.add_argument("--seed", type=int, default=1,
                        help="fixed so a failure can be reproduced")
    parser.add_argument("--ready-file", help="written once both endpoints are open")
    arguments = parser.parse_args()

    random.seed(arguments.seed)

    left = open_endpoint(arguments.left)
    right = open_endpoint(arguments.right)

    selector = selectors.DefaultSelector()
    selector.register(left, selectors.EVENT_READ, right)
    selector.register(right, selectors.EVENT_READ, left)

    if arguments.ready_file:
        with open(arguments.ready_file, "w", encoding="ascii") as handle:
            handle.write("lossy link ready\n")

    forwarded = 0
    dropped = 0
    skip_remaining = 0

    try:
        while True:
            for key, _ in selector.select(timeout=5.0):
                source = key.fileobj
                destination = key.data
                try:
                    chunk = os.read(source, 512)
                except OSError:
                    return 0
                if not chunk:
                    continue

                output = bytearray()
                for byte in chunk:
                    if skip_remaining > 0:
                        skip_remaining -= 1
                        dropped += 1
                        continue

                    output.append(byte)
                    forwarded += 1
                    if forwarded % arguments.drop_every == 0:
                        skip_remaining = arguments.drop_bytes

                if output:
                    os.write(destination, bytes(output))
    except KeyboardInterrupt:
        pass
    finally:
        print(f"lossy link: forwarded {forwarded} bytes, dropped {dropped}",
              file=sys.stderr, flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
