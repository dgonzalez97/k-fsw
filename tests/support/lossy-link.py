#!/usr/bin/env python3
"""Forward serial traffic and drop byte bursts to test transfer recovery.
"""

import argparse
import json
from pathlib import Path
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
    stats_options = parser.add_mutually_exclusive_group()
    stats_options.add_argument("--ftp-stats", help="record observed PUT blocks and retransmissions before loss")
    stats_options.add_argument("--lite-stats", help="record observed FWU lite blocks before loss")
    parser.add_argument("--csp-version", type=int, choices=(1, 2), default=2)
    arguments = parser.parse_args()

    if arguments.ftp_stats or arguments.lite_stats:
        sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
        from firmware_fixture import wire
        wire.csp_version = arguments.csp_version
        readers = {}
        blocks = set()
        resent = 0

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

                if arguments.ftp_stats or arguments.lite_stats:
                    reader = readers.setdefault(source, wire.KissReader())
                    for frame in reader.feed(chunk):
                        if len(frame) < wire.csp_header_bytes() + 12:
                            continue
                        header = wire.parse_csp_header(frame)
                        payload = frame[wire.csp_header_bytes():]
                        # Image blocks from the ops node to the target.
                        if header['source'] != 19:
                            continue
                        if arguments.ftp_stats:
                            if header['dport'] != 9 or payload[:2] != b'\x01\x0a':
                                continue
                            block = bytes(payload[4:12])  # request ID and offset
                        else:
                            if header['dport'] != 13 or payload[:1] != b'\x02':
                                continue
                            block = bytes(payload[2:4])  # block index
                        if block in blocks:
                            resent += 1
                        else:
                            blocks.add(block)

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
                if arguments.ftp_stats or arguments.lite_stats:
                    stats = Path(arguments.ftp_stats or arguments.lite_stats)
                    temporary = stats.with_suffix('.tmp')
                    temporary.write_text(json.dumps({
                        'blocks': len(blocks), 'resent': resent, 'dropped': dropped}))
                    temporary.replace(stats)
    except KeyboardInterrupt:
        pass
    finally:
        print(f"lossy link: forwarded {forwarded} bytes, dropped {dropped}",
              file=sys.stderr, flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
