#!/usr/bin/env python3
"""Inject acknowledgement loss and short contacts between native KISS nodes."""

import argparse
import json
import os
from pathlib import Path
import selectors
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--left', required=True)
    parser.add_argument('--right', required=True)
    parser.add_argument('--stats', required=True)
    parser.add_argument('--ready-file', required=True)
    parser.add_argument('--csp-version', type=int, choices=(1, 2), default=2)
    parser.add_argument('--lossy', action='store_true')
    parser.add_argument('--contacts', action='store_true')
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from firmware_fixture import wire
    wire.csp_version = args.csp_version

    left = os.open(args.left, os.O_RDWR | os.O_NOCTTY)
    right = os.open(args.right, os.O_RDWR | os.O_NOCTTY)
    selector = selectors.DefaultSelector()
    selector.register(left, selectors.EVENT_READ, right)
    selector.register(right, selectors.EVENT_READ, left)
    readers = {left: wire.KissReader(), right: wire.KissReader()}
    blocks = set()
    stats = dict(blocks=0, resent=0, dropped=0, begin_reply_dropped=0,
                 tail_reply_dropped=0, contacts=0)
    forwarded = 0
    skip = 0
    deaf_until = 0.0

    def save():
        path = Path(args.stats)
        temporary = path.with_suffix('.tmp')
        temporary.write_text(json.dumps(stats))
        temporary.replace(path)

    save()
    Path(args.ready_file).write_text('lossy link ready\n')
    try:
        while True:
            for key, _ in selector.select(timeout=.1):
                data = os.read(key.fileobj, 4096)
                if not data:
                    continue
                for frame in readers[key.fileobj].feed(data):
                    encoded = wire.kiss_encode(frame)
                    drop = time.monotonic() < deaf_until
                    if len(frame) >= wire.csp_header_bytes() + 28:
                        header = wire.parse_csp_header(frame)
                        payload = frame[wire.csp_header_bytes():]
                        lite = payload[0] == 1
                        request = lite and header['source'] == 19 and header['dport'] == 13
                        reply = lite and header['source'] == 16 and header['sport'] == 13
                        if request and payload[1:3] == b'\x00\x02':
                            block = payload[8:20]
                            if block in blocks:
                                stats['resent'] += 1
                            else:
                                blocks.add(block)
                                stats['blocks'] = len(blocks)
                                if (args.contacts and stats['contacts'] < 3
                                        and len(blocks) >= 10 * (stats['contacts'] + 1)):
                                    stats['contacts'] += 1
                                    deaf_until = time.monotonic() + 2.0
                                    drop = True
                        if args.lossy and reply and payload[1] == 1 and payload[3] == 0:
                            if payload[2] == 1 and not stats['begin_reply_dropped']:
                                stats['begin_reply_dropped'] = 1
                                drop = True
                            if (payload[2] == 2 and len(payload) >= 84
                                    and payload[16:20] == payload[42:46]
                                    and not stats['tail_reply_dropped']):
                                stats['tail_reply_dropped'] = 1
                                drop = True
                    if drop:
                        stats['dropped'] += len(encoded)
                        save()
                        continue
                    output = bytearray()
                    for byte in encoded:
                        if skip:
                            skip -= 1
                            stats['dropped'] += 1
                            continue
                        output.append(byte)
                        forwarded += 1
                        if args.lossy and forwarded % 9000 == 0:
                            skip = 6
                    if output:
                        remaining = memoryview(output)
                        while remaining:
                            written = os.write(key.data, remaining)
                            if written <= 0:
                                raise OSError('KISS endpoint stopped accepting bytes')
                            remaining = remaining[written:]
                    save()
    except KeyboardInterrupt:
        return 0
    finally:
        save()
        selector.close()
        os.close(left)
        os.close(right)


if __name__ == '__main__':
    sys.exit(main())
