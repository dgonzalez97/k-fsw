#!/usr/bin/env python3
"""Forward serial traffic, drop byte bursts, and end a pass mid-transfer.

Dropping bursts tests retransmission. Going deaf for a while tests the other
failure a real link has: the pass ends, both ends lose the conversation, and
whatever was being transferred has to continue on the next one.
"""

import argparse
import json
from pathlib import Path
import os
import random
import selectors
import sys
import time


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
    parser.add_argument("--cut-after-get-data", type=int, default=0,
                        help="go deaf once this many download chunks have been forwarded")
    parser.add_argument("--drop-put-data", type=int, default=0,
                        help="drop this upload chunk once, allowing its retransmission")
    parser.add_argument("--cut-for", type=float, default=20.0,
                        help="seconds to stay deaf, modelling the gap between two passes")
    stats_options = parser.add_mutually_exclusive_group()
    stats_options.add_argument("--ftp-stats", help="record observed PUT blocks and retransmissions before loss")
    stats_options.add_argument("--lite-stats", help="record observed FWU lite blocks before loss")
    parser.add_argument("--csp-version", type=int, choices=(1, 2), default=2)
    arguments = parser.parse_args()

    watch_frames = bool(arguments.ftp_stats or arguments.lite_stats
                        or arguments.cut_after_get_data or arguments.drop_put_data)
    if watch_frames:
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
    chunks_seen = 0
    deaf_until = 0.0
    cut_spent = False
    put_chunks_seen = 0
    put_drop_spent = False

    try:
        while True:
            events = selector.select(timeout=0.2)
            # Checked on the clock and not on arriving bytes, because the end
            # of a quiet pass has no bytes to trigger it.
            if deaf_until and time.monotonic() >= deaf_until:
                deaf_until = 0.0
                print("lossy link: forwarding again", file=sys.stderr, flush=True)
            for key, _ in events:
                source = key.fileobj
                destination = key.data
                try:
                    chunk = os.read(source, 512)
                except OSError:
                    return 0
                if not chunk:
                    continue

                if watch_frames:
                    reader = readers.setdefault(source, wire.KissReader())
                    frames = reader.feed(chunk)
                else:
                    frames = [chunk]

                for frame in frames:
                    chunk = wire.kiss_encode(frame) if watch_frames else frame
                    cut_after_forwarding = False
                    drop_put = False
                    if watch_frames and len(frame) >= wire.csp_header_bytes() + 12:
                        header = wire.parse_csp_header(frame)
                        payload = frame[wire.csp_header_bytes():]
                        if not deaf_until:
                            if (arguments.cut_after_get_data and not cut_spent
                                    and header['sport'] == 9
                                    and payload[:2] == b'\x01\x0e'):
                                chunks_seen += 1
                                cut_after_forwarding = chunks_seen >= arguments.cut_after_get_data
                            if (arguments.drop_put_data and not put_drop_spent
                                    and header['source'] == 19 and header['dport'] == 9
                                    and payload[:2] == b'\x01\x0a'):
                                put_chunks_seen += 1
                                if put_chunks_seen == arguments.drop_put_data:
                                    put_drop_spent = True
                                    drop_put = True
                        if (arguments.ftp_stats or arguments.lite_stats) and header['source'] == 19:
                            if arguments.ftp_stats:
                                is_block = header['dport'] == 9 and payload[:2] == b'\x01\x0a'
                                block = bytes(payload[4:12])  # request ID and offset
                            else:
                                is_block = header['dport'] == 13 and payload[:3] == b'\x01\x00\x02'
                                block = bytes(payload[8:20])  # transfer, generation and byte offset
                            if is_block:
                                if block in blocks:
                                    resent += 1
                                else:
                                    blocks.add(block)

                    if deaf_until or drop_put:
                        dropped += len(chunk)

                    output = bytearray()
                    for byte in (b"" if deaf_until or drop_put else chunk):
                        if skip_remaining > 0:
                            skip_remaining -= 1
                            dropped += 1
                            continue

                        output.append(byte)
                        forwarded += 1
                        if forwarded % arguments.drop_every == 0:
                            skip_remaining = arguments.drop_bytes

                    if output:
                        remaining = memoryview(output)
                        while remaining:
                            written = os.write(destination, remaining)
                            if written <= 0:
                                raise OSError("KISS endpoint stopped accepting bytes")
                            remaining = remaining[written:]
                    # Cut only after the complete triggering frame reached the receiver.
                    if cut_after_forwarding:
                        cut_spent = True
                        deaf_until = time.monotonic() + arguments.cut_for
                        print("lossy link: deaf for "
                              f"{arguments.cut_for}s after {chunks_seen} chunks",
                              file=sys.stderr, flush=True)
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
