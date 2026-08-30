#!/usr/bin/env python3
"""Send deterministic raw-radio requests and require every peer response."""

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
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=10.0)
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if not re.fullmatch(r"[0-9]{4}", args.sequence):
        print("HOLYBRO RAW RESULT: FAIL")
        print("  --sequence must contain exactly four decimal digits")
        return 2
    first_sequence = int(args.sequence)
    if args.count <= 0 or first_sequence + args.count > 10000:
        print("HOLYBRO RAW RESULT: FAIL")
        print("  count must be positive and the final sequence must not exceed 9999")
        return 2
    if args.baud <= 0 or args.timeout <= 0:
        print("HOLYBRO RAW RESULT: FAIL")
        print("  baud and timeout must be positive")
        return 2

    passed = 0
    invalid = 0
    timeouts = 0

    try:
        with serial.Serial(
            port=args.device,
            baudrate=args.baud,
            timeout=0.1,
            write_timeout=args.timeout,
            rtscts=False,
        ) as radio:
            radio.reset_input_buffer()
            for sequence_number in range(
                first_sequence, first_sequence + args.count
            ):
                sequence = f"{sequence_number:04d}"
                request = f"KGROUND-RAW-PING {sequence}\r\n".encode("ascii")
                response = f"KGROUND-RAW-PONG {sequence}".encode("ascii")
                received = bytearray()

                radio.write(request)
                radio.flush()

                deadline = time.monotonic() + args.timeout
                exchange_complete = False
                while time.monotonic() < deadline:
                    received.extend(radio.read(256))
                    while b"\n" in received:
                        raw_line, _, remainder = received.partition(b"\n")
                        received = bytearray(remainder)
                        line = raw_line.rstrip(b"\r")
                        if line == response:
                            passed += 1
                            exchange_complete = True
                            break
                        if line:
                            invalid += 1
                            print("HOLYBRO RAW RESULT: FAIL")
                            print(
                                f"  exchange {passed + 1}/{args.count} "
                                f"received invalid payload: {bytes(line)!r}"
                            )
                            print(
                                f"  exchanges={passed}/{args.count} "
                                f"invalid={invalid} timeouts={timeouts}"
                            )
                            return 1
                    if exchange_complete:
                        break
                    if len(received) > 4096:
                        invalid += 1
                        print("HOLYBRO RAW RESULT: FAIL")
                        print(
                            f"  exchange {passed + 1}/{args.count} "
                            "exceeded the receive-buffer limit"
                        )
                        print(
                            f"  exchanges={passed}/{args.count} "
                            f"invalid={invalid} timeouts={timeouts}"
                        )
                        return 1

                if not exchange_complete:
                    timeouts += 1
                    print("HOLYBRO RAW RESULT: FAIL")
                    print(
                        f"  exchange {passed + 1}/{args.count} timed out waiting "
                        f"for {response.decode('ascii')}"
                    )
                    if received:
                        print(f"  partial payload: {bytes(received)!r}")
                    print(
                        f"  exchanges={passed}/{args.count} "
                        f"invalid={invalid} timeouts={timeouts}"
                    )
                    return 1
    except (OSError, serial.SerialException) as error:
        print("HOLYBRO RAW RESULT: FAIL")
        print(f"  serial error: {error}")
        return 1

    print(
        "HOLYBRO RAW RESULT: PASS "
        f"device={args.device} baud={args.baud} "
        f"sequences={args.sequence}-{first_sequence + args.count - 1:04d} "
        f"exchanges={passed}/{args.count} invalid={invalid} timeouts={timeouts}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
