#!/usr/bin/env python3
"""Send one deterministic raw-radio request and require its peer response."""

import argparse
import re
import sys
import time

import serial


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True)
    parser.add_argument("--baud", type=int, default=57600)
    parser.add_argument("--sequence", default="0001")
    parser.add_argument("--timeout", type=float, default=10.0)
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if not re.fullmatch(r"[0-9]{4}", args.sequence):
        print("HOLYBRO RAW RESULT: FAIL")
        print("  --sequence must contain exactly four decimal digits")
        return 2
    if args.baud <= 0 or args.timeout <= 0:
        print("HOLYBRO RAW RESULT: FAIL")
        print("  baud and timeout must be positive")
        return 2

    request = f"KGROUND-RAW-PING {args.sequence}\r\n".encode("ascii")
    response = f"KGROUND-RAW-PONG {args.sequence}".encode("ascii")
    received = bytearray()

    try:
        with serial.Serial(
            port=args.device,
            baudrate=args.baud,
            timeout=0.1,
            write_timeout=args.timeout,
            rtscts=False,
        ) as radio:
            radio.reset_input_buffer()
            radio.write(request)
            radio.flush()

            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                received.extend(radio.read(256))
                if response in received:
                    print(
                        "HOLYBRO RAW RESULT: PASS "
                        f"device={args.device} baud={args.baud} sequence={args.sequence}"
                    )
                    return 0
                if len(received) > 4096:
                    del received[:-2048]
    except (OSError, serial.SerialException) as error:
        print("HOLYBRO RAW RESULT: FAIL")
        print(f"  serial error: {error}")
        return 1

    print("HOLYBRO RAW RESULT: FAIL")
    print(f"  timed out waiting for {response.decode('ascii')}")
    if received:
        print(f"  received: {bytes(received)!r}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
