"""Relay bytes between two pseudo terminals at a chosen line rate.

socat moves bytes at memory speed and a pseudo terminal ignores the termios
baud rate, so a hosted pair never feels the link a board has. This paces the
relay instead: 8N1 carries one byte per ten bits, so 4800 baud is 480 bytes a
second in each direction.
"""

import argparse
import os
from pathlib import Path
import select
import time


def relay(left, right, rate):
    budget = {left: 0.0, right: 0.0}
    for fd in (left, right):
        os.set_blocking(fd, False)
    while True:
        ready, _, _ = select.select([left, right], [], [], 0.2)
        for fd in ready:
            try:
                data = os.read(fd, 256)
            except BlockingIOError:
                continue
            if not data:
                return
            other = right if fd == left else left
            if rate > 0:
                now = time.monotonic()
                budget[fd] = max(budget[fd], now) + len(data) / rate
                time.sleep(max(0.0, budget[fd] - now))
            os.write(other, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("--baud", type=int, default=0,
                        help="line rate in bits a second; 0 relays as fast as it can")
    parser.add_argument("--ready-file")
    arguments = parser.parse_args()
    left = os.open(arguments.left, os.O_RDWR | os.O_NOCTTY)
    right = os.open(arguments.right, os.O_RDWR | os.O_NOCTTY)
    if arguments.ready_file:
        Path(arguments.ready_file).write_text('paced link ready\n')
    relay(left, right, arguments.baud / 10.0 if arguments.baud else 0.0)


if __name__ == "__main__":
    main()
